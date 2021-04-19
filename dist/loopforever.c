//MARKS

#include "types.h"
#include "user.h"
#include "pdx.h"

int
main(int argc, char *argv[])
{
  int pid, max;
	int pid1, pid2;
  unsigned long x = 0;

  if (argc == 1) {
    printf(2, "Enter number of processes to create\n");
    exit();
  }

  max = atoi(argv[1]);
	pid1 = atoi(argv[2]); //PID to set to MINPRIO
	pid2 = atoi(argv[3]);	//PID to set to MAXPRIO


  for (int i=0; i<max; i++) {
    sleep(5*TPS);  // pause before each child starts
    pid = fork();
    if (pid < 0) {
      printf(2, "fork failed!\n");
      exit();
    }

    if (pid == 0) { // child
      sleep(getpid()*TPS); // stagger start
      do {
        x += 1;
      } while (1);
      printf(1, "Child %d exiting\n", getpid());
      exit();
    }
  }

//ADDED TEST SECTION 5 TESTS
	printf(1, "SECTION 5 TESTS START\n\n");
  printf(1, "Setting PID %d to priority PRIO_MIN.\n", pid1);
  if(setpriority(pid1, MINPRIO) < 0)
    printf(1, "Error setting priority.\n");

  sleep(5000);

  printf(1, "Setting PID %d to priority PRIO_MAX.\n", pid2);
  if(setpriority(pid2, MAXPRIO) < 0)
    printf(1, "Error setting priority.\n");

  sleep(5000);
	printf(1, "Setting PID %d to priority 1000 (should fail).\n", pid1);
  if(setpriority(pid1, 1000) < 0)
    printf(1, "Error setting priority.\n");
	sleep(5000);

	printf(1, "Setting PID 4 to SAME PRIORITY\n");
	int prio = getpriority(4);
	if(setpriority(4, prio) <0){
    printf(1, "Error setting priority.\n");
	}
	printf(1, "PID 4 set to priority %d\n", prio);
	sleep(5000);
/*
	printf(1, "getting priority for PID 10,11,12\n");
	
	printf(1, "PID 10: %d\n", getpriority(10));
	printf(1, "PID 11: %d\n", getpriority(11));
	printf(1, "PID 12: %d\n", getpriority(12));

	printf(1, "\n\nSECTION 5 TESTS END SLEEPING NOW\n");
*/
/*
	printf(1, "Setting current pid to priority 0");
	pid = getpid();
	setpriority(pid, 0);
	*/

  sleep(5000);
	

	//END TEST SECTION 5 TESTS


  pid = fork();
  if (pid == 0) {
    sleep(20);
    do {
      x = x+1;
    } while (1);
  }

  sleep(15*TPS);
  wait();
  printf(1, "Parent exiting\n");
  exit();
}



