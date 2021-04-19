#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#ifdef CS333_P2
#include "uproc.h"
#endif

static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runnable",
  [RUNNING]   "running",
  [ZOMBIE]    "zombie"
};

#ifdef CS333_P3
//record
// record with head and tail pointer for constant-time access to the beginning
// and end of a linked list of struct procs.  use with stateListAdd() and
// stateListRemove().
struct ptrs {
  struct proc* head;
  struct proc* tail;
};
#endif

#ifdef CS333_P3
#define statecount NELEM(states)
#endif //CS333_P3


static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
	#ifdef CS333_P3
	struct ptrs list[statecount];
	#endif //CS333_P3

	#ifdef CS333_P4
	struct ptrs ready[MAXPRIO+1];
	uint PromoteAtTime;
	#endif
} ptable;

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc*, enum procstate, const char *, int);
#ifdef DEBUG
static void checkProcs(const char *, const char *, int);
#endif
#endif // CS333_P3
#ifdef CS333_P4
static void printReadyLists();
static void printReadyList(struct proc *, int);
#endif // CS333_P4

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
	
#ifdef CS333_P3
	//gitcommit
	p = ptable.list[UNUSED].head;
	if(!p){
		release(&ptable.lock);
		return 0;
	}
#else

  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
#endif

#ifdef CS333_P3
	if(stateListRemove(&ptable.list[UNUSED], p) == -1){
		panic("Failed to remove from UNUSED list in allocproc()");
	}
	assertState(p, UNUSED, __FUNCTION__, __LINE__);
#endif //CS333_P3
  p->state = EMBRYO;
#ifdef CS333_P3
	stateListAdd(&ptable.list[EMBRYO], p);
#endif
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
#ifdef CS333_P3
	if(stateListRemove(&ptable.list[EMBRYO], p) == -1){
		panic("Failed to remove from EMRYO list after kernel stack allocation failure in allocproc()");
	}
	assertState(p, EMBRYO, __FUNCTION__, __LINE__);
#endif
    p->state = UNUSED;
#ifdef CS333_P3
	stateListAdd(&ptable.list[UNUSED], p);
#endif
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

#ifdef CS333_P1
  p->start_ticks = ticks; //initialize the start time
#endif //CS333_P1

#ifdef CS333_P2
	p->cpu_ticks_total = 0;
	p->cpu_ticks_in = 0;
#endif //CS333_P2

  return p;
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
#ifdef CS333_P3
	acquire(&ptable.lock);
	initProcessLists();
	initFreeList();
	release(&ptable.lock);
#endif

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
#ifdef CS333_P3
	if(stateListRemove(&ptable.list[EMBRYO], p) == -1){
		panic("failed to remove from EMRBYO list after successfgul allocation in userinit()");
	}
	assertState(p, EMBRYO, __FUNCTION__, __LINE__);
#endif
  p->state = RUNNABLE;

#ifdef CS333_P4
	p->priority = MAXPRIO;
	p->budget = DEFAULT_BUDGET;
	stateListAdd(&ptable.ready[MAXPRIO], p);
#elif CS333_P3 //CS333_P4
	stateListAdd(&ptable.list[RUNNABLE], p);
#endif //CS333_P3
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
#ifdef CS333_P3
	acquire(&ptable.lock);
	if(stateListRemove(&ptable.list[EMBRYO], np) == -1){
		panic("failed to remove from EMBRYO list in fork() after page directory allocation failure");
	}
	assertState(np, EMBRYO, __FUNCTION__ , __LINE__);
#endif
    np->state = UNUSED;
#ifdef CS333_P3
	stateListAdd(&ptable.list[UNUSED], np);
#endif
		release(&ptable.lock);
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;


  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
	#ifdef CS333_P2
	 np->uid = curproc->uid;
	 np->gid = curproc->gid;
	#endif

  acquire(&ptable.lock);
#ifdef CS333_P3
	if(stateListRemove(&ptable.list[EMBRYO], np) == 1){
		panic("Failed  to remove from EMBRYO on successful fork");
	}
	assertState(np, EMBRYO, __FUNCTION__, __LINE__);
#endif
  np->state = RUNNABLE;
#ifdef CS333_P4
	np -> priority = MAXPRIO;
	np ->budget = DEFAULT_BUDGET;
	stateListAdd(&ptable.ready[MAXPRIO], np);
