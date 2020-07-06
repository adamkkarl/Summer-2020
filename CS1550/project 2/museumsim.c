#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <sys/resource.h>
#include "condvar.c"
#include "condvar.h"


//integers shared across processes
int *visitorsInMuseum, *guidesInMuseum, *visitorsOutside;

//locks shared across processes
struct cs1550_lock *lock;

//CVs shared
struct cs1550_condition *visitorGO, *guideGO, *guideEXIT;

  
//c1: visitorArrives()
//arrive and wait (if necessary) until the number of visitors inside is < 10x the number of guides inside
void visitorArrives() {
  cs1550_acquire(lock);
  if(*visitorsInMuseum < 10 * *guidesInMuseum) {
    *visitorsInMuseum += 1;
    //space is available inside, go right in
    cs1550_release(lock);
    return;
  }

  *visitorsOutside += 1;
  while(*visitorsInMuseum == 10 * *guidesInMuseum) { //cases to wait: 0v0g, 10v1g, 20v2g
//    fprintf(stderr, "%dv %dg %dw\n", *visitorsInMuseum, *guidesInMuseum, *visitorsOutside);
    if(*guidesInMuseum < 2) {
//      fprintf(stderr, "signaling guide\n");
      cs1550_signal(guideGO);
    }
//    fprintf(stderr, "visitor sleep\n");
    cs1550_wait(visitorGO);
  }
  *visitorsOutside -= 1;

  //free to go inside
  *visitorsInMuseum += 1;
  cs1550_signal(visitorGO); //in case multiple visitors were waiting on guide
  cs1550_release(lock);
  return;
}

//c2: tourMuseum()
//just sleep
void tourMuseum() {
  sleep(2);
  return;
}

//c3: visitorLeaves()
//decrement the number of visitors inside
void visitorLeaves() {
  cs1550_acquire(lock);
  *visitorsInMuseum -= 1; //decrement num visitors inside
  cs1550_signal(visitorGO);
  if(*visitorsInMuseum == 0) { //signal guide(s) to leave if no visitors left
    cs1550_signal(guideEXIT);
  }
  cs1550_release(lock);
  return;
}

//c4: tourguideArrives()
// arrive and wait(if necessary) until
// < 2 guides inside && visitor(s) waiting outside
void tourguideArrives() {
  cs1550_acquire(lock);
  while(*visitorsOutside == 0 || *guidesInMuseum > 1) {
//    fprintf(stderr, "guide sleeping\n");
    cs1550_wait(guideGO);
  }
  *guidesInMuseum += 1;
//  fprintf(stderr, "%d in\n", *guidesInMuseum);
  cs1550_release(lock);
  return;
}

//c5: openMuseum()
//simply increment the number of guides in the museum and signal any waiting visitors
void openMuseum() {
  cs1550_acquire(lock);
//  fprintf(stderr, "signalling visitor\n");
  cs1550_signal(visitorGO);
  cs1550_release(lock);
}

//c6: tourguideLeaves()
//wait until no more visitors are in the museum, then leave
void tourguideLeaves() {
  cs1550_acquire(lock);
  do {
    cs1550_wait(guideEXIT);
  } while(*visitorsInMuseum > 0);
  *guidesInMuseum -= 1;
  cs1550_release(lock);
}


int main(int argc, const char* argv[]) {
  int m = 0;
  int k = 0;
  int pv = 80;
  int dv = 2;
  int sv = 9999;
  int pg = 10;
  int dg = 5;
  int sg = 12345;

  //read inputs
  int i = 1;
  while(i < argc) {
    if(strcmp(argv[i], "-m") == 0) {
      m = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-k") == 0) {
      k = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-pv") == 0) {
      pv = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-dv") == 0) {
      dv = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-sv") == 0) {
      sv = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-pg") == 0) {
      pg = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-dg") == 0) {
      dg = atoi(argv[i+1]);
    } else if(strcmp(argv[i], "-sg") == 0) {
      sg = atoi(argv[i+1]);
    }
    i += 2;
  }

//  fprintf(stderr, "%d visitors, %d guides\n", m, k);

  //initialize shared ints
  void *ptr = mmap(NULL, 3*sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);
  visitorsInMuseum = (int*) ptr;
  guidesInMuseum = visitorsInMuseum + 1;
  visitorsOutside = visitorsInMuseum + 2;
  *visitorsInMuseum = *guidesInMuseum = *visitorsOutside = 0;
  

  //initialize the locks for the 2 shared ints
  ptr = mmap(NULL, sizeof(struct cs1550_lock), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);
  lock = (struct cs1550_lock *) ptr;
  cs1550_init_lock(lock, "lock");

  //initialize 3 shared condition variables
  ptr = mmap(NULL, 3*sizeof(struct cs1550_condition), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);
  visitorGO = (struct cs1550_condition *) ptr;
  guideGO = visitorGO + 1;
  guideEXIT = visitorGO + 2;
  cs1550_init_condition(visitorGO, lock, "visitorGO");
  cs1550_init_condition(guideGO, lock, "guideGO");
  cs1550_init_condition(guideEXIT, lock, "guideEXIT");

  int j;

  //initialize shared struct timeval startTime just before forking
  struct timeval startTime, currTime;
  gettimeofday(&startTime, NULL);

  int pid = fork();
  if(pid == 0) {
    //visitor generator process

    //seed random generator for visior arrivals
    srand(sv);
    int id;
    for(id = 0; id < m; id += 1) {
      pid = fork();
      if(pid == 0) {
        //visitor process
        gettimeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d arrives at time %d.\n", id, currTime.tv_sec - startTime.tv_sec);

        visitorArrives();

        gettimeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d tours the museum at time %d.\n", id, currTime.tv_sec - startTime.tv_sec);

        tourMuseum();

        gettimeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d leaves the museum at time %d.\n", id, currTime.tv_sec - startTime.tv_sec);
        
        visitorLeaves();

        return;
      } else {
        //continue with visitor generation

        int value = rand() %  100;
        if(value >= pv) { //check if there's a delay before next visitor
          //delay
          sleep(dv);
        }
      }
    }
    wait();
  } else {
    //guide generator process
    
    //seed random generator for guide arrivals
    srand(sg);

    int id;

    for(id = 0; id < k; id += 1) {
      pid = fork();
      if(pid == 0) {
        //guide process

        gettimeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d arrives at time %d.\n", id, currTime.tv_sec - startTime.tv_sec);
        
        tourguideArrives();

        gettimeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d opens the museum for tours at time %d.\n", id, currTime.tv_sec - startTime.tv_sec);

        openMuseum();

        tourguideLeaves();

        gettimeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d leaves the museum at time %d.\n", id, currTime.tv_sec - startTime.tv_sec);

        cs1550_acquire(lock);
        if(*guidesInMuseum == 0) {
          fprintf(stderr, "The museum is now empty.\n");
        }
        cs1550_release(lock);
        
        return;
      } else {
        //continue with guide generation

        int value = rand() % 100;
        if(value >= pg) { //check if there's a delay before next guide
          //delay
          sleep(dg);
        }
      }
    }
    wait();
  }
  return;
}
