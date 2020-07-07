#include <sys/mman.h>
#include <sys/time.h>
#include <stdio.h>
#include "condvar.h"

//Adam Karl
//Note: 4 shared ints seems like overkill, but it was very valuable during the testing phase
//to see EXACTLY where the program was messing up by printing all 4 heuristics

//integers shared across processes
int *visitorsInMuseum, *guidesInMuseum, *visitorsOutside, *visitorOpenings;

//locks shared across processes
struct cs1550_lock *lock;

//CVs shared
struct cs1550_condition *visitorGO, *guideGO, *guideEXIT;

  
//c1: visitorArrives()
//arrive and wait (if necessary) until the number of visitors inside is < 10x the number of guides inside
void visitorArrives() {
  cs1550_acquire(lock);
  if(*visitorOpenings > 0) {
    *visitorsInMuseum += 1;
    *visitorOpenings -= 1;
    //space is available inside, go right in
    cs1550_release(lock);
    return;
  }

  *visitorsOutside += 1;
  while(*visitorOpenings < 1) { //cases to wait: 0v0g, 10v1g, 20v2g
    if(*guidesInMuseum < 2) {
      cs1550_signal(guideGO);
    }
    cs1550_wait(visitorGO);
  }
  *visitorsOutside -= 1;
  *visitorOpenings -= 1;

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
  if(*visitorsInMuseum == 0 || (*visitorOpenings + *visitorsInMuseum == 10 && *guidesInMuseum == 2)) {
    //signal guide to leave if conditions are appropriate
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
    cs1550_wait(guideGO);
  }
  *guidesInMuseum += 1;
  *visitorOpenings += 10;
  cs1550_release(lock);
  return;
}

//c5: openMuseum()
//simply increment the number of guides in the museum and signal any waiting visitors
void openMuseum() {
  cs1550_acquire(lock);
  cs1550_signal(visitorGO);
  cs1550_release(lock);
}

//c6: tourguideLeaves()
//wait until no more visitors are in the museum, then leave
void tourguideLeaves() {
  cs1550_acquire(lock);
  do {
    cs1550_wait(guideEXIT); 
  } while ((*guidesInMuseum - 1) * 10 < *visitorsInMuseum);
  *guidesInMuseum -= 1;
  if(*guidesInMuseum == 0) {
    //museum is empty
    *visitorOpenings = 0;
  }
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

  //allocate memory for all shared cariables
  void *ptr = mmap(NULL, 4*sizeof(int) + sizeof(struct cs1550_lock) + 3*sizeof(struct cs1550_condition), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);

  //shared ints
  visitorsInMuseum = (int*) ptr;
  guidesInMuseum = visitorsInMuseum + 1;
  visitorsOutside = visitorsInMuseum + 2;
  visitorOpenings = visitorsInMuseum + 3;
  *visitorsInMuseum = *guidesInMuseum = *visitorsOutside = *visitorOpenings = 0;

  //shared lock
  lock = (struct cs1550_lock *) (visitorsInMuseum + 4);
  cs1550_init_lock(lock, "lock");

  //shared condition vars
  visitorGO = (struct cs1550_condition *) (lock + 1);
  guideGO = visitorGO + 1;
  guideEXIT = visitorGO + 2;
  cs1550_init_condition(visitorGO, lock, "visitorGO");
  cs1550_init_condition(guideGO, lock, "guideGO");
  cs1550_init_condition(guideEXIT, lock, "guideEXIT");


  //initialize shared struct timeval startTime just before forking
  struct timeval startTime, currTime;
  long int timeDiff;
  gettimeofday(&startTime, NULL);

  int pid = fork();
  if(pid == 0) {
    //visitor generator process

    //seed random generator for visior arrivals
    srand(sv);

    int v;
    for(v = 0; v < m; v += 1) {
      pid = fork();
      if(pid == 0) {
        //visitor process
        gettimeofday(&currTime, NULL);
        timeDiff = (long int)(currTime.tv_sec - startTime.tv_sec);

        fprintf(stderr, "Visitor %d arrives at time %ld.\n", v, timeDiff);

        visitorArrives();

        gettimeofday(&currTime, NULL);
        timeDiff = (long int)(currTime.tv_sec - startTime.tv_sec);
        fprintf(stderr, "Visitor %d tours the museum at time %ld.\n", v, timeDiff);

        tourMuseum();

        gettimeofday(&currTime, NULL);
        timeDiff = (long int)(currTime.tv_sec - startTime.tv_sec);
        fprintf(stderr, "Visitor %d leaves the museum at time %ld.\n", v, timeDiff);
        
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

    int g;

    for(g = 0; g < k; g += 1) {
      pid = fork();
      if(pid == 0) {
        //guide process

        gettimeofday(&currTime, NULL);
        timeDiff = (long int)(currTime.tv_sec - startTime.tv_sec);
        fprintf(stderr, "Tour guide %d arrives at time %ld.\n", g, timeDiff);
        
        tourguideArrives();

        gettimeofday(&currTime, NULL);
        timeDiff = (long int)(currTime.tv_sec - startTime.tv_sec);
        fprintf(stderr, "Tour guide %d opens the museum for tours at time %ld.\n", g, timeDiff);

        openMuseum();

        tourguideLeaves();

        gettimeofday(&currTime, NULL);
        timeDiff = (long int)(currTime.tv_sec - startTime.tv_sec);
        fprintf(stderr, "Tour guide %d leaves the museum at time %ld.\n", g, timeDiff);

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
  wait();
  return;
}