#elif CS333_P4
	stateListAdd(&ptable.list[RUNNABLE], np);
#endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3
void //P3 COPY
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
	p = ptable.list[EMBRYO].head;
	while(p){
		assertState(p, EMBRYO, __FUNCTION__, __LINE__);
		if(p->parent == curproc){
			p->parent = initproc;	
		}
		p = p-> next;
	}

	p = ptable.list[SLEEPING].head;
	while(p){
		assertState(p, SLEEPING, __FUNCTION__, __LINE__);
		if(p->parent == curproc){
			p->parent = initproc;
		}
		p = p->next;
	}
#ifdef CS333_P4
	for(int i = MAXPRIO; i <= MINPRIO; i++){
		p = ptable.ready[i].head;
		while(p){
			if(p->parent == curproc){
				p -> parent = initproc;
			}
			p = p->next;
		}
	}
#elif CS333_P3
	p = ptable.list[RUNNABLE].head;
	while(p){
		assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
		if(p->parent == curproc){
			p -> parent = initproc;
		}
		p = p -> next;
	}
#endif
	p = ptable.list[RUNNING].head;
	while(p){
		assertState(p, RUNNING, __FUNCTION__, __LINE__);
		if(p->parent == curproc){
			p->parent = initproc;
		}
		p = p-> next;
	}

	p= ptable.list[ZOMBIE].head;
	while(p){
		assertState(p, ZOMBIE, __FUNCTION__, __LINE__);
		if(p -> parent == curproc){
			p-> parent = initproc;
			wakeup1(initproc);
		}
		p = p-> next;
	}

  // Jump into the scheduler, never to return.
	if(stateListRemove(&ptable.list[RUNNING], curproc) == -1){
		panic("Failed to remove from RUNNING in exit()");
	}
	assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = ZOMBIE;
	stateListAdd(&ptable.list[ZOMBIE], curproc);
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#else
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif //CS333P3


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3 
int //P3 COPY
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;

		p = ptable.list[EMBRYO].head;
		while(p){
			assertState(p, EMBRYO, __FUNCTION__, __LINE__);
			if(p-> parent == curproc){
				havekids = 1;
			}
			p = p-> next;
		}
#ifdef CS333_P4
	for(int i = MAXPRIO; i >=MINPRIO; --i){
		p = ptable.ready[i].head;
		while(p){
			assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
			if(p ->parent == curproc){
				havekids = 1;
			}
			p = p->next;
		}
	}
#elif CS333_P3
		p = ptable.list[RUNNABLE].head;
		while(p){
			assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
			if(p->parent == curproc){
				havekids = 1;
			}
			p = p-> next;
		}
#endif
		p = ptable.list[RUNNING].head;
		while(p){
			assertState(p, RUNNING, __FUNCTION__, __LINE__);
			if(p->parent == curproc){
				havekids = 1;
			}
			p = p-> next;
		}
		p = ptable.list[SLEEPING].head;
		while(p){
			assertState(p, SLEEPING, __FUNCTION__, __LINE__);
			if(p->parent == curproc){
				havekids = 1;
			}
			p = p-> next;
		}

		p = ptable.list[ZOMBIE].head;
		while(p){
			if(p->parent == curproc){
				havekids = 1;
				if(stateListRemove(&ptable.list[ZOMBIE], p) == -1){
					panic("Failed to remove from ZOMBIE list in wait");
				}
				assertState(p, ZOMBIE, __FUNCTION__, __LINE__);
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
				stateListAdd(&ptable.list[UNUSED], p);
				release(&ptable.lock);
				return pid;
				}
				p = p-> next;
			}
		
    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

#endif

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

#ifdef CS333_P3
void //P3 COPY
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
	int i;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
