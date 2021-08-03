# Assignment 2: Scheduling

## Part 1 - System Call: Sleep()

### Description

We implement the system call `sleep(int time)`, which can make a thread sleep for a specified amount of time. The unit of the `time` parameter is "one `TimerTicks`".

### Solution

We first define the system call in the system call interface and the entry point to the kernel. Also, we need to implement the assembly language part (`start.s`) to make it able to support the system call.

* `userprog/syscall.h`: system call interface

  ```c++
  #define SC_Sleep 12
  ...
  void Sleep(int time); // Sleep a thread for a specified amount of time
  ```

* `userprog/exception.cc`: entry point into the Nachos kernel

  ```c++
  case SyscallException:
  	switch(type) {
  		...
  		case SC_Sleep:
  			val = kernel->machine->ReadRegister(4);
  			cout << "Thread " << kernel->currentThread->getName()
                  << " sleeps for " << val << "(TimerTicks)" << endl;
  			kernel->alarm->WaitUntil(val);
  			return;
          ...
  	}
  ```

  * We use `Alarm::WaitUntil()` to implement the system call.

* `test/start.s`: assembly language assisting user programs in running on top of Nachos

  ```assembly
  	.global	Sleep
  	.ent	Sleep
  Sleep:
  	addiu	$2, $0, SC_Sleep
  	syscall
  	j		$31
  	.end	Sleep
  ```

When `Alarm::WaitUntil()` is called, meaning that the current thread is calling the system call `sleep()`, scheduler will make the thread sleep and put it into a waitlist.

```c++
void Alarm::WaitUntil(int x) {
	Interrupt *interrupt = kernel->interrupt;
	IntStatus oldLevel = interrupt->SetLevel(IntOff);
	kernel->scheduler->SetToSleep(x); 	// SetToSleep assumes that interrupts
										// are disabled
	(void) interrupt->SetLevel(oldLevel);
}
```

Notice that there could be multiple threads sleeping simultaneously, so we need to use a data structure to manage them. We use a sorted list to store these sleeping threads, and each thread is recorded with its remaining sleeping time.

```c++
class Scheduler {
	public:
		...
		void SetToSleep(int sleepTime);
						// insert the thread to the sleepingList
		void AlarmTicks();
    					// minus sleepTime by 1 for each sleeping thread
						// if some thread should wake up now, do so
		bool NoOneSleeping(); // return TRUE if sleepingList is empty
	private:
		...
		SortedList<SleepingThread *> *sleepingList;
};
```

```c++
class SleepingThread {
	public:
		SleepingThread(Thread* t, int x)
			: sleeper(t), sleepTime(x) {};
		Thread* sleeper;
		int sleepTime; // the remaining sleeping time
};
```

The sleeping threads will be sorted in `sleepingList` according to the remaining sleeping time of each thread. We need a function for comparing `sleepTime` of the sleeping threads, so that `sleepingList` can utilize this function to sort them.

```c++
Scheduler::Scheduler() {
	...
	sleepingList = new SortedList<SleepingThread *>(SleepTimeCompare);
}
```

```c++
static int SleepTimeCompare(SleepingThread *x, SleepingThread *y) {
	if (x->sleepTime < y->sleepTime) { return -1; }
	else if (x->sleepTime > y->sleepTime) {return 1; }
	else { return 0; }
}
```

Note that the routines accessing `sleepingList` assume that interrupts are already disabled. Same as the routines accessing `readyList`, we don't want multiple threads to access the list concurrently because this may cause data inconsistency. Therefore, these routines are called after interrupts being disabled, which can ensure mutual exclusion.

```c++
void Scheduler::SetToSleep(int sleepTime) {
	Thread* sleepyThread = kernel->currentThread;
    ASSERT(kernel->interrupt->getLevel() == IntOff);
	SleepingThread* toSleep = new SleepingThread(sleepyThread, sleepTime);
	sleepingList->Insert(toSleep); // insert the thread in sorted order
	sleepyThread->Sleep(FALSE);
}
```

