//c1: visitorArrives()
void visitorArrives() {
  ;
}

//c2: tourMuseum()
//1 tour guide and at most 10 visitors
void tourMuseum() {
	sleep(2);
}

//c3: visitorLeaves()
void visitorLeaves() {
  ;
}

//c4: tourguideArrives()
//at most 2 tour guides in museum at a time
void tourguideArrives() {
  ;
}

//c5: openMuseum()
//tour guide cannot open museum without a visitor
void openMuseum() {
  ;
}

//c6: tourguideLeaves()
//leaves when no more visitors are in the museum
void tourguideLeaves() {
  ;
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

  int visitorNumber = 0;
  int guideNumber = 0;


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
        
        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Visitor %d leaves the museum at time %d.", id, currTime - startTime);

        visitorLeaves();

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
    exit();
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

        gettimeeofday(&currTime, NULL);
        fprintf(stderr, "Tour guide %d leaves the museum to time %d.", id, currTime - startTime);
        
        tourguideLeaves();

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
    exit();
  }
}