#ifdef CS333_P4
		if(ticks >= ptable.PromoteAtTime){
			for(i = MAXPRIO -1; i >= MINPRIO; --i){
				p = ptable.ready[i].head;
				while(p){
					if(stateListRemove(&ptable.ready[i], p) == -1){
						panic("failed to remove process. we will run from runnable list in scheduler()");
					}
					assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
					p ->priority++;
					p->budget = DEFAULT_BUDGET;
					stateListAdd(&ptable.ready[p->priority], p);
					p = ptable.ready[i].head;
				}
			}

			p = ptable.list[SLEEPING].head;
			while(p){
				if(p->priority < MAXPRIO){
					p->priority++;
					p->budget = DEFAULT_BUDGET;
				}
				p = p->next;
			}

			p = ptable.list[RUNNING].head;
			while(p){
				if(p->priority <MAXPRIO){
					p -> priority++;
					p->budget = DEFAULT_BUDGET;
				}
				p = p->next;
			}
			ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;

			
		}


		for(i = MAXPRIO; i >= MINPRIO; --i){
		//p = ptable.list[RUNNABLE].head;
			p = ptable.ready[i].head;
      if(p){
				// switch to chosen process.  it is the process's job
				// to release ptable.lock and then reacquire it
				// before jumping back to us.

			if(stateListRemove(&ptable.ready[i], p) == -1){
				panic("failed to remove process we will run from runnable list in scheduler()");
				}
			assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
#ifdef pdx_xv6
      idle = 0;  // not idle this timeslice
#endif // pdx_xv6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
			stateListAdd(&ptable.list[RUNNING], p);
//#ifdef CS333_P2
			p->cpu_ticks_in = ticks;
//#endif //CS333_P2
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
			break;
			}
    }
		
#endif //CS333_P4
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
#ifdef CS333_P2
			p->cpu_ticks_in = ticks;
#endif //CS333_P2
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif //CS333_P3

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
#ifdef CS333_P2
	p->cpu_ticks_total += ticks-p->cpu_ticks_in;
#endif
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifdef CS333_P3
void //P3 COPY
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
	if(stateListRemove(&ptable.list[RUNNING], curproc) == 1){
		panic("failed to remove from RUNNING list in yield()");
	}
	assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = RUNNABLE;
#ifdef CS333_P4
	curproc->budget -= (ticks-curproc->cpu_ticks_in);
	if(curproc->budget <= 0){
		if(curproc->priority > MINPRIO){
			curproc ->priority -= 1;
		}
		curproc->budget = DEFAULT_BUDGET;
	}
	stateListAdd(&ptable.ready[curproc->priority], curproc);
#else
	stateListAdd(&ptable.list[RUNNABLE],curproc);
#endif
  sched();
  release(&ptable.lock);
}

#else
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

#endif

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
#ifdef CS333_P3

void //P3 COPY
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }

	if(stateListRemove(&ptable.list[RUNNING], p) == -1){
		panic("failed to remove process we will run from RUNNING list in sleep()");
	}
	assertState(p, RUNNING, __FUNCTION__, __LINE__);
	
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
#ifdef CS333_P4
	p->budget -= (ticks - p->cpu_ticks_in);
	if(p ->budget <=0){
		if(p->priority >MINPRIO){
			p->priority -= 1;
		}
		p ->budget = DEFAULT_BUDGET;
	}
#endif //CS333_P4
	stateListAdd(&ptable.list[SLEEPING], p);
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#else
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#endif
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P3
static void //P3 copy
wakeup1(void *chan) 
{
  struct proc *p;
	struct proc *h;
	p = ptable.list[SLEEPING].head;
	while(p){
		if(p->chan == chan){
			h = p->next;
			if(stateListRemove(&ptable.list[SLEEPING], p) == -1){
				panic("fail to remove from SLEEPING list in wakeup1()");
			}
			//p = ptable.list[SLEEPING].head;
			assertState(p, SLEEPING, __FUNCTION__, __LINE__);
			p->state = RUNNABLE;
#ifdef CS333_P4
			stateListAdd(&ptable.ready[p->priority], p);
#endif
			p = h;
		}
		else
			p = p-> next;
	}
}

#else
static void
wakeup1(void *chan)
{
	struct proc *p;

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if(p->state == SLEEPING && p->chan == chan)
			p->state = RUNNABLE;
}
#endif

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
	acquire(&ptable.lock);
	wakeup1(chan);
	release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