`Alarm::Callback()` is the software interrupt handler for the timer device. It is called each time the timer device interrupts the CPU (once every `TimerTicks`). Every time it is called, we should update the remaining sleeping time of each sleeping thread and check whether it's time for any threads to wake up, and if so, do it. Also note that when we are checking whether the whole program is completed, except for checking there is any pending interrupts, we should now additionally check there is any threads in `sleepingList`.

```c++
void Alarm::CallBack() {
	Interrupt *interrupt = kernel->interrupt;
	MachineStatus status = interrupt->getStatus();
	bool noOneSleeping = kernel->scheduler->NoOneSleeping();
    kernel->scheduler->AlarmTicks();
	if (status == IdleMode && noOneSleeping) { // is it time to quit?
		if (!interrupt->AnyFutureInterrupts()) {
			timer->Disable(); // turn off the timer
		}
	}
	else interrupt->YieldOnReturn(); // there's someone to preempt
}
```

```c++
void Scheduler::AlarmTicks() {
	ASSERT(kernel->interrupt->getLevel() == IntOff);
    ListIterator<SleepingThread *> iter(sleepingList);
    for (; !iter.IsDone(); iter.Next()) {
        iter.Item()->sleepTime --; // update the remaining sleeping time
    }
	while (!NoOneSleeping()) {
		if (sleepingList->Front()->sleepTime > 0) break;
        		// if the first thread in the sorted list is still sleeping,
        		// other threads must still be sleeping
		ReadyToRun(sleepingList->RemoveFront()->sleeper);
	}
}
```

### Testing

We design two threads both calling `sleep()`:

* `test/sleep1.c`

  ```c++
  #include "syscall.h"
  main() {
      int i;
      for(i = 0; i < 5; i++) {
          Sleep(1000000);
          PrintInt(1);
      }
      return 0;
  }
  ```

* `test/sleep2.c`

  ```c++
  #include "syscall.h"
  main() {
      int i;
      for(i = 0; i < 4; i++) {
          Sleep(500000);
          PrintInt(2);
      }
      return 0;
  }
  ```

We need to modify `test/Makefile` to add these programs into compilation, and convert the COFF (Common Object Code Format) into a NOFF (Nachos Object Code Format).

```makefile
all: halt shell matmult sort test1 test2 sleep sleep2
...
sleep: sleep.o start.o
        $(LD) $(LDFLAGS) start.o sleep.o -o sleep.coff
        ../bin/coff2noff sleep.coff sleep

sleep2: sleep2.o start.o
        $(LD) $(LDFLAGS) start.o sleep2.o -o sleep2.coff
        ../bin/coff2noff sleep2.coff sleep2
```

### Building

Copy the files in `code_part1` into `code` to replace the original ones:

* `threads/alarm.cc`, `threads/scheduler.h`, `threads/scheduler.cc`
* `userprog/syscall.h`, `userprog/exception.cc`
* `test/Makefile`, `test/sleep1.c`, `test/sleep2.c`, `test/start.s`

```
$ cd ~/nachos-4.0/code
$ make
```

### Result

We get the correct results!

```
$ cd ./userprog
$ ./nachos -e ../test/sleep1 -e ../test/sleep2
  Thread ../test/sleep1 sleeps for 1000000(TimerTicks)
  Thread ../test/sleep2 sleeps for 500000(TimerTicks)
  Print interger:2
  Thread ../test/sleep2 sleeps for 500000(TimerTicks)
  Print interger:2
  Thread ../test/sleep2 sleeps for 500000(TimerTicks)
  Print interger:1
  Thread ../test/sleep1 sleeps for 1000000(TimerTicks)
  Print interger:2
  Thread ../test/sleep2 sleeps for 500000(TimerTicks)
  Print interger:2
  return value:0
  Print interger:1
  Thread ../test/sleep1 sleeps for 1000000(TimerTicks)
  Print interger:1
  Thread ../test/sleep1 sleeps for 1000000(TimerTicks)
  Print interger:1
  Thread ../test/sleep1 sleeps for 1000000(TimerTicks)
  Print interger:1
  return value:0
```

