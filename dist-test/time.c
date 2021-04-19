#ifdef CS333_P2

#include "types.h"
#include "user.h"
#include "uproc.h"

int 
main(int argc, char *argv[])
{
	if(argc == 0){
		printf(1, "No args. Fail");
		exit();
	}

	int start = uptime(); //capturing current time
	int end;
	int pid = fork(); //fork creates duplicate process

	++argv;
	if(pid == 0){ //we are in the child
		exec(argv[0], argv); //copies context of existing child, pass in args, 
	}
	else if(pid < 0){
		exit();
	}
	else{
		//parent
		wait();
		end =	uptime(); 
		int final = end-start;
		printf(1, "%s ran in %d.%d seconds\n", argv[0], final/1000, final%1000);
	}

	exit();
}


//if argc is 1 then command was null
//else get input from argv

//local variables 
// start end elapsed
//uptime();
/*

	if(argc == 1){
		end = uptime();
		int final = end-start;
		printf(1, "(null) ran in %d.%d seconds\n", final/1000, final%1000);
	}
*/


#endif //CS333_P2