#ifdef CS333_P3
int //P3 Copy
kill(int pid)
{
	struct proc *p;

	acquire(&ptable.lock);
	
	p = ptable.list[EMBRYO].head;
	while(p){
		if(p->pid == pid){
			p -> killed = 1;
			release(&ptable.lock);
			return 0;
		}
		p = p-> next;
	}
#ifdef CS333_P4
	for(int i = MAXPRIO; i >= MINPRIO; --i){
		p = ptable.ready[i].head;
		while(p){
			if(p->pid == pid){
				p->killed = 1;
				release(&ptable.lock);
				return 0;
			}
			p = p -> next;
		}
	}
#else
	p = ptable.list[RUNNABLE].head;
	while(p){
		if(p->pid == pid){
			p->killed = 1;
			release(&ptable.lock);
			return 0;
		}
		p = p -> next;
	}
#endif

	p = ptable.list[RUNNING].head;
	while(p){
		if(p->pid == pid){
			p -> killed = 1;
			release(&ptable.lock);
			return 0;
		}
		p = p-> next;
	}

	p = ptable.list[ZOMBIE].head;
	while(p){
		if(p->pid == pid){
			p -> killed = 1;
			release(&ptable.lock);
			return 0;
		}
		p = p-> next;
	}

	p = ptable.list[SLEEPING].head;
	while(p){
		if(p->pid == pid){
			p->killed = 1;
			if(stateListRemove(&ptable.list[SLEEPING], p) == -1){
				panic("Failed to remove from SLEEPING list in kill()");
			}
			assertState(p, SLEEPING, __FUNCTION__, __LINE__);
			p->state = RUNNABLE;
#ifdef CS333_P4
			stateListAdd(&ptable.ready[p->priority], p);
#else
			stateListAdd(&ptable.list[RUNNABLE], p);
#endif
			release(&ptable.lock);
			return 0;
		}
		p = p->next;
	}

	release(&ptable.lock);
	return -1;

/*
for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			p->killed = 1;
			// Wake process from sleep if necessary.
			if(p->state == SLEEPING){
				//think carefully: could there be further SLEEPING processes
				//that we need to wake up?	 if we remove p from he SLEEPING list, 
				//what does p-> next become?
				if(stateListRemove(&ptable.list[SLEEPING], p) == -1){
					panic("Failed to remove from SLEEPING list in kill()");
				}
				assertState(p, SLEEPING, __FUNCTION__, __LINE__);
				p->state = RUNNABLE;
				stateListAdd(&ptable.list[RUNNABLE], p);
			}
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
	*/
}
#else
int
kill(int pid)
{
	struct proc *p;

	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			p->killed = 1;
			// Wake process from sleep if necessary.
			if(p->state == SLEEPING)
				p->state = RUNNABLE;
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}
#endif
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#if defined(CS333_P2)
void
procdumpP2P3P4(struct proc *p, char *state_string)
{
	//the following is a helpful hint from a slack post by Mark for the buffer
	int MAXNAME = 12;
	int len = strlen(p->name);
	if(len >MAXNAME){
		p->name[MAXNAME] = '\0';
		len = MAXNAME;
	}
	cprintf("%d\t%s", p->pid,p->name);
	for(int i = len; i<=MAXNAME; i++) cprintf(" ");

	//elapsed calculations

	int elapsed_time = ticks - p->start_ticks;
	int sec1 = elapsed_time /1000;
	int tens1 = (elapsed_time %= 1000) /100;
	int hunds1 = (elapsed_time %= 100)/10;
	int thous1 = elapsed_time %= 10;


	//CPU calculations	
	//cpu_ticks_total
	int cpu_time = p->cpu_ticks_total;
	int sec = cpu_time /1000;
	int tens = (cpu_time %= 1000) /100;
	int hunds = (cpu_time %= 100) /10;
	int thous = cpu_time %= 10;

	int ppid;
	if(!p->parent){
		ppid = p->pid;
	}
	else{
		ppid = p->parent->pid;
	}
#ifdef CS333_P4
//#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
	cprintf("%d\t\t%d\t%d\t%d\t%d.%d%d%d\t%d.%d%d%d\t%s\t%d\t",p->uid,p->gid,ppid,p->priority,sec1,tens1,hunds1,thous1,sec,tens,hunds,thous, state_string, p->sz);
	
#else
	cprintf("%d\t\t%d\t%d\t%d.%d%d%d\t%d.%d%d%d\t%s\t%d\t",p->uid,p->gid,ppid,sec1,tens1,hunds1,thous1,sec,tens,hunds,thous, state_string, p->sz);
#endif
	return;
}
#elif defined(CS333_P1)
void
procdumpP1(struct proc *p, char *state_string)
{
	uint end_ticks = ticks;
	uint final_time = end_ticks - p->start_ticks;
	uint dec = final_time % 1000;
	uint ones = (final_time - dec) / 1000;

	cprintf("%d\t%s\t     %d.%d\t%s\t%d\t", p->pid, p->name, ones, dec,state_string , p->sz);

	return;
}
#endif

