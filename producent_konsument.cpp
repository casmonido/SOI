

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
#define M 20 // number of Objects to be produced



enum ObjectType {
  first_type,
  second_type,
  invalid
};



class Object 
{
private:
  Object() { type = invalid; };
public:
  int color;
  ObjectType type;
  Object(ObjectType t): type(t) {};
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
public:
  ProducerProcess(ObjectType type) { item = new Object(type); };
  ~ProducerProcess() { delete item; };

  void run(FIFO *conveyor_belt, sem_t *full_slots, sem_t *empty_slots, sem_t *mutex) 
  {
    while (true)
    {
      sem_wait(empty_slots); 
      sem_wait (mutex);         

      sem_post(full_slots);
      sem_post (mutex);
    }
  }
};



class ColoringProcess 
{
private:
  int color;
public:
  ColoringProcess(int c): color(c) {};

  void run(FIFO *conveyor_belt_A, sem_t *full_slots_A, sem_t *empty_slots_A, sem_t *mutex_A,
           FIFO *conveyor_belt_B, sem_t *full_slots_B, sem_t *empty_slots_B, sem_t *mutex_B) 
  {
    while (true)
    { 
      sem_wait(full_slots_A);
      sem_wait(mutex_A);

      sem_post(empty_slots_A);
      sem_post (mutex_A);         

      sem_wait(empty_slots_B);
      sem_wait (mutex_B);           

      sem_post(full_slots_B);
      sem_post (mutex_B);       
    }
  }
};



class ReadingProcess 
{
public:
  ReadingProcess() {};

  void run(FIFO *conveyor_belt, sem_t *full_slots, sem_t *empty_slots, sem_t *mutex) 
  {
    while (true)
    {
      sem_wait(full_slots); 
      sem_wait (mutex);         

      sem_post(empty_slots);
      sem_post (mutex);
    }
  }
};



int main (int argc, char **argv)
{
    sem_t *mutex_A, *empty_slots_A, *full_slots_A, *mutex_B, *empty_slots_B, *full_slots_B;
    sem_t *all_produced_counter, *mutex_all_produced, *all_colored_counter, *mutex_all_colored, *all_read_counter;
    ReadingProcess *reading_process;
    ProducerProcess *producer_process;
    ColoringProcess *coloring_process;
    pid_t pid; // fork pid

    /* initialize a shared variable in shared memory */
    key_t shared_memory_key = ftok ("/dev/null", 597999); // valid directory name and a number 
    printf ("shared_memory_key for conveyor belts = %d\n", shared_memory_key);
    int shared_memory_id = shmget (shared_memory_key, 2*sizeof (FIFO), 0644 | IPC_CREAT);
    if (shared_memory_id < 0) // shared memory error check
    { 
        perror ("shmget\n");
        exit (1);
    }
    FIFO *conveyor_belt_A = (FIFO *) shmat (shared_memory_id, NULL, 0); // attach shared variables to shared memory 
    FIFO *conveyor_belt_B = conveyor_belt_A+1;
    conveyor_belt_A->in = -1;
    conveyor_belt_A->out = 0;
    conveyor_belt_B->in = -1;
    conveyor_belt_B->out = 0;
    printf ("conveyor belts allocated in shared memory.\n");

    /********************************************************/
    unsigned int coloring_thread_count = 0; 
    while (coloring_thread_count < 1) 
    {
      printf ("How many coloring processes do you want?\n");
      scanf ("%u", &coloring_thread_count);
    }
    /* initialize semaphores for shared processes */
    errno = 0; 
    if ((empty_slots_B = sem_open ("/empty_slots_B", O_CREAT | O_EXCL, 0644, N)) == SEM_FAILED) //przedstawia counter wolnych miejsc
        fprintf(stderr, "sem_open() failed.  errno:%d\n", errno); //zwykle jak juz jeden wyjdzie to reszta tez.
    mutex_A = sem_open ("/mutex_A", O_CREAT | O_EXCL, 0644, 1); 
    full_slots_B = sem_open ("/full_slots_B", O_CREAT | O_EXCL, 0644, 0);

    empty_slots_B = sem_open ("/empty_slots_B", O_CREAT | O_EXCL, 0644, N);
    mutex_B = sem_open ("/mutex_B", O_CREAT | O_EXCL, 0644, 1); 
    full_slots_B = sem_open ("/full_slots_B", O_CREAT | O_EXCL, 0644, 0);

    all_produced_counter = sem_open ("/all_produced_counter", O_CREAT | O_EXCL, 0644, 10);
    mutex_all_produced = sem_open ("/mutex_all_produced", O_CREAT | O_EXCL, 0644, 1);
    all_colored_counter = sem_open ("/all_colored_counter", O_CREAT | O_EXCL, 0644, 2);
    mutex_all_colored = sem_open ("/mutex_all_colored", O_CREAT | O_EXCL, 0644, 1);
    all_read_counter = sem_open ("/all_read_counter", O_CREAT | O_EXCL, 0644, 0);

    sem_unlink ("/empty_slots_B");   
    sem_unlink ("/mutex_A");  
    sem_unlink ("/full_slots_B");  
    sem_unlink ("/empty_slots_B");   
    sem_unlink ("/mutex_B");  
    sem_unlink ("/full_slots_B");   

    sem_unlink ("/all_produced_counter"); 
    sem_unlink ("/mutex_all_produced");    
    sem_unlink ("/all_colored_counter"); 
    sem_unlink ("/mutex_all_colored"); 
    sem_unlink ("/all_read_counter"); 
    // unlink prevents the semaphore existing forever if a crash occurs during the execution         
    printf ("semaphores initialized.\n\n");

    // fork child processes 
    int process_num = 0;
    for ( ; process_num < coloring_thread_count + 3; process_num++){
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
        shmctl (shared_memory_id, IPC_RMID, 0);
        // cleanup semaphores 
        sem_destroy (mutex_A);
        sem_destroy (empty_slots_B);
        sem_destroy (full_slots_B);
        sem_destroy (mutex_B);
        sem_destroy (empty_slots_B);
        sem_destroy (full_slots_B);
        sem_destroy (all_produced_counter); 
        sem_destroy (mutex_all_produced);    
        sem_destroy (all_colored_counter); 
        sem_destroy (mutex_all_colored); 
        sem_destroy (all_read_counter); 
        exit (0);
    }
    /******************   CHILD PROCESS   *****************/
    else
      switch (process_num)
      {
        case 0: 
          reading_process = new ReadingProcess();
          reading_process->run(conveyor_belt_B, full_slots_B, empty_slots_B, mutex_B);
          delete reading_process;
          break;
        case 1:
        case 2: 
          producer_process = new ProducerProcess(static_cast<ObjectType> (process_num-1));
          producer_process->run(conveyor_belt_A, full_slots_A, empty_slots_A, mutex_A);
          delete producer_process;
          break;
        default: 
          coloring_process = new ColoringProcess(process_num-3);
          coloring_process->run(conveyor_belt_A, full_slots_A, empty_slots_A, mutex_A,
                                conveyor_belt_B, full_slots_B, empty_slots_B, mutex_B);
          delete coloring_process;
      }

}