#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
 
static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int threshold = 30;
void aging();
void push(int q, struct proc* p);
void pop(int q);

struct proc* head[5] = {0,0,0,0,0};
struct proc* tail[5] = {0,0,0,0,0};


uint ticks;

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
    if (cpus[i].apicid == apicid)
      return &cpus[i];
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

//PAGEBREAK: 32
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->n_run = 0;

  //time
  p->ctime = ticks;
  p->rtime = 0;
  p->queue = -1;
  p->wtime = 0;


  //pbs
  p->priority = 60;
  

  
  for(int i=0;i<5;i++)p->all_ticks[i] = -1;
  p->queue = -1;
  

  #ifdef MLFQ
  p->queue = 0;
  for(int i=0;i<5;i++)p->all_ticks[i] = 0;
  #endif

  cprintf("process %d has been created with time %d\n",p->pid,p->ctime);
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
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

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

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

  p->state = RUNNABLE;
  
  p->enter = ticks;

  #ifdef MLFQ
  push(p->queue,p);
  #endif

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
  int i, pid;
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
    np->state = UNUSED;
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

  acquire(&ptable.lock);
  np->queue = 0;
  np->state = RUNNABLE;
  np->enter = ticks;
  #ifdef MLFQ
  push(np->queue,np);
  #endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
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
  if(curproc->state == RUNNABLE)curproc->wtime += (ticks - curproc->enter);
  curproc->state = ZOMBIE;
  curproc->etime = ticks;

  cprintf("process %d has exited with end time%d\n",curproc->pid,curproc->etime);


  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
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


int waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
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


        //wtime and rtime values:
  
        *wtime = p->wtime;
        *rtime = p->rtime;


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

int set_priority(int new_priority,int pid)
{
  acquire(&ptable.lock);

  struct proc *p;
  int flag = 0;
  int old = -1;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid)
    {
      old = p->priority;
      p->priority = new_priority;
      flag = 1;
      break;
    }
  }

  release(&ptable.lock);
  if(flag && (p->priority < old))
  {
    yield();
  }

  return (old);
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

