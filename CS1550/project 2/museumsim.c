#include <sys/mman.h>
#include <condvar.c>
#include <condvar.h>

//integers shared across processes
int *visitorsInMuseum;
int *guidesInMuseum;

//locks shared across processes
struct cs1550_lock *lock;

//CVs shared
struct cs1550_condition *visitorGO, *guideGO, *guideEXIT;

  
//c1: visitorArrives()
//arrive and wait (if necessary) until the number of visitors inside is < 10x the number of guides inside
void visitorArrives() {
  cs1550_acquire(lock);
  if(*visitorsInMuseum < 10 * *guidesInMuseum) {
    //space is available inside, go right in
    cs1550_release(lock);
    return;
  }

  while(*visitorsInMuseum >= 10 * *guidesInMuseum) {
    if(guidesInMuseum < 2) {
      cs1550_signal(guideGO);
    }
    cs1550_wait(visitorGO);
  }

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
  *visitorsInMuseum--; //decrement num visitors inside
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
  cs1550_signal(visitorGO);
  do {
    //should always go to sleep until signalled by visitor waiting outside
    cs1550_wait(guideGO);
  } while(guidesInside >= 2);
  *guidesInMuseum++;
  cs1550_signal(visitorGO);
  cs1550_release(lock);
  return;
}

//c5: openMuseum()
//simply increment the number of guides in the museum
void openMuseum() {
  cs1550_acquire(lock);
  *guidesInMuseum++;
  cs1550_release(lock);
}

//c6: tourguideLeaves()
//wait until no more visitors are in the museum, then leave
void tourguideLeaves() {
  cs1550_acquire(lock);
  do {
    cs1550_wait(guideEXIT);
  } while(*visitorsInMuseum > 0);
  *guidesInMuseum--;
  cs1550_release(lock);
}


int main(int argc, const char* argv[]) {
  int m = 0;
  int k = 0;
  int pv = 0;
  int dv = 1;
  int sv = 0;
  int pg = 0;
  int dg = 1;
  int sg = 0;


  //read inputs
	int i = 1;
	while(argc >= i + 2) {
		if(argv[i] == "-m") {
			m = atoi(argv[i+1]);
		} else if(argv[i] == "-k") {
			k = atoi(argv[i+1]);
		} else if(argv[i] == "-pv") {
			pv = atoi(argv[i+1]);
		} else if(argv[i] == "-dv") {
			dv = atoi(argv[i+1]);
		} else if(argv[i] == "-sv") {
			sv = atoi(argv[i+1]);
		} else if(argv[i] == "-pg") {
			pg = atoi(argv[i+1]);
		} else if(argv[i] == "-dg") {
			dg = atoi(argv[i+1]);
		} else if(argv[i] == "-sg") {
			sg = atoi(argv[i+1]);
		}
		i += 2;
	}

  //ids for each visitor and guide (don't need to be locked)
  int visitorNumber = 0;
  int guideNumber = 0;

  //initialize shared ints
  void *ptr = mmap(NULL, 2*sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);
  visitorsInMuseum = (int*) ptr;
  guidesInMuseum = visitorsInMuseum + 1;
  *visitorsInMuseum = *guidesInMuseum = 0;
  

  //initialize the locks for the 2 shared ints
  void *ptr = mmap(NULL, sizeof(struct cs1550_lock), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);
  lock = (struct cs1550_lock *) ptr;
  cs1550_init_lock(lock, "lock");

  //initialize 3 shared condition variables
  void *ptr = mmap(NULL, 3*sizeof(struct cs1550_condition), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0,0);
  visitorGO = (struct cs1550_condition *) ptr;
  guideGO = visitorGO + 1;
  guideEXIT = visitorGO + 2;
  cs1550_init_condition(visitorGO, lock, "visitorGO");
  cs1550_init_condition(guideGO, lock, "guideGO");
  cs1550_init_condition(guideEXIT, lock, "guideEXIT");



  //initialize shared struct timeval startTime just before forking
  struct timeval startTime = 0;
  gettimeofday(&startTime, NULL);

  pid = fork();
  if(pid == 0) {
    //visitor generator process

    //seed random generator for visior arrivals
    srand(sv);

    for(i = 0; i < m; i++) {
      pid = fork();
      if(pid == 0) {
        //visitor process
        int id = visitorNumber++;

        //TODO add logic for child process
        struct timeval currTime = 0;
        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d arrives at time %d.", id, currTime - startTime);

        visitorArrives();

        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d tours the museum at time %d.", id, currTime - startTime);

        tourMuseum();
        
        visitorLeaves();
        
        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d leaves the museum at time %d.", id, currTime - startTime);


        exit();
      } else {
        //continue with visitor generation

        int value = rand() %  100 + 1;
        if(value > pv) { //check if there's a delay before next visitor
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

    //TODO implement logic for tour guide generator and processes
    for(i = 0; i < k; i++) {
      pid = fork();
      if(pid == 0) {
        //guide process
        int id = guideNumber++;

        //TODO add logic for guide process
        struct timeval currTime = 0;
        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d arrives at time %d.", id, currTime - startTime);
        
        tourguideArrives();

        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d opens the museum for tours at time %d.", id, currTime - startTime);

        openMuseum();

        tourguideLeaves();

        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d leaves the museum to time %d.", id, currTime - startTime);
        
        exit();
      } else {
        //continue with guide generation

        int value = rand() % 100;
        if(value > pg) { //check if there's a delay before next guide
          //delay
          sleep(dg);
        }
      }
    }
    wait();
  }
  //close condition vars
  cs1550_close_condition(visitorGO, lock, "visitorGO");
  cs1550_close_condition(guideGO, lock, "guideGO");
  cs1550_close_condition(guideEXIT, lock, "guideEXIT");

  //close lock
  cs1550_close_lock(lock);
}
