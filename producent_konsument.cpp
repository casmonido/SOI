

/**
 * compilation: g++ -pthread semafory.cpp
 *
 * This is an example of a classic producer-consumer problem.
 * We have 2 conveyor belts. There are two processes that each 
 * create Objects of a different type and put them on the first 
 * conveyor belt, and a few (user decides how many) processes 
 * that color Objects they picked up from the first conveyor
 * belt and put them on the second. Form there, Objects are 
 * picked up by a reading thred.
 * The 'difficulty' is, a set number of Objects is to be created
 * and all of them have to be consumed, and then all processes 
 * have to end on their own.  
 *
 * The two producent processes create Objects of two different 
 * types. The consumer processes color Objects each into different 
 * colors (represented by an int).
 */
#include <stdio.h>          /* printf(), because it is atomic */
#include <iostream>         /* new, delete              */
#include <stdlib.h>         /* exit(), malloc(), free() */
#include <sys/types.h>      /* key_t, sem_t, pid_t      */
#include <sys/shm.h>        /* shmat(), IPC_RMID        */
#include <errno.h>          /* errno, ECHILD            */
#include <semaphore.h>      /* sem_open(), sem_destroy(), sem_wait().. */
#include <fcntl.h>          /* O_CREAT, O_EXEC          */
#include <unistd.h>
#include <sys/wait.h>
#define N 5 // conveyor belt length
#define M 10 // number of Objects to be produced
#define SLEEPY_TIME 3



enum ObjectType {
  first_type,
  second_type,
  invalid
};



class Object 
{
private:
  int number;
  int color;
  ObjectType type;
  Object() { type = invalid; };
public:
  Object(ObjectType t): type(t) {};
  void set_number(int n) { number = n; };
  void set_color(int c) { color = c; };
};



class FIFO
{
public:
    Object conveyor_belt[N];
    int in;
    int out;
    void add_object(Object *p) // we do not create & delete Objects, because they are not shared; 
                               // instead we change properties of existing objects
    {
        in = (in + 1) % N;
        conveyor_belt[in].type = p->type;
        conveyor_belt[in].color = p->color;
    };
    Object remove_object() 
    {
      int o = out;
      out = (out + 1) % N;
      return conveyor_belt[o];
    }
};



class ProducerProcess 
{
private:
  Object *item;
  sem_t *mutex_all;
  int *counter_all;
public:
  ProducerProcess(ObjectType type, int *c, sem_t *m): counter_all(c), mutex_all(m)
  { 
    item = new Object(type); 
  };
  ~ProducerProcess() { delete item; };

  void run(FIFO *conveyor_belt, sem_t *full_slots, sem_t *empty_slots, sem_t *mutex) 
  {
    while (true)
    {
      sem_wait(mutex_all); // checking for end condition
      if (*counter_all > 0)
        *counter_all--;
      else
        break;
      sem_post(mutex_all);

      sem_wait(empty_slots); 
      sem_wait (mutex);      
      sleep(rand() % SLEEPY_TIME);
      printf("producer in A\n");
      sem_post(full_slots);
      sem_post (mutex);
    }
  }
};



class ColoringProcess 
{
private:
  int color;
  sem_t *mutex_all;
  int *counter_all;
  Object *item;
public:
  ColoringProcess(int color, int *c, sem_t *m): counter_all(c), mutex_all(m), color(color) {};

  void run(FIFO *conveyor_belt_A, sem_t *full_slots_A, sem_t *empty_slots_A, sem_t *mutex_A,
           FIFO *conveyor_belt_B, sem_t *full_slots_B, sem_t *empty_slots_B, sem_t *mutex_B) 
  {
    while (true)
    { 
      sem_wait(mutex_all); // checking for end condition
      if (*counter_all > 0)
        *counter_all--;
      else
        break;
      sem_post(mutex_all);

      sem_wait(full_slots_A);
      sem_wait(mutex_A);
      sleep(rand() % SLEEPY_TIME);
      printf("colorer in A\n");
      sem_post(empty_slots_A);
      sem_post (mutex_A);         

      sem_wait(empty_slots_B);
      sem_wait (mutex_B);       
      sleep(rand() % SLEEPY_TIME);    
      printf("colorer in B\n");
      sem_post(full_slots_B);
      sem_post (mutex_B);       
    }
  }
};



class ReadingProcess 
{
private:
  int *counter_all;
  Object *item;
public:
  ReadingProcess(int *c): counter_all(c) {};

  void run(FIFO *conveyor_belt, sem_t *full_slots, sem_t *empty_slots, sem_t *mutex) 
  {
    while (true)
    {
      if (*counter_all > 0) // checking for end condition - there is only one process of this kind
        *counter_all--;
      else
        break;

      sem_wait(full_slots); 
      sem_wait (mutex);         
      sleep(rand() % SLEEPY_TIME);
      conveyor_belt->remove_object();
      printf("Object nr %d, of type %c and color %c picked up from conveyor_belt_B\n", );
      sem_post(empty_slots);
      sem_post (mutex);
    }
  }
};