void
procdump(void)
{
	int i;
	struct proc *p;
	char *state;
	uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName         Elapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

	cprintf(HEADER);  // not conditionally compiled as must work in all project states

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state == UNUSED)
			continue;
		if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
			state = states[p->state];
		else
			state = "???";

		// see TODOs above this function
		// P2 and P3 are identical and the P4 change is minor
#if defined(CS333_P2)
		procdumpP2P3P4(p, state);
#elif defined(CS333_P1)
		procdumpP1(p, state);
#else
		cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

		if(p->state == SLEEPING){
			getcallerpcs((uint*)p->context->ebp+2, pc);
			for(i=0; i<10 && pc[i] != 0; i++)
				cprintf(" %p", pc[i]);
		}
		cprintf("\n");
	}
#ifdef CS333_P1
	cprintf("$ ");  // simulate shell prompt
#endif // CS333_P1
}

#if defined(CS333_P3)
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
	if((*list).head == NULL){
		(*list).head = p;
		(*list).tail = p;
		p->next = NULL;
	} else{
		((*list).tail)->next = p;
		(*list).tail = ((*list).tail)->next;
		((*list).tail)->next = NULL;
	}
}
#endif

#if defined(CS333_P3)
static int
stateListRemove(struct ptrs* list, struct proc* p)
{
	if((*list).head == NULL || (*list).tail == NULL || p == NULL){
		return -1;
	}

	struct proc* current = (*list).head;
	struct proc* previous = 0;

	if(current == p){
		(*list).head = ((*list).head)->next;
		// prevent tail remaining assigned when we've removed the only item
		// on the list
		if((*list).tail == p){
			(*list).tail = NULL;
		}
		return 0;
	}

	while(current){
		if(current == p){
			break;
		}

		previous = current;
		current = current->next;
	}

	// Process not found. return error
	if(current == NULL){
		return -1;
	}

	// Process found.
	if(current == (*list).tail){
		(*list).tail = previous;
		((*list).tail)->next = NULL;
	} else{
		previous->next = current->next;
	}

	// Make sure p->next doesn't point into the list.
	p->next = NULL;

	return 0;
}
#endif

#if defined(CS333_P3)
static void
initProcessLists()
{
	int i;

	for (i = UNUSED; i <= ZOMBIE; i++) {
		ptable.list[i].head = NULL;
		ptable.list[i].tail = NULL;
	}
#if defined(CS333_P4)
	for (i = 0; i <= MAXPRIO; i++) {
		ptable.ready[i].head = NULL;
		ptable.ready[i].tail = NULL;
	}
#endif
}
#endif

#if defined(CS333_P3)
static void
initFreeList(void)
{
	struct proc* p;

	for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
		p->state = UNUSED;
		stateListAdd(&ptable.list[UNUSED], p);
	}
}
#endif

#if defined(CS333_P3)
// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char * func, int line)
{
	if (p->state == state)
		return;
	cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n",
			states[p->state], states[state], func, line);
	panic("Error: Process state incorrect in assertState()");
}
#endif