## Part 2 - Scheduler Implementation

### Description

Nachos default scheduler is **Round-Robin** (RR). In this part, we also implement **Fist-Come-First-Served** (FCFS) and **Shortest-Job-First** (SJF) scheduler.

### Solution

We first add other types of scheduler to the `SchedulerType` enum defined in `scheduler.h`.

```c++
enum SchedulerType {
    FCFS,	// First Come First Served
    RR,		// Round Robin
    NSJF,	// Shortest Job First (Non-preemptive)
    SJF		// Shortest Job First (Preempitve)
}
```

Then we design the nachos interface to switch between different scheduling algorithms. To do so, we modify `threads/kernel.cc`, where `ThreadedKernel::ThreadedKernel()` parses the command line arguments.

```c++
for (int i = 1; i < argc; i++) {
	if (strcmp(argv[i], "-rs") == 0)
		...
	else if (strcmp(argv[i], "FCFS") == 0) schedulerType = FCFS;
	else if (strcmp(argv[i], "RR") == 0) schedulerType = RR; // default
	else if (strcmp(argv[i], "NSJF") == 0) schedulerType = NSJF;
	else if (strcmp(argv[i], "SJF") == 0) schedulerType = SJF;
}
```

When `ThreadedKernel::Initialize()` initializes the scheduler, it passes `schedulerType` to the constructor of `Scheduler`.

```c++
scheduler = new Scheduler(schedulerType);
```

In `Scheduler::Scheduler()`, we define the data type of `readyList` to be `List` or `SortedList` according to the value of `schedulerType`. If `schedulerType` is RR or FCFS, we store the ready threads in a FIFO data structure (a `List`). If `schedulerType` is SJF or NSJF, we should remove the thread with the shortest CPU burst each time when dequeuing. Therefore, we store the ready threads in a `SortedList`.

```c++
if (type == RR || type == FCFS) readyList = new List<Thread *>;
else readyList = new SortedList<Thread *>(BurstTimeCompare);
```

To estimate the next CPU burst time, we use the following formula:
$$
\tau_{n+1}=\alpha t_{n}+(\alpha-1)\tau_{n}
$$
where $t_n$​ means the new burst time, and $\tau_{n}$​ means the history burst time. We define a `map` as a private variable in the class `Scheduler` to record these information for each thread.

```c++
std::map<Thread *, std::pair<int, int> > *burstTimeMap;
	// record the CPU burst time of each thread
	// (*burstTimeMap)[thread]: (histBurst, newBurst)
```

First we initialize $\tau_0$ to 0 in `Scheduler::ReadyToRun()`.

```c++
if (burstTimeMap->find(thread) == burstTimeMap->end())
								// thread not in map yet
	(*burstTimeMap)[thread] = std::make_pair(0, 0);
								// initialize the CPU burst time to 0
```

Each time when `Alarm::CallBack()` is called, the number of user instructions executed during a `TimerTicks` will be accumulate to `newBurst`.

```c++
kernel->scheduler->AccumNewBurst();
```

```c++
void Scheduler::AccumNewBurst() {
	Thread* thread = kernel->currentThread;
	(*burstTimeMap)[thread].second += (kernel->stats->userTicks - startTicks);
	startTicks = kernel->stats->userTicks;
}
```

When a thread is set to sleep or finishes, `Account()` will be called. It estimates the next CPU burst time and update the value to `burstTimeMap`.

```c++
void Scheduler::Account() {
	Thread* sleepyThread = kernel->currnetThread;
	AccumNewBurst();
	ASSERT(burstTimeMap->find(sleepyThread) != burstTimeMap->end());
	int histBurst = (*burstTimeMap)[sleepyThread].first;
	int newBurst = (*burstTimeMap)[sleepyThread].second;
	int estiBurst = (int) (RATE * newBurst + (1-RATE) * histBurst; // RATE = 0.5
	(*burstTimeMap)[sleepyThread].first = estiBurst;
	(*burstTimeMap)[sleepyThread].second = 0;
}
```