void scheduler(void)
{
	struct cpu *c = mycpu();
	c->proc = 0;
  struct proc *p = 0;

	for (;;)
	{

		// Enable interrupts on this processor.
		sti();

		// Loop over process table looking for process to run.
		acquire(&ptable.lock);

		#ifdef RR
		
			for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
			{
				
				if (p->state != RUNNABLE)
					continue;

				// Switch to chosen process.  It is the process's job
				// to release ptable.lock and then reacquire it
				// before jumping back to us.

				c->proc = p;
				switchuvm(p);
				p->state = RUNNING;
        p->wtime += (ticks-p->enter);
        p->n_run++;

				swtch(&(c->scheduler), p->context);
				switchkvm();

				// Process is  running for now.
				// It should have changed its p->state before coming back.
				c->proc = 0;
			}

		#else
		#ifdef FCFS
      
      struct proc *minP = 0;
    
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {

        if (p->state != RUNNABLE)
          continue;

        if (minP != 0){
            if(p->ctime < minP->ctime)
              minP = p;
        }
        else minP = p;
      }

      if (minP != 0 && minP->state == RUNNABLE)
      {
        
        p = minP;
        p->wtime += (ticks - p->enter);
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        p->wtime += (ticks - p->enter);
        p->n_run++;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }

    #else
    #ifdef PBS

      struct proc* minP = 0;
      for(p = ptable.proc;p < &ptable.proc[NPROC];p++)
      {
        if(p->state != RUNNABLE)continue;

        if(minP == 0)
        {
          minP = p;
        }
        else if(minP->priority > p->priority)
        {
          minP = p;
        }
      }

      if(minP == 0)
      {
        release(&ptable.lock);
        continue;
      }

      int check = 0;
      for(p = ptable.proc;p < &ptable.proc[NPROC];p++)
      {
        if(p->state != RUNNABLE)continue;

        for(struct proc*temp = ptable.proc; temp < &ptable.proc[NPROC];temp++ )
        {
          if(temp->state != RUNNABLE)continue;

          if(temp->priority < minP->priority)
          {
            check = 1;
          }
        }

        if(check)break;

        if(p->priority == minP->priority)
        {
          c->proc = p;
          switchuvm(p);
          p->state = RUNNING;
          p->wtime += (ticks - p->enter);
          p->n_run++;

          swtch(&(c->scheduler), p->context);
					switchkvm();
          c->proc = 0;
        }
      }
    
    #else
    #ifdef MLFQ

      aging();
      //struct proc *p = 0;

      for(int i=0;i<5;i++)
      {
        if(head[i] != 0)
        {
          p = head[i];
          pop(i);
          p->next = 0;
          break;
        }
      }

      if(p!=0 && p->state == RUNNABLE)
      {
        
        c->proc = p;
        p->wtime +=(ticks - p->enter);
      	
        switchuvm(p);
      	p->state = RUNNING;
        p->wtime += (ticks - p->enter);

      	p->n_run++;
        swtch(&c->scheduler, p->context);
      	switchkvm();
      	c->proc = 0;
      }

    #endif
		#endif
		#endif
		#endif

		release(&ptable.lock);
	}
}

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
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;

  myproc()->enter = ticks;
  #ifdef MLFQ
  push(myproc()->queue,myproc());
  #endif
  
  sched();
  release(&ptable.lock);
}

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
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  if(p->state == RUNNABLE)p->wtime += (ticks - p->enter);
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
    {  
      p->state = RUNNABLE;
      p->enter = ticks;

      #ifdef MLFQ
      push(p->queue,p);
      #endif
    }
}

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
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        p->enter = ticks;
    
        #ifdef MLFQ
        push(p->queue,p);
        #endif
      
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void get_state(char *string,struct proc* p)
{
  if(p->state == RUNNING)string = "running";
  if(p->state == RUNNABLE)string ="runnable";
  if(p->state == ZOMBIE)string ="zombie";
  if(p->state == SLEEPING)string ="sleeping";
  if(p->state == EMBRYO)string ="embryo";
}
int ps(void)
{
  struct proc* p;
  cprintf("pid\tpriority\t  state  \t\tr_time\tw_time\tn_run\tcur_q\tq0\tq1\tq2\tq3\tq4\n");
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == UNUSED)continue;

    int pid = p->pid;
    int priority = p->priority;
    int r = p->rtime;
    int w;
    if(p->state == RUNNABLE)w = ticks - p->enter;
    else w = 0;

    int n = p->n_run;
    int q = p->queue;
    int t[5];
    for(int i=0;i<5;i++)t[i] = p->all_ticks[i];
 
    cprintf("%d\t%d",pid,priority);

    if(p->state == RUNNING)cprintf("\t\trunning \t");
    if(p->state == RUNNABLE)cprintf("\t\trunnable\t");
    if(p->state == ZOMBIE)cprintf("\t\tzombie\t");
    if(p->state == SLEEPING)cprintf("\t\tsleeping\t");
    if(p->state == EMBRYO)cprintf("\t\tembryo\t");

    cprintf("\t%d\t%d\t%d\t%d",r,w,n,q);

    for(int i=0;i<5;i++)cprintf("\t%d",t[i]);
    cprintf("\n");

  }
  release(&ptable.lock);
  return 0;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


void aging()
{  
  struct proc* t;
  
  for(int i=1;i<5;i++)
  {
    t = head[i];

    while(t != 0)
    {
      t->age = ticks - t->enter;
      if(t->age > threshold)
      {
        if(t == head[i])
        {
          pop(i);
          t->queue = i-1;
          push(t->queue,t);
        }
        else
        {
          struct proc* prev = head[i];
          while(prev->next != t)
          {
            prev = prev->next;
          }
          prev->next = t->next;
          t->queue = i-1;
          push(t->queue,t);
        }
      }
      else
      {
        t = t->next;
      }
    }
  }
}


void push(int q, struct proc *p)
{
  p->enter = ticks;
  p->next = 0;
  p->queue = q;
  p->alloted = (1 << q);
  p->ticks = 0;  // total time it gets to run on cpu

  if(head[q] == 0)
  {
    head[q] = p;
    tail[q] = p;
  }
  else
  {
    tail[q]->next = p;
    tail[q] = p;
  }
}

void pop(int q)
{
  if(head[q] != 0)
  {
    head[q] = head[q]->next;
  }
}