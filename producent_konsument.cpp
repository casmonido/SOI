//g++ -lpthread save.cpp
#include <stdio.h>          /* printf()                 */
#include <iostream>         /* new, delete              */
#include <stdlib.h>         /* exit(), malloc(), free() */
#include <sys/types.h>      /* key_t, sem_t, pid_t      */
#include <sys/shm.h>        /* shmat(), IPC_RMID        */
#include <errno.h>          /* errno, ECHILD            */
#include <semaphore.h>      /* sem_open(), sem_destroy(), sem_wait().. */
#include <fcntl.h>          /* O_CREAT, O_EXEC          */
#include <unistd.h>
#include <sys/wait.h>
#define N 5 //zadana dlugosc tasmy

class Przedmiot 
{
public:
    static int ilosc; //TEN LICZNIK NIE JEST WSPOLDZIELONY wiec kazda instancja procesu tworzacego liczy sobie. tak tylko, orientacyjnie.
    int nr;
    int kolor;
    char typ;
    Przedmiot(): typ('a'), nr(-1), kolor(-1) {};
    Przedmiot(char t): typ(t), nr(ilosc) { ilosc += 1; };
    void koloruj(int i) { kolor = i; };
};

int Przedmiot::ilosc = 0;

class Kolejka
{
public:
    Przedmiot tasma[N];
    int in;
    int out;

    void add(Przedmiot *p) //nie tworzymy & usuwamy instancji Przedmiotu. zamiast tego, odbijamy je na Tasmie (przedmioty nie sa wspoldzielone,
    {                     //wiec rozne procesy nie moglyby ich tworzyc & kasowac)
        in = (in + 1) % N;
        tasma[in].typ = p->typ;
        tasma[in].kolor = p->kolor;
        tasma[in].nr = p->nr;
    };

    void read(int count) 
    {
      printf("\n%d) Z tasma[%d]: nr: %d, typ: %c, kolor: %d\n\n", count, out, tasma[out].nr, tasma[out].typ, tasma[out].kolor); //bo cout nie jest atomowe
      out = (out + 1) % N;
    };
};