Remember to call `YieldOnReturn()` in `Alarm::CallBack()` only if the scheduler is preemptive (RR or SJF).

```c++
if (kernel->scheduler->GetSchedulerType() == RR ||
	kernel->scheduler->GetSchedulerType() == SJF)
	interrupt->YieldOnReturn();
```

Notice that in the case of SJF scheduler, a thread may be preempted when its current burst has not ended yet. When choosing a thread to run from `readyList`, we should choose the one with the shortest **remaining burst time** instead of the shortest **estimated burst time**. Therefore, in `BurstTimeCompare()`, which is used by `readyList` to sort the ready threads, we should make the comparison according to the remaining burst time of each thread.

```c++
static int BurstTimeCompare(Thread *x, Thread *y) {
	int xBurstTime = kernel->scheduler->GetRestBurstTime(x);
	int yBurstTime = kernel->scheduler->GetRestBurstTime(y);
	if (xBurstTime < yBurstTime) { return -1; }
	else if (xBurstTime > yBurstTime) { return 1; }
	else { return 0; }
}
```

```c++
int Scheduler::GetRestBurstTime(Thread* thread) {
	int estiBurst = (*burstTimeMap)[thread].first;
	int accumBurst = (*burstTimeMap)[thread].second;
	int restBrust = estiBurst - accumBurst;
	return (restBurst < 0) ? 0 : restBurst;
}
```

### Testing

In addition to `test/test1` and `test/test2`, we design three test cases with different CPU burst time for the testing of SJF and NSJF. These threads will print out a series of three-digit integers. We can see the hundreds place to know which thread is running now.

* `test/sjf_test1.c`

  ```c++
  #include "syscall.h"
  main() {
      int i, j;
      for(i = 0; i < 5; i++) {
          for (j = 0; j < 3; j++) PrintInt(100+i*10+j);
          Sleep(1);
      }
  }
  ```

* `test/sjf_test2.c`

  ```c++
  #include "syscall.h"
  main() {
      int i, j;
      for(i = 0; i < 5; i++) {
          for (j = 0; j < 5; j++) PrintInt(200+i*10+j);
          Sleep(1);
      }
  }
  ```

* `test/sjf_test3.c`

  ```c++
  #include "syscall.h"
  main() {
      int i;
      for(i = 0; i < 5; i++) {
          PrintInt(300+i*10);
          Sleep(1);
      }
  }
  ```

Also remember to modify `test/Makefile` to add these programs into compilation, and convert the COFF into a NOFF.

