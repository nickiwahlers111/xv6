#ifdef CS333_P2

#include "types.h"
#include "user.h"
#include "uproc.h"
//#include "defs.h"

/*
	 MAX SIZE DEFINITIONS
	 uncomment line for program requirements
 */
//#define MAX_SIZE 1
//#define MAX_SIZE 16
#define MAX_SIZE 64
//#define MAX_SIZE 72

#ifdef CS333_P4
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#else
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\n"
#endif


int 
main(int argc, char *argv[])
{
	int max=MAX_SIZE;
	struct uproc* u;

	u = malloc(max*sizeof(struct uproc));

	//u = malloc(max*sizeof(*u));

	int num =	getprocs(max, u);
	if(num < 0){
		printf(2, "Error Retrieving Processes");
		exit();
	}

	printf(1,HEADER);	
	for(int i = 0; i < num; ++i){	
		//PID NAME UID GID PPID ELAPSED CPU STATE SIZE PCs

		int MAXNAME = 12;
		int len = strlen(u[i].name);
		if(len >MAXNAME){
			u[i].name[MAXNAME] = '\0';
			len = MAXNAME;
		}

		printf(1, "%d\t%s", u[i].pid, u[i].name);
		for(int i = len; i < MAXNAME; i++) printf(1," ");

		printf(1, " %d\t\t%d\t%d\t", u[i].uid, u[i].gid, u[i].ppid);
#ifdef CS333_P4
		printf(1, "%d\t", u[i].priority);
#endif


		//ELAPSED
		/*
		int dec = u[i].elapsed_ticks % 1000;
		int ones = (u[i].elapsed_ticks-dec) /1000;
		printf(1,"%d.%d\t",ones,dec );
		*/

		int elapsed_time = u[i].elapsed_ticks;
		int sec1 = elapsed_time /1000;
		int tens1 = (elapsed_time %= 1000) /100;
		int hunds1 = (elapsed_time %= 100)/10;
		int thous1 = elapsed_time %= 10;

		printf(1,"%d.%d%d%d\t", sec1, tens1, hunds1, thous1);

		//CPU

		int cpu_time = u[i].CPU_total_ticks;
		int sec = cpu_time /1000;
		int tens = (cpu_time %= 1000) / 100;
		int hunds = (cpu_time %= 100) /10;
		int thous = cpu_time %= 10;
		printf(1,"%d.%d%d%d\t", sec, tens, hunds, thous);

		//STATE and SIZE
		printf(1,"%s\t%d\n",u[i].state ,u[i].size);
	}
	if(u){
		free(u);
	}
	exit();
}
#endif //CS333_P2