#if defined(CS333_P3)
// Project 3/4 control sequence support
void
printList(int state)
{
	int count = 0;
	const int PER_LINE = 15;  // per line max on print
	const int PER_LINE_Z = (PER_LINE/2);  // zombie list has more chars per entry on print
	struct proc *p;
	static char *stateNames[] = {  // note: sparse array
		[RUNNABLE]  "Runnable",
		[SLEEPING]  "Sleep",
		[RUNNING]   "Running",
		[ZOMBIE]    "Zombie"
	};


	if (state < UNUSED || state > ZOMBIE) {
		cprintf("Invalid control sequence\n");
		cprintf("$ ");  // simulate shell prompt
		return;
	}

	acquire(&ptable.lock);
#ifdef DEBUG
	checkProcs(__FILE__, __FUNCTION__, __LINE__);
#endif
#ifdef CS333_P4
	if (state == RUNNABLE) {
		printReadyLists();
		release(&ptable.lock);
		cprintf("$ ");  // simulate shell prompt
		return;
	}
#endif
	cprintf("\n%s List Processes:\n", stateNames[state]);
	p = ptable.list[state].head;
	while (p != NULL) {
		if (p->state != state) {  // sanity check
			cprintf("Error: PID %d on %s list but should be on %s\n",
					p->pid, states[p->state], states[state]);
			panic("Corrupted list\n");
		}
		if (state == ZOMBIE)
			cprintf("(%d, %d)", p->pid,
					(p->parent) ? p->parent->pid : p->pid);
		else
			cprintf("%d", p->pid);
		p = p->next;
		cprintf("%s", p ? " -> " : "\n");
		if (p && (++count) %
				((state == ZOMBIE) ? PER_LINE_Z : PER_LINE) == 0)
			cprintf("\n");
	}
	release(&ptable.lock);
	cprintf("$ ");  // simulate shell prompt
	return;
}

void
printFreeList(void)
{
	int count = 0;
	struct proc *p;

	acquire(&ptable.lock);
	p = ptable.list[UNUSED].head;
	while (p != NULL) {
		count++;
		p = p->next;
	}
	release(&ptable.lock);
	cprintf("\nFree List Size: %d processes\n", count);
	cprintf("$ ");  // simulate shell prompt
	return;
}

void
printListStats()
{
	int i, count, total = 0;
	struct proc *p;

	acquire(&ptable.lock);
	for (i=UNUSED; i<=ZOMBIE; i++) {
		count = 0;
		p = ptable.list[i].head;
		while (p != NULL) {
			count++;
			if(p->state != i) {
				cprintf("\nlist invariant failed: process %d has state %s but is on list %s\n",
						p->pid, states[p->state], states[i]);
			}
			p = p->next;
		}
		cprintf("\n%s list has ", states[i]);
		if (count < 10) cprintf(" ");  // line up columns. we know NPROC < 100
		cprintf("%d processes", count);
		total += count;
	}
	release(&ptable.lock);
	cprintf("\nTotal on lists is: %d. NPROC = %d. %s",
			total, NPROC, (total == NPROC) ? "Congratulations!" : "Bummer");
	cprintf("\n$ ");  // simulate shell prompt
	return;
}
#endif // CS333_P3

#ifdef CS333_P4
void
printReadyList(struct proc *p, int prio)
{
	const int PER_LINE = 15;  // per line max on print
	if (p == NULL) {
		cprintf("(NULL)\n");
		return;
	}
	int count = 0;
	do {
		if(p->priority != prio) {
			cprintf("\nlist invariant failed: process %d has prio %d but is on runnable list %d\n",
					p->pid, p->priority, prio);
		}

		cprintf("(%d,%d)", p->pid, p->budget);
		if(p -> next){
			p = p->next;
		}
		else{
			p = NULL;
		}

		cprintf("%s", p ? " -> " : "\n");
		if (p && (++count) % PER_LINE == 0)
			cprintf("\n");
	} while (p != NULL);
	return;
}

void
printReadyLists()
{
	struct proc *p;

	cprintf("Ready List Processes:\n");
	// this look must be changed based on MAX/MIN prio
	for (int i=MAXPRIO; i>= MINPRIO; i--) {
		p = ptable.ready[i].head;
		
		cprintf("Prio %d: ", i);
		if(!p){
			cprintf("(NULL)\n");
			continue;
		}
		
		if(p->state != RUNNABLE) {
			cprintf("\nlist invariant failed: process %d has state %s but is on ready list\n",
					p->pid, states[p->state]);
		}
		printReadyList(p, i);
	}
}

#endif // CS333_P4

#ifdef DEBUG
static int
procLookup(struct proc *p, struct proc *np)
{
	while (np != NULL) {
		if (np == p)
			return 1;
		np = np->next;
	}
	return 0;
}

static int
findProc(struct proc *p)
{
	for (int i=UNUSED; i<=ZOMBIE; i++)
		if (procLookup(p, ptable.list[i].head)   != 0) return 1;
	return 0; // not found
}