int main (int argc, char **argv)
{
    sem_t *mutex_A, *empty_slots_A, *full_slots_A, *mutex_B, *empty_slots_B, *full_slots_B;
    sem_t *mutex_all_produced, *mutex_all_colored;
    ReadingProcess *reading_process;
    ProducerProcess *producer_process;
    ColoringProcess *coloring_process;
    pid_t pid; // fork pid

    /* initialize a shared variable in shared memory */
    key_t shared_memory_key = ftok ("/dev/null", 597994444); // valid directory name and a number 
    printf ("shared_memory_key for conveyor belts = %d\n", shared_memory_key);
    int shared_memory_id = shmget (shared_memory_key, 2*sizeof(FIFO) + 3*sizeof(int), 0644 | IPC_CREAT);
    if (shared_memory_id < 0)
    { 
        fprintf(stderr, "Error creating shared memory - errno: %d\n", errno);
        exit (1);
    }
    FIFO *conveyor_belt_A = (FIFO *) shmat (shared_memory_id, NULL, 0); // attach shared variables to shared memory 
    FIFO *conveyor_belt_B = conveyor_belt_A + 1;
    conveyor_belt_A->in = -1;
    conveyor_belt_A->out = 0;
    conveyor_belt_B->in = -1;
    conveyor_belt_B->out = 0;
    int *all_produced_counter = (int *) shmat (shared_memory_id, NULL, 0);
    int *all_colored_counter = all_produced_counter + 1;
    int *all_read_counter = all_colored_counter + 1;
    *all_produced_counter = M;
    *all_colored_counter = M;
    *all_read_counter = M;
    printf ("shared objects allocated in shared memory.\n");

    /********************************************************/
    unsigned int coloring_thread_count = 0; 
    while (coloring_thread_count < 1) 
    {
      printf ("How many coloring processes do you want?\n");
      scanf ("%u", &coloring_thread_count);
    }
    /* initialize semaphores for shared processes */
    errno = 0; 
    if ((empty_slots_B = sem_open ("/empty_slots_B", O_CREAT | O_EXCL, 0644, N)) == SEM_FAILED) 
        fprintf(stderr, "sem_open() failed.  errno:%d\n", errno); // left out error checking for rest of them
    empty_slots_A = sem_open ("/empty_slots_A", O_CREAT | O_EXCL, 0644, N);

    mutex_A = sem_open ("/mutex_A", O_CREAT | O_EXCL, 0644, 1); 
    mutex_B = sem_open ("/mutex_B", O_CREAT | O_EXCL, 0644, 1); 

    full_slots_A = sem_open ("/full_slots_A", O_CREAT | O_EXCL, 0644, 0);
    full_slots_B = sem_open ("/full_slots_B", O_CREAT | O_EXCL, 0644, 0);

    mutex_all_produced = sem_open ("/mutex_all_produced", O_CREAT | O_EXCL, 0644, 1);
    mutex_all_colored = sem_open ("/mutex_all_colored", O_CREAT | O_EXCL, 0644, 1);
    // unlink prevents the semaphore existing forever if a crash occurs during the execution
    sem_unlink ("/empty_slots_B"); 
    sem_unlink ("/empty_slots_A");   
    sem_unlink ("/mutex_A");  
    sem_unlink ("/mutex_B");
    sem_unlink ("/full_slots_B");      
    sem_unlink ("/full_slots_A");   
 
    sem_unlink ("/mutex_all_produced");   
    sem_unlink ("/mutex_all_colored");    
    printf ("semaphores initialized.\n\n");

    // fork child processes 
    int process_num = 0;
    for ( ; process_num < coloring_thread_count + 3; process_num++)
    {
        pid = fork();
        if (pid < 0) 
            printf ("Fork error.\n");
        else if (pid == 0)
            break; // child process breaks and its process_num stays frozen
    }

    /******************   PARENT PROCESS   ****************/
    if (pid != 0)
    {
      printf ("\nParent: waiting for all children to exit.\n\n");
        while (pid = waitpid (-1, NULL, 0))
        {
            if (errno == ECHILD)
                break;
        }
        printf ("\nParent: All children have exited.\n");
        // shared memory detach
        shmdt (conveyor_belt_A);
        shmdt (conveyor_belt_B);
        shmdt (all_produced_counter);
        shmdt (all_colored_counter);
        shmdt (all_read_counter);
        shmctl (shared_memory_id, IPC_RMID, 0);
        // cleanup semaphores 
        sem_destroy (mutex_A);
        sem_destroy (mutex_B);
        sem_destroy (empty_slots_B);
        sem_destroy (empty_slots_A);
        sem_destroy (full_slots_B);
        sem_destroy (full_slots_A);
        sem_destroy (mutex_all_produced);    
        sem_destroy (mutex_all_colored);
        exit (0);
    }
    /******************   CHILD PROCESS   *****************/
    else
      switch (process_num)
      {
        case 0: 
          reading_process = new ReadingProcess(all_read_counter);
          reading_process->run(conveyor_belt_B, full_slots_B, empty_slots_B, mutex_B);
          delete reading_process;
          break;
        case 1:
        case 2: 
          producer_process = new ProducerProcess(static_cast<ObjectType> (process_num-1), all_produced_counter, mutex_all_produced);
          producer_process->run(conveyor_belt_A, full_slots_A, empty_slots_A, mutex_A);
          delete producer_process;
          break;
        default: 
          coloring_process = new ColoringProcess(process_num-3, all_colored_counter, mutex_all_colored);
          coloring_process->run(conveyor_belt_A, full_slots_A, empty_slots_A, mutex_A,
                                conveyor_belt_B, full_slots_B, empty_slots_B, mutex_B);
          delete coloring_process;
      }

}