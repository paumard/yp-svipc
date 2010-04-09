#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#if !defined(__USE_GNU)
#define __USE_GNU
#endif

#include <sys/ipc.h>
#include <sys/sem.h>

#include "svipc_misc.h"
#include "svipc_sem.h"


/*******************************************************************
 * define
 *******************************************************************/

/* fixme - which sys header defines semun ??? */
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO (Linux specific) */
};

//---------------------------------------------------------------
// svipc_sem_init
//---------------------------------------------------------------
int svipc_sem_init(long key, long numslots) {
   int i, status;
   int sempoolid = -1;
   
   Debug(5, "svipc_sem_init %lx\n",key);

   
   if (numslots>=0) {
      sempoolid = semget(key,numslots,IPC_CREAT|IPC_PRIVATE|IPC_EXCL|0666);
      if (sempoolid == -1) {
         perror ("sempoolid semget failed");
         return -1;
      }

      // all semaphores are locked at startup
      union semun semctlops;
      semctlops.val=0;
      // fixme - SETALL perf improvement
      for (i=0;i<numslots;i++) {
         status = semctl(sempoolid,i, SETVAL,semctlops);
         if (status == -1) {
            perror ("sempoolid semctl failed");
            return -1;
         }
      }
   } else {
      // noop, print info
      return svipc_sem_info(key,1);
   }
   
   return 0;
}

//---------------------------------------------------------------
// svipc_sem_cleanup
//---------------------------------------------------------------
int svipc_sem_cleanup(long key) {
   int sempoolid;
   
   Debug(5, "svipc_sem_cleanup\n");
   
   sempoolid = semget(key,0,0666);
   if (sempoolid == -1) {
      perror ("sempoolid semget failed");
      return -1;
   }
   
   int status = semctl(sempoolid,IPC_RMID,0);
   if (status == -1) {
      perror ("shmctl IPC_RMID failed");
      return -1;
   }
   
   return 0;
}

//---------------------------------------------------------------
// svipc_sem_info
//---------------------------------------------------------------
int svipc_sem_info(long key, long details) {
   int sempoolid, i, status;
   
   Debug(5, "svipc_sem_info %lx\n",key);

   sempoolid = semget(key,0,0666);
   if (sempoolid == -1) {
      perror ("sempoolid semget failed");
      return -1;
   }
   
   union semun semctlops;
   struct semid_ds stat;
   semctlops.buf = &stat;
   status = semctl(sempoolid,0, IPC_STAT,semctlops);
   if (status == -1) {
      perror ("semctl IPC_STAT failed");
      return -1;
   }
   
   if (details) {
      fprintf (stderr, "SemPool key: 0x%lx id: %d\n", key,sempoolid);
      fprintf (stderr, "No. of semaphores in set: %ld\n", stat.sem_nsems);
      fprintf (stderr, "Last semop time:  %s", ctime(&stat.sem_otime));
      fprintf (stderr, "Last change time: %s", ctime(&stat.sem_ctime));
   }
   
   unsigned short *pvals = (unsigned short *) malloc(stat.sem_nsems * sizeof(unsigned short));
   semctlops.array = pvals;
   status = semctl(sempoolid,0, GETALL,semctlops);
   
   fprintf (stderr, "#id          used? val\n");
   fprintf (stderr, "----------------------\n");
   for (i=0;i<stat.sem_nsems;i++) {
      fprintf (stderr, "[%d]           %s  %2d\n",i,pvals[i]?"Free":"Used",pvals[i]);
   }
   
   free(pvals);

   return 0;
}

//---------------------------------------------------------------
// svipc_semtake
//---------------------------------------------------------------
int svipc_semtake(long key,long id,float wait) {
   int sempoolid, status;
   
   Debug(5, "svipc_semtake %f\n", wait);

   struct timespec timeout, *pto=NULL;
   if ( wait >= 0.0 ) {
      timeout.tv_sec = (__time_t) wait;
      timeout.tv_nsec = (long int) ((wait-timeout.tv_sec)*1e9) ;
      pto = &timeout;
   }
   
   
   sempoolid = semget(key,0,0666);
   if (sempoolid == -1) {
      perror ("sempoolid semget failed");
      return -1;
   }

   // take the semaphore
   struct sembuf sops;
   sops.sem_num=id;
   sops.sem_op=-1; // 
   sops.sem_flg=0; // fixme - undo if interrupted?
   
   status = semtimedop(sempoolid,&sops,1,pto);
   if (status == -1) {
      if (errno != EAGAIN) perror ("semop failed");
      return -1;
   }
   
   return 0;
}

//---------------------------------------------------------------
// svipc_semgive
//---------------------------------------------------------------
int svipc_semgive(long key,long id) {
   int sempoolid;

   Debug(5, "svipc_semgive\n");

   sempoolid = semget(key,0,0666);
   if (sempoolid == -1) {
      perror ("sempoolid semget failed");
      return -1;
   }
   
   // unlock the slot
   struct sembuf sops;
   sops.sem_num=id;
   sops.sem_op=1;
   sops.sem_flg=0;

   int status = semop(sempoolid,&sops,1);
   if (status == -1) {
      perror ("semop failed");
      return -1;
   }
   return 0;
   
}