In order to run these three threads concurrently, we temporarily modify `NumPhysPages` in `machine.h` to make these threads able to be loaded into memory at the same time. (This problem should be solved after the Virtual Memory Manager is implemented. For now, let's simply modify the memory size.)

```c++
const unsigned int NumPhysPages = 64;
```

To see if the result is correct and to observe the scheduling behavior, we add some debug messages. We add a debugging flag in `lib/debug.h`. In this way, we can see the debug messages about context switching and burst time estimation by adding `-d z` in the command line.

```c++
const char dbgScheduling = 'z' // CPU scheduling
```

### Building

Copy the files in `code_part2` into `code` to replace the original ones:

* `threads/alarm.cc`, `threads/scheduler.h`, `threads/scheduler.cc`, `threads/kernel.h`, `threads/kernel.cc`
* `machine/machine.h`
* `lib/debug.h`
* `test/Makefile`, `test/scheduling_test1.c`, `test/scheduling_test2.c`, `test/scheduling_test3.s`

```
$ cd ~/nachos-4.0/code
$ make
```

### Result

We test FCFS through running `test/test1` and `test/test2` concurrently, and compare the result with RR (default scheduling)

```
$ cd ./userprog
$ ./nachos -d z -e ../test/test1 -e ../test/test2
  Context Switching...
  Print integer:9
  Print integer:8
  Print integer:7
  Context Switching...
  Print integer:20
  Print integer:21
  Print integer:22
  Print integer:23
  Print integer:24
  Context Switching...
  Print integer:6
  return value:0
  Context Switching...
  Print integer:25
  return value:0
$ ./nachos -d z -FCFS -e ../test/test1 -e ../test/test2
  Context Switching...
  Print integer:9
  Print integer:8
  Print integer:7
  Print integer:6
  return value:0
  Context Switching...
  Print integer:20
  Print integer:21
  Print integer:22
  Print integer:23
  Print integer:24
  Print integer:25
  return value:0
```

We test SJF and NSJF through running `test/sjf_test1`, `test/sjf_test2`, and `test/sjf_test3` concurrently.

```
$ ./nachos -d z -NSJF -e ../test/sjf_test1 -e ../test/sjf_test2 -e ../test/sjf_test3
  Estimating the next CPU busrt time of thread main ...
  histBurst: 0, newBusrt: 0, estiBusrt: 0
  Context Switching...
  Print integer:100
  Print integer:101
  Print integer:102
  Thread ../test/sjf_test1 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test1 ...
  histBurst: 0, newBusrt: 104, estiBusrt: 52
  Context Switching...
  Print integer:200
  Print integer:201
  Print integer:202
  Print integer:203
  Print integer:204
  Thread ../test/sjf_test2 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test2 ...
  histBurst: 0, newBusrt: 156, estiBusrt: 78
  Context Switching...
  Print integer:300
  Thread ../test/sjf_test3 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test3 ...
  histBurst: 0, newBusrt: 31, estiBusrt: 15
  Context Switching...
  Print integer:110
  Print integer:111
  Print integer:112
  Thread ../test/sjf_test1 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test1 ...
  histBurst: 52, newBusrt: 102, estiBusrt: 77
  Context Switching...
  Print integer:310
  Thread ../test/sjf_test3 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test3 ...
  histBurst: 15, newBusrt: 29, estiBusrt: 22
  Context Switching...
  Print integer:120
  Print integer:121
  Print integer:122
  Thread ../test/sjf_test1 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test1 ...
  histBurst: 77, newBusrt: 102, estiBusrt: 89
  Context Switching...
  Print integer:320
  Thread ../test/sjf_test3 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test3 ...
  histBurst: 22, newBusrt: 29, estiBusrt: 25
  Context Switching...
  Print integer:210
  Print integer:211
  Print integer:212
  Print integer:213
  Print integer:214
  Thread ../test/sjf_test2 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test2 ...
  histBurst: 78, newBusrt: 154, estiBusrt: 116
  ...(Ommited content)...
  Ticks: total 1900, idle 180, system 220, user 1500
$ ./nachos -d z -SJF -e ../test/sjf_test1 -e ../test/sjf_test2 -e ../test/sjf_test3
  Estimating the next CPU busrt time of thread main ...
  histBurst: 0, newBusrt: 0, estiBusrt: 0
  Context Switching...
  Print integer:100
  Context Switching...
  Print integer:200
  Print integer:201
  Print integer:202
  Context Switching...
  Print integer:300
  Thread ../test/sjf_test3 sleeps for 1(TimerTicks)
  Estimating the next CPU busrt time of thread ../test/sjf_test3 ...
  histBurst: 0, newBusrt: 31, estiBusrt: 15
  ...(Ommited content)...
  Ticks: total 2001, idle 91, system 410, user 1500
```

##### Observation:

* The thread with shorter CPU burst time finishes earlier.
* Under NSJF, `sjf_test2` is in danger of starvation. If there are always other threads with shorter CPU burst ready, `sjf_test2` may never be executed.
* SJF indeed has fewer idle ticks than NSJF does. However, NSJF has fewer context switches than SJF does, and the less system ticks make the total ticks of NSJF fewer than that of SJF.