int main (int argc, char **argv)
{
    int i;
    key_t shmkey;                 /*      shared memory key       */
    int shmid;                    /*      shared memory id        */
    sem_t *mutex_A, *wolne_A, *pelne_A, *mutex_B, *wolne_B, *pelne_B;
    sem_t *tworzace_end, *mutex_tworzace_end, *kolorujace_end, *mutex_kolorujace_end, *il_wprocesie;
    pid_t pid;                    /*      fork pid                */
    Kolejka *tasma_A, *tasma_B;   /*      shared variable         */
    unsigned int n;               /*      fork count              */


    /* initialize a shared variable in shared memory */
    shmkey = ftok ("/dev/null", 597999); /* valid directory name and a number */
    printf ("shmkey for tasma_A = %d\n", shmkey);
    shmid = shmget (shmkey, 2*sizeof (Kolejka), 0644 | IPC_CREAT);
    if (shmid < 0) 
    { /* shared memory error check */
        perror ("shmget\n");
        exit (1);
    }

    tasma_A = (Kolejka *) shmat (shmid, NULL, 0); /* attach p to shared memory */
    tasma_B = tasma_A+1;
    tasma_A->in = -1;
    tasma_A->out = 0;
    tasma_B->in = -1;
    tasma_B->out = 0;
    printf ("tasma_A, tasma_B, %d is allocated in shared memory.\n\n", tasma_B->in);

    /********************************************************/

    printf ("How many children do you want to fork?\n");
    printf ("Fork count: ");
    scanf ("%u", &n);

    /* initialize semaphores for shared processes */
    errno = 0; 
    if ((wolne_A = sem_open ("/wolne_A", O_CREAT | O_EXCL, 0644, N)) == SEM_FAILED) //przedstawia ilosc wolnych miejsc
        fprintf(stderr, "sem_open() failed.  errno:%d\n", errno); //zwykle jak juz jeden wyjdzie to reszta tez.
    mutex_A = sem_open ("/mutex_A", O_CREAT | O_EXCL, 0644, 1); 
    pelne_A = sem_open ("/pelne_A", O_CREAT | O_EXCL, 0644, 0);

    wolne_B = sem_open ("/wolne_B", O_CREAT | O_EXCL, 0644, N);
    mutex_B = sem_open ("/mutex_B", O_CREAT | O_EXCL, 0644, 1); 
    pelne_B = sem_open ("/pelne_B", O_CREAT | O_EXCL, 0644, 0);

    tworzace_end = sem_open ("/tworzace_end", O_CREAT | O_EXCL, 0644, 10);
    mutex_tworzace_end = sem_open ("/mutex_tworzace_end", O_CREAT | O_EXCL, 0644, 1);
    kolorujace_end = sem_open ("/kolorujace_end", O_CREAT | O_EXCL, 0644, 2);
    mutex_kolorujace_end = sem_open ("/mutex_kolorujace_end", O_CREAT | O_EXCL, 0644, 1);
    il_wprocesie = sem_open ("/il_wprocesie", O_CREAT | O_EXCL, 0644, 0);

    sem_unlink ("/wolne_A");   
    sem_unlink ("/mutex_A");  
    sem_unlink ("/pelne_A");  
    sem_unlink ("/wolne_B");   
    sem_unlink ("/mutex_B");  
    sem_unlink ("/pelne_B");   

    sem_unlink ("/tworzace_end"); 
    sem_unlink ("/mutex_tworzace_end");    
    sem_unlink ("/kolorujace_end"); 
    sem_unlink ("/mutex_kolorujace_end"); 
    sem_unlink ("/il_wprocesie"); 
    /* unlink prevents the semaphore existing forever */
    /* if a crash occurs during the execution         */
    printf ("semaphores initialized.\n\n");


    /* fork child processes */
    for (i = 0; i < n; i++){
        pid = fork();
        if (pid < 0)                /* check for error      */
            printf ("Fork error.\n");
        else if (pid == 0)
            break;                  /* child processes */
    }


    /******************************************************/
    /******************   PARENT PROCESS   ****************/
    /******************************************************/
    if (pid != 0)
    {
      printf ("\nParent: waiting for all children to exit.\n\n");

        while (pid = waitpid (-1, NULL, 0))
        {
            if (errno == ECHILD)
                break;
        }

        printf ("\nParent: All children have exited.\n");

        /* shared memory detach */
        shmdt (tasma_A);
        shmdt (tasma_B);
        shmctl (shmid, IPC_RMID, 0);

        /* cleanup semaphores */
        sem_destroy (mutex_A);
        sem_destroy (wolne_A);
        sem_destroy (pelne_A);
        sem_destroy (mutex_B);
        sem_destroy (wolne_B);
        sem_destroy (pelne_B);
        sem_destroy (tworzace_end); 
        sem_destroy (mutex_tworzace_end);    
        sem_destroy (kolorujace_end); 
        sem_destroy (mutex_kolorujace_end); 
        sem_destroy (il_wprocesie); 
        exit (0);
    }

    /******************************************************/
    /******************   CHILD PROCESS   *****************/
    /******************************************************/
    else
      switch (i)
      {
/********************************************************************************/
/******************************* PROCES CZYTAJACY *******************************/
/********************************************************************************/
        case 0: 
        {
          int a, il = 0;
          while (1) 
          { 
            //
            sem_getvalue(tworzace_end, &a);
            if (a == 0) 
            {
                sem_getvalue(il_wprocesie, &a);
                if (il == a) 
                {
                    printf ("\t\t\t\tczytacz wyszedl\n");
                    exit(0);
                };
            };
            sem_wait(pelne_B); 
            //

                  sem_wait (mutex_B);
                        printf ("czytacz\t wchodzi do B\n");
                        tasma_B->read(il);
                        il += 1;
            sem_post (wolne_B); 
                        printf ("czytacz\t  opuszcza  B\n");
                  sem_post (mutex_B);  
          } 
        }
/***************************************************************************************************/
/******************************* PROCES TWORZACY PRZEDMIOTY TYPU 'R' *******************************/
/***************************************************************************************************/
        case 1: 
        {
          int b;
          Przedmiot *p;

          do
          {
            sem_wait(wolne_A); //stad sie wszystko zaczyna!
                  sem_wait (mutex_A);         
                        printf ("\ttworca R\t wchodzi do A\n");
                        p = new Przedmiot('R');
                        sleep (1);
                        tasma_A->add(p);
                        sem_post(il_wprocesie);
                        delete p;
            sem_post(pelne_A); //post() & wait() operations are ATOMIC
                        printf ("\ttworca R\t  opuszcza  A\n");
                  sem_post (mutex_A);


            sem_wait(mutex_tworzace_end);
                  sem_getvalue(tworzace_end, &b);
                  if (b > 0) sem_wait(tworzace_end);
                  b -= 1;
            sem_post(mutex_tworzace_end);
          } while (b); //wciaz moze sie zdarzyc ze wyprodukowany zostanie o 1 wiecej Przedmiot

          sem_wait(kolorujace_end);
          printf ("\t\t\t\ttworca R wyszedl\n");
          exit (0);
        }
/***************************************************************************************************/
/******************************* PROCES TWORZACY PRZEDMIOTY TYPU 'P' *******************************/
/***************************************************************************************************/
        case 2:
        {
          int c;
          Przedmiot *p;

          do
          {
            sem_wait(wolne_A);
                  sem_wait (mutex_A);           
                        printf ("\ttworca P\t wchodzi do A\n");
                        p = new Przedmiot('P');
                        sleep (1);
                        tasma_A->add(p);
                        sem_post(il_wprocesie);
                        delete p;
            sem_post(pelne_A);
                        printf ("\ttworca P\t  opuszcza  A\n");
                  sem_post (mutex_A); 


            sem_wait(mutex_tworzace_end);
                  sem_getvalue(tworzace_end, &c);
                  if (c > 0) sem_wait(tworzace_end);
                  c -= 1;
            sem_post(mutex_tworzace_end);
          } while(c);

          sem_wait(kolorujace_end);
          printf ("\t\t\t\ttworca P wyszedl\n");
          exit (0);
        }
/**********************************************************************************/
/******************************* PROCESY KOLORUJACE *******************************/
/**********************************************************************************/
        default: 
        {
          int d, notall;
          Przedmiot p;

          do 
          { 


                sem_wait(pelne_A);


          printf ("\tkolorujacy %d\t wchodzi do A\n", i);
          sleep (1);
          
          p.typ = tasma_A->tasma[tasma_A->out].typ;
          p.nr = tasma_A->tasma[tasma_A->out].nr;
          tasma_A->out = (tasma_A->out + 1) % N;
          sem_post(wolne_A);
          printf ("\tkolorujacy %d\t  opuszcza  A\n", i);
          sem_post (mutex_A);         

          //zrob cos dalej z tym co wziales
          p.koloruj(i);
            sem_wait(wolne_B);
            sem_wait (mutex_B);           
            printf ("kolorujacy %d\t wchodzi do B\n", i);
            sleep (1);
            tasma_B->add(&p);
            sem_post(pelne_B);
            printf ("kolorujacy %d\t  opuszcza  B\n", i);
            sem_post (mutex_B);       

          } while (1);

          printf ("\t\t\t\tkolorujacy %d wyszedl\n", i);
          exit(0);
        };     
      }
}