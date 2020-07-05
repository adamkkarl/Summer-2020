int m = 0;
int k = 0;
int pv = 0;
int dv = 1;
int sv = 0;
int pg = 0;
int dg = 1;
int sg = 0;
struct timeval startTime = 0;

//a: visitor process
Visitor {
	visitorArrives();
	tourMuseum();
	visitorLeaves();
}

//b: tour guide process
Tourguide {
	tourguideArrives();
	openMuseum();
	tourguideLeaves();
}

//c1: visitorArrives()

//c2: tourMuseum()
//1 tour guide and at most 10 visitors
private void tourMuseum() {
	sleep(2);
}

//c3: visitorLeaves()

//c4: tourguideArrives()
//at most 2 tour guides in museum at a time

//c5: openMuseum()
//tour guide cannot open museum without a visitor

//c6: tourguideLeaves()
//leaves when no more visitors are in the museum

//d: process simulating tour guides' arrival


//e: process simulating visitor's arrival


int main(int argc, const char* argv[]) {
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

  //allocate 2 shared ints: visitorNumber, guideNumber
  void *ptr = mmap(NULL, 2*sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
  int *visitorNumber, *guideNumber;
  visitorNumber = (int *) ptr;
  guideNumber = visitorNumber + 1;
  startTime = visitorNumber + 2;
  //initialize
  *visitorNumber = *guideNumber = 0;

  //initialize shared struct timeval startTime just before forking
  gettimeofday(&startTime, NULL);

}
