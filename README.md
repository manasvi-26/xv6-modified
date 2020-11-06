# **xv6-modifed**

#### Manasvi Vaidyula 2019101012

#####  How to Run:
- make clean
- make qemu-nox
- add the flag "SCHEDULER" to choose between RR, FCFS, PBS, and MLFQ

##### System-Call:
##### 1. `Waitx Syscall`
- Modified files : proc.c trap.c user.h usys.S syscall.h syscall.c sysproc.c defs.h proc.h 
- Arguments passed to the system call waitx is `rtime` and `wtime`
which are pointers to integers to which waitx will assign the total number of clock ticks during which process was waiting and total number of clock ticks when the process was running.
- ctime, etime, rtime, wtime was added to the proc structure in proc.h file.
- tester file used is `time.h`

##### 2. `Ps Syscall`
- Modified files : proc.c trap.c user.h usys.S syscall.h syscall.c sysproc.c defs.h proc.h 
- displays information about all the active processes 
- fields displayed are : PID, Priority, State, r_time, w_time, n_run, cur_q, q0, q1, q2, q3, q4
- Modified the proc structure to help display this.
- tested using the benchmark.c file.


##### Schedulers:
1. `FCFS`
- Set SCHEDULER=FCFS
- Non preemptive policy so we the yield() call was removed in trap.c.
- It selects process that was created earliest to run
- implemented in scheduler function in proc.c

2. `PBS`
-  Set SCHEDULER=PBS
-  It selects the process with the highest priority for execution. In case two or more processes have the same priority, we choose them in a round-robin fashion.
- implemented in scheduler function in proc.c
- `set_priority()` calls yield() when the priority of a process becomes lower than its old priority.(also implemented in proc.c)

3. `MLFQ`
- Set SCHEDULER=MLFQ
- MLFQ scheduler allows processes to move between different priority queues based on their behavior and CPU bursts. If a process uses too much CPU time, it is pushed to a lower priority queue, leaving I/O bound and interactive processes for higher priority queues. Also, to prevent starvation, it implements aging.
- functions added to implement the queue(used linked list):
  pop(int queue), aging() , push(int queue, strucut proc* p) 
-First aging is implemented by checking which process is exceeding waittime. 
-Chooses the process in highest queue that is runnable


#### Comparision
As we can see from the total time taken by all the schedulers :-
RR is the fastest one in terms of total time and also gives fair run time to the process that forks all other processes in the bench mark.
MLFQ and PBS are not far behind as they also complete in almost the same time or a little bit lower.
FCFS is the slowest one as it increases the overall turnaround time due to convoy effect.

from most time to least: -
FCFS > PBS >= MLFQ >= RR

**************
# Original Readme--
NOTE: we have stopped maintaining the x86 version of xv6, and switched
our efforts to the RISC-V version
(https://github.com/mit-pdos/xv6-riscv.git)

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern x86-based multiprocessor using ANSI C.

ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)). See also https://pdos.csail.mit.edu/6.828/, which
provides pointers to on-line resources for v6.

xv6 borrows code from the following sources:
    JOS (asm.h, elf.h, mmu.h, bootasm.S, ide.c, console.c, and others)
    Plan 9 (entryother.S, mp.h, mp.c, lapic.c)
    FreeBSD (ioapic.c)
    NetBSD (console.c)

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by Silas
Boyd-Wickizer, Anton Burtsev, Cody Cutler, Mike CAT, Tej Chajed, eyalz800,
Nelson Elhage, Saar Ettinger, Alice Ferrazzi, Nathaniel Filardo, Peter
Froehlich, Yakir Goaron,Shivam Handa, Bryan Henry, Jim Huang, Alexander
Kapshuk, Anders Kaseorg, kehao95, Wolfgang Keller, Eddie Kohler, Austin
Liew, Imbar Marinescu, Yandong Mao, Matan Shabtay, Hitoshi Mitake, Carmi
Merimovich, Mark Morrissey, mtasm, Joel Nider, Greg Price, Ayan Shafqat,
Eldar Sehayek, Yongming Shen, Cam Tenny, tyfkda, Rafael Ubal, Warren
Toomey, Stephen Tu, Pablo Ventura, Xi Wang, Keiichi Watanabe, Nicolas
Wolovick, wxdao, Grant Wu, Jindong Zhang, Icenowy Zheng, and Zou Chang Wei.

The code in the files that constitute xv6 is
Copyright 2006-2018 Frans Kaashoek, Robert Morris, and Russ Cox.

ERROR REPORTS

We don't process error reports (see note on top of this file).

BUILDING AND RUNNING XV6

To build xv6 on an x86 ELF machine (like Linux or FreeBSD), run
"make". On non-x86 or non-ELF machines (like OS X, even on x86), you
will need to install a cross-compiler gcc suite capable of producing
x86 ELF binaries (see https://pdos.csail.mit.edu/6.828/).
Then run "make TOOLPREFIX=i386-jos-elf-". Now install the QEMU PC
simulator and run "make qemu".

