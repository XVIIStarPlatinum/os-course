#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include <stddef.h>

struct cpu cpus[NCPU];

struct proc dummyhead;

struct proc *initproc = 0;

int nextpid = 1;
struct spinlock pid_lock;
struct spinlock lst_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// global process count. Not to be confused with process_count, 
// which is a local variable for tracking total process count during allocproc.
volatile int proc_count = 0;

// initialize the proc table.
void
procinit(void)
{
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&lst_lock, "lst_lock");
  
  dummyhead.pid = -1;
	dummyhead.state = UNUSED;
	dummyhead.prev = &dummyhead;
	dummyhead.next = &dummyhead;
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with list_lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc* p;

  int process_count = __atomic_fetch_add(&proc_count, 1, __ATOMIC_SEQ_CST);
  if (process_count >= NPROC) {
    __atomic_fetch_sub(&proc_count, 1, __ATOMIC_SEQ_CST);
    return 0;
  }
    
  if (!(p = bd_malloc(sizeof(struct proc)))) {
    __atomic_fetch_sub(&proc_count, 1, __ATOMIC_SEQ_CST);
    return 0;
  }

  memset(p, 0, sizeof(struct proc));
  
  p->pid = allocpid();
  p->state = USED;
  
  if ((p->kstack = (uint64)kalloc()) == 0) {
    freeproc(p);
    return 0;
  }

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  acquire(&lst_lock);
  p->prev = &dummyhead;
  p->next = dummyhead.next;
  dummyhead.next->prev = p;
  dummyhead.next = p;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// list_lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->kstack)
    kfree((void*)p->kstack);
  p->kstack = 0;
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  
  p->prev->next = p->next;
  p->next->prev = p->prev;
  bd_free((void*)p);
  __atomic_fetch_sub(&proc_count, 1, __ATOMIC_RELAXED);
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&lst_lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&lst_lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  np->parent = p;
  np->state = RUNNABLE;
  release(&lst_lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold list_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = dummyhead.next; pp != &dummyhead; pp = pp->next) {
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup_lockless(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&lst_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup_lockless(p->parent);

  p->xstate = status;
  p->state = ZOMBIE;

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&lst_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for (struct proc* pp = dummyhead.next; pp != &dummyhead; pp = pp->next) {
      if(pp->parent == p){
        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&lst_lock);
            return -1;
          }
          freeproc(pp);
          release(&lst_lock);
          return pid;
        }
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&lst_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &lst_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    acquire(&lst_lock);
    for (p = dummyhead.next; p != &dummyhead; p = p->next) {
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
    }
    release(&lst_lock);
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&lst_lock))
    panic("sched lst_lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&lst_lock);
  p->state = RUNNABLE;
  sched();
  release(&lst_lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding list_lock from scheduler.
  release(&lst_lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    __sync_synchronize();
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  if (lk != &lst_lock) {
    acquire(&lst_lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &lst_lock) {
    release(&lst_lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// Must be called without list_lock.
void
wakeup(void *chan)
{
  acquire(&lst_lock);
  wakeup_lockless(chan);
  release(&lst_lock);
}

// Wake up all processes sleeping on chan.
void
wakeup_lockless(void *chan) {
  struct proc *p;
  for (p = dummyhead.next; p != &dummyhead; p = p->next){
    if(p != myproc()){
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&lst_lock);
  for (p = dummyhead.next; p != &dummyhead; p = p->next){
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&lst_lock);
      return 0;
    }
  }
  release(&lst_lock);
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&lst_lock);
  p->killed = 1;
  release(&lst_lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&lst_lock);
  k = p->killed;
  release(&lst_lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for (p = dummyhead.next; p != &dummyhead; p = p->next) {
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s\n", p->pid, state, p->name);
  }
}

/**
 * Just your run-of-the-mill dump test. 
 * Outputs the states of registers s2-s12 involved in the current process.
 */
uint64
dump(void)
{
  struct proc* current_process = myproc();
  uint64 *registers = &current_process->trapframe->s2;
  for(int i = 0; i < 10; ++i) 
  {
    printf("s%d = %d\n", i + 2, (uint32) registers[i]);
  }
  return 0;
}

/**
 * Another normal test. When compared to the one before, 
 * it has added functionalities of outputting more verbose information and
 * having access to processes other than the current one.
 */ 
uint64
dump2(int pid, int register_num, uint64* return_value)
{
  enum error_code {
    OK = 0,
    NO_RIGHTS = -1,
    INVALID_PID = -2,
    INVALID_REGISTER = -3,
    FAILED_WRITE = -4
  };

  if(register_num < 2 || register_num > 11) {
    return INVALID_REGISTER;
  }

  struct proc *current_proc = myproc();
  struct proc *target_proc = 0;

  acquire(&lst_lock);

  // Find the target process
  for(struct proc *p = dummyhead.next; p != &dummyhead; p = p->next) {
    if(p->pid == pid) {
      target_proc = p;
      break;
    }
  }

  if(!target_proc) {
    release(&lst_lock);
    return INVALID_PID;
  }

  // Check permissions: current process can access itself or its children
  if(current_proc->pid != target_proc->pid && 
     current_proc->pid != target_proc->parent->pid) {
    release(&lst_lock);
    return NO_RIGHTS;
  }

  uint64 *register_ptr = (uint64*)((char*)target_proc->trapframe + 
                         offsetof(struct trapframe, s2) + 
                         (register_num - 2) * sizeof(uint64));
  if(copyout(current_proc->pagetable, (uint64) return_value, 
             (char*)register_ptr, sizeof(uint64)) < 0) {
    release(&lst_lock);
    return FAILED_WRITE;
  }

  release(&lst_lock);
  return OK;
}