static void
checkProcs(const char *file, const char *func, int line)
{
	int found;
	struct proc *p;

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		found = findProc(p);
		if (found) continue;
		cprintf("checkprocs error. Called from %s, %s, @ %d\n", file, func, line);
		panic("Process array and lists inconsistent\n");
	}
}
#endif // DEBUG


#ifdef CS333_P2	
int
get_procs(uint max, struct uproc* u)
{
	struct proc *p; 
	int num = 0;

	acquire(&ptable.lock);

	for(p = ptable.proc; p < &ptable.proc[NPROC]&& num<max; p++){
		//check for active
		if(p-> state == UNUSED || p->state == EMBRYO ){
			continue;
		}
		else{
			u[num].pid = p->pid;
			u[num].uid = p->uid;
			u[num].gid = p->gid;
#ifdef CS333_P4
			u[num].priority = p->priority;
#endif

			if(p->parent == NULL){
				u[num].ppid = 1;
			}
			else{
				u[num].ppid = p->parent->pid;
			}

			u[num].elapsed_ticks = ticks - p->start_ticks;
			u[num].CPU_total_ticks = p->cpu_ticks_total;
			safestrcpy(u[num].state, states[p->state], sizeof(u[num].state));
			u[num].size = p->sz;
			safestrcpy(u[num].name, p->name, sizeof(u[num].name));
			++num; //update how many times we did this
		}
	}

	release(&ptable.lock);

	return num;
}
#endif

#ifdef CS333_P4
int
setpriority(int pid, int priority)
{
	struct proc* p;
	int i;
	if(priority <0 || priority > MAXPRIO){
		return -1;
	}

	if(pid < 0 || pid > 32767){
		return -1;
	}

	acquire(&ptable.lock);

	p = ptable.list[EMBRYO].head;
	while(p){
		if(p->pid == pid){
			p -> priority = priority;
			p -> budget = DEFAULT_BUDGET;
			release(&ptable.lock);
			return 0;
		}
		p = p-> next;
	}

	p = ptable.list[RUNNING].head;
	while(p){
		if(p->pid == pid){
			p->priority = priority;
			p->budget = DEFAULT_BUDGET;
			release(&ptable.lock);
			return 0;
		}
		p = p->next;
	}

	p = ptable.list[SLEEPING].head;
	while(p){
		if(p->pid == pid){
			p->priority = priority;
			p->budget = DEFAULT_BUDGET;
			release(&ptable.lock);
			return 0;
		}
		p = p-> next;
	}

	for(i = MAXPRIO; i>=MINPRIO; --i){
		p = ptable.ready[i].head;
		while(p){
			if(p->pid == pid){
				if(p->priority == priority){
					release(&ptable.lock);
					return 0;
				}
				if(stateListRemove(&ptable.ready[i], p)==-1){
					panic("Error in setpriority. not in ready list");
				}
				assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
				p -> priority = priority;
				p->budget = DEFAULT_BUDGET;
				stateListAdd(&ptable.ready[p->priority], p);
				release(&ptable.lock);
				return 0;
			}
			p = p->next;
		}
	}
	release(&ptable.lock);
	return -1;
}

int
getpriority(int pid)
{
	struct proc* p;
	int priority;

	acquire(&ptable.lock);

	p = ptable.list[EMBRYO].head;

	while(p){
		if(p->pid == pid){
			priority = p->priority;
			release(&ptable.lock);
			return priority;
		}
		p = p-> next;
	}
	
	p = ptable.list[RUNNING].head;
	while(p){
		if(p->pid == pid){
			priority = p->priority;
			release(&ptable.lock);
			return priority;
		}
		p = p->next;
	}

	p = ptable.list[SLEEPING].head;
	while(p){
		if(p->pid == pid){
			priority = p->priority;
			release(&ptable.lock);
			return priority;
		}
		p = p->next;
	}

	p = ptable.list[ZOMBIE].head;
	while(p){
		if(p->pid ==pid){
			priority = p->priority;
			release(&ptable.lock);
			return priority;
		}
		p = p->next;
	}
	
	for(int i = MAXPRIO; i >= MINPRIO; --i){
		p = ptable.ready[i].head;
		while(p){
			if(p->pid == pid){
				priority = p->priority;
				release(&ptable.lock);
				return priority;
			}
			p = p->next;
		}
	}
	release(&ptable.lock);
	return -1;
}
#endif


