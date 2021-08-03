# Code Tracing

##### `thread/main.h` defines different versions of Nachos kernel.

* `NetKernel`: the version that supports network communication
* `UserProgKernel`: the version that supports running user programs (a version of the basic multithreaded kernel)
* `ThreadedKernel`

##### `thread/main.cc` runs the operating system kernel.

```c++
kernel = new KernelType(argc, argv); // KernelType is determined in main.h
kernel->Initialize();
kernel->Run();
```

##### `thread/kernel.cc`: initialization and cleanup routines for the Nachos kernel

* `ThreadedKernel::ThreadedKernel()` parses the command line arguments.

* `ThreadedKernel::Initialize()` initializes Nachos global data structures.

  ```c++
  stats = new Statistics(); // collect statistics
  interrupt = new Interrupt; // start up interrupt handling
  scheduler = new Scheduler(); // initialize the ready queue
  alarm = new Alarm(randomSlice); // start up time slicing
  
  currentThread = new Thread("main");
  currnetThread->setStatus(RUNNING);
  ```

* `ThreadedKernel::Run()` runs the Nachos kernel.

  ```c++
  currentThread->Finish();
  ```

  * There may be other threads on the ready list. When the procedure "main" returns, we will switch to those threads.

##### `userprog/userkernel.cc`: initialization and cleanup routines for the Nachos kernel (`UserProgKernel` version)

* The class `UserProgKernel` inherits from the class `ThreadedKernel` and uses a private variable `Thread* t[10]` to store the threads.

* `UserProgKernel::Initialize()` initializes Nachos global data structures.

  ```c++
  ThreadedKernel::Initialize();
  machine = new Machine();
  fileSystem = new FileSystem();
  ```

* `UserProgKernel::Run()` runs the Nachos kernel.

  ```c++
  for (int n=1;n<=execfileNum;n++) {
  	t[n] = new Thread(execfile[n]); // create a thread
  	t[n]->space = new AddrSpace(); // create the address space
  	t[n]->Fork((VoidFunctionPtr) &ForkExecute), (void *)t[n]);
  }
  ThreadedKernel::Run();
  ```
  
  ```c++
  void ForkExecute(Thread *t) {
  	t->space->Execute(t->getName()); // see AddrSpace::Execute()
  }
  ```

##### `threads/thread.cc`: routines to manage threads

* The class `Thread` defines a **thread control block**, which represents a single thread of execution. Every thread has an execution stack (`stackTop` and `stack`), space to save CPU registers while not running (`machineState`), and a `status` (running/ready/blocked).

* If the Nachos kernel version is `UserProgKernel`:
  * A thread running a user program will actually have **two** sets of CPU registers
    * `machineState` for its state while executing **kernel code**.
    * `userRegisters` for its state while executing **user code**
  * A public variable `AddrSpace *space` will be defined for the address space of the user program.
  
* `Thread::Fork()` invokes `(*func)(arg)`, allowing caller and callee to execute concurrently. It is implemented as the following steps:

  1. Allocate and initialize an execution stack (so that a call to `SWITCH()` later will cause it to run the procedure). This stack is initialized with an initial stack frame for `ThreadRoot()`.

  2. Put the thread on the ready queue

     ```c++
     kernel->scheduler->ReadyToRun(this);
     ```

* Two machine-dependent routines are defined in `switch.s` (in **assembly language**).

  * `ThreadRoot()` is the first frame on thread execution stack.

    1. call `Thread::Begin()`

    2. call `(*func)(arg)` (i.e., `AddrSpace::Execute()`, see `ForkExecute()`)

    3. (when `func` returns, if ever) call `Thread::Finish()`

  * `SWITCH()` stops running oldThread and start running newThread.

* `Thread::Begin()` is called when the forked procedure starts up. It's main responsibilities are:
  1. Deallocate the previously running thread if it finished (see `Thread::Finish()`)
  2. Enable interrupts (so we can get time-sliced)
  
* `Thread::Finish()` is called when the forked procedure finishes.
  
  * NOTE: we can't immediately de-allocate the thread data structure or the execution stack, because we're still running in the thread and we're still on the stack! Instead, we **tell the scheduler to call the destructor** once it is running in the context of a different thread.
  
  ```c++
  Sleep(TRUE); // finishing = TRUE, telling the scheduler that this thread is done
  ```
  
* `Thread::Yield()` relinquished the CPU if any other thread is ready to run. If so, put the thread on the end of the ready list, so that it will eventually be re-scheduled.

  ```c++
  if (nextThread != NULL) {
      kernel->scheduler->ReadyToRun(this);
  	kernel->scheduler->Run(nextThread, FALSE); 
      	// finshing = FALSE, meaning that this thread is not done yet
  }
  ```

* `Thread::Sleep()` relinquished the CPU, because the current thread has either finished or is blocked waiting on a synchronization variable (Semaphore, Lock, or Condition). In the latter case, eventually some thread will wake this thread up, and put back on the ready queue, so that it can be re-scheduled.

  * We assume interrupts are already disabled, because it is called from synchronization routines that must **disable interrupts for atomicity**.
  
  ```c++
  ASSERT(kernel->interrupt->getLevel() == IntOff);
  
  while (nextThread == NULL)
  	kernel->interrupt->Idle(); // no one to run, wait for an interrupt
  kernel->scheduler->Run(nextThread, finishing); 
  	// "finishing" marks whether this thread is done or not yet
  ```

##### `threads/scheduler.cc`: routines to choose the next thread to run, and to dispatch to that thread

* This routines assume that interrupts are already disabled, so that we can assume **mutual exclusion** (there shouldn't be multiple threads accessing `readyList` simultaneously).

* `Scheduler::Scheduler()` initializes the list of ready but not running threads.

  ```c++
  readyList = new List<Thread *>;
  toBeDestroyed = NULL;
  ```

* `Scheduler::ReadyToRun()` marks a thread as ready, but not running.

  ```c++
  thread->setStatus(READY);
  readyList->Append(thread);
  ```

* `Scheduler::FindNextToRum()` returns the next thread to be scheduled onto the CPU.

  ```c++
  if (readyList->IsEmpty()) return NULL;
  else return readyList->RemoveFront();
  ```

* `Scheduler::Run()` dispatches the CPU to nextThread. It saves the state of the old thread, and loads the state of the new thread, by calling the **machine dependent context switch routine**, `SWITCH()`.

  ```c++
  Thread *oldThread = kernel->currentThread;
  if (finishing) { // mark that we need to delete current thread
  	toByDestroyed = oldThread;
  }
  
  #ifdef USER_PROGRAM // if this thread is a user program
  if (oldThread->space != NULL) {
      oldThread->SaveUserState(); // save the user's CPU registers
      oldThread->space->SaveState(); // save pageTable
  }
  #endif
  
  kernel->currentThread = nextThread; // switch to the next thread
  nextThread->setStatus(RUNNING); // nextThread is now running
  
  SWITCH(oldThread, nextThread);
  // now the machine is set to run nextThread
  // ...(nextThread may finish or do a context switch)
  // now we're back to oldThread
  CheckToBeDestroyed(); 	// check if the thread we were running before this
  						// one has finished and needs to be cleaned up
  
  #ifdef USER_PROGRAM
  // restore everything for oldThread
  if (oldThread->space != NULL) {
      oldThread->RestoreUserState();
      oldThread->space->RestoreState();
  }
  #endif
  ```

* `Scheduler::CheckToBeDestroyed()`: If the old thread gave up the processor because it was finishing, we need to delete its carcass.

  ```c++
  if (toBeDestroyed != NULL) {
  	delete toBeDestroyed;
  	toBeDestroyed = NULL;
  }
  ```

##### `threads/switch.s`: machine dependent context switch routines

* Context switching is inherently machine dependent, since the registers to be saved, how to set up an initial call frame, etc, are all specific to a processor architecture.
* `ThreadRoot()` is called from the `SWITCH()` routine  to start a thread for the first time.
* `SWITCH(oldThread, newThread)` stops running oldThread and start running newThread.
  * oldThread -- The current thread that was running, where the CPU register state is to be saved
  * newThread -- The new thread to be run, where the CPU register state is to be loaded from

##### `userprog/addrspace.cc`: routines to manage address spaces.

* `AddrSpace::AddrSpace()` creates an address space to run a user program and sets up the translation from program memory to physical memory. (For now, assume linear translation since we are only uni-programming)

  ```c++
  // create a page table with "NumPhysPages" entries
  pageTable = new TranslationEntry[NumPhysPages];
  for (unsigned int i = 0; i < NumPhysPages; i++) {
  	pageTable[i].virtualPage = i; // for now, virt page # = phys page #
  	pageTable[i].physicalPage = i;
  	pageTable[i].valid = TRUE;
  	pageTable[i].use = FALSE;
  	pageTable[i].dirty = FALSE;
  	pageTable[i].readOnly = FALSE;
  }
  ```

* `AddrSpace::Load()` loads a user program into memory from a file. It is implemented as the following steps:

  1. Open file

     ```c++
     OpenFile *executable = kernel->fileSystem->Open(fileName);
     ```

  2. Read the file in NOFF format

     ```c++
     NoffHeader noffH;
     executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
     ```

  3. Copy in the code and data segments into memory

     ```c++
     executable->ReadAt(
         &(kernel->machine->mainMemory[noffH.code.virtualAddr]),
         noffH.code.size, noffH.code.inFileAddr);
     executable->ReadAt(
         &(kernel->machine->mainMemory[noffH.initData.virtualAddr]),
         noffH.initData.size, noffH.initData.inFileAddr);
     ```

  * `OpenFile::ReadAt(char *into, int numBytes, int position)` reads bytes from a file. "into" is the buffer to contain the data to be read from disk, and "position" is the offset within the file of the first byte to be read. (see `filesys/openfile.cc`)

* `AddrSpace::Execute()` runs a user program.

  ```c++
  Load(fileName);
  this->InitRegisters();
  this->RestoreState();
  kernel->machine->Run();
  ```

* `AddrSpace::InitRegisters()` sets the initial values for the user-level register set.

  ```c++
  Machine *machine = kernel->machine;
  for (i = 0; i < NumTotalRegs; i++) machine->WriteRegister(i, 0);
  machine->WriteRegister(PCReg, 0);
  machine->WriteRegister(NextPCReg, 0);
  machine->WriteRegister(StackReg, numPages * PageSize - 16);
  ```

* `AddrSpace::SaveState()`: on a context switch, save any machine state specific to this address space

  ```c++
  pageTable=kernel->machine->pageTable;
  numPages=kernel->machine->pageTableSize;
  ```

* `AddrSpace::RestoreState()`: on a context switch, restore the machine state so that this address space can run

  ```c++
  kernel->machine->pageTable = pageTable;
  kernel->machine->pageTableSize = numPages;
  ```

##### `machine/machine.cc`: routines for simulating the execution of user programs

* The class `Machine` defines the simulated host workstation hardware, as seen by user programs -- the CPU registers, main memory, etc.

* Data structures accessible to the Nachos kernel -- main memory and the page table/TLB.

  ```c++
  char *mainMemory;
  TranslationEntry *tlb;
  TranslationEntry *pageTable;
  unsigned int pageTableSize;
  ```

  * In Nachos, the hardware translation of virtual addresses in the user program to physical addresses can be controlled by either **a traditional linear page table** or **a software-loaded translation lookaside buffer (tlb)**. We must have one of the two, but not both! (For now, we use linear page table, so `tlb` is not used)
  * For simplicity, both the page table pointer and the TLB pointer are public. However, while there can be multiple page tables (one per address space, stored in memory), there is only one TLB (implemented in hardware). Thus the TLB pointer should be considered as **read-only**, although the contents of the TLB are free to be modified by the kernel software.

* `Machine::RaiseException()` transfers control to the Nachos kernel from user mode, because the user program either invoked a system call, or some exception occurred (such as the address translation failed)

  ```c++
  kernel->interrupt->setStatus(SystemMode);
  ExceptionHandler(which);
  kernel->interrupt->setStatus(UserMode);
  ```

  * `ExceptionHandler()` (defined in `userprog/exception.cc`): entry point into the Nachos kernel from user programs. See `userprog/syscall.h` and `test/start.s` for more details.

##### `machine/mipssim.cc`: simulate a MIPS R2/3000 processor

* The class `Instruction` defines an instruction.

* `Machine::Run()` simulates the execution of a user-level program on Nachos. It is called by the kernel when the program starts up; never returns. This routine is re-entrant, in that it can be called multiple times concurrently -- one for each thread executing user code.

  ```c++
  Instruction *instr = new Instruction;
  kernel->interrupt->setStatus(UserMode);
  for (;;) {
      OneInstruction(instr);	
      kernel->interrupt->OneTick(); // simulate the CPU clock
  }
  ```

* `Machine::OneInstruction()` executes one instruction from a user-level program.

  ```c++
  ReadMem(registers[PCReg], 4, &raw);
  instr->value = raw;
  instr->Decode();
  switch (instr->opCode) {
      case OP_ADD:
          sum = registers[instr->rs] + registers[instr->rt];
          if (!((registers[instr->rs] ^ registers[instr->rt]) & SIGN_BIT)
              && ((registers[instr->rs] ^ sum) & SIGN_BIT)) {            
              RaiseException(OverflowException, 0);
              return; }        
          registers[instr->rd] = sum;
          break;
      ...
      case OP_SYSCALL:
          RaiseException(SyscallException, 0);
          break;
      ...
  }
  ```

  * If there is any kind of exception or interrupt, we invoke the exception handler.

##### `machine/translate.cc`: routines to translate virtual addresses to physical addresses.

* The class `TranslationEntry` defines an entry in a translation table -- either in a **page table** or a **TLB**. Each entry defines a mapping from one virtual page to one physical page.
* `Machine::ReadMem(int addr, int size, int *value)` reads "size" bytes of virtual memory at "addr" into the location pointed to by "value".
* `Machine::WriteMem(int addr, int size, int value)` writes"size" bytes of the contents of "value" into virtual memory at "addr".
* Both `Machine::ReadMem()` and `Machine::WriteMem()` call `Machine::Translate()`.
* `Machine::Translate()` translates a virtual address into a physical address, using either a page table or a TLB. It checks for alignment and all sorts of other errors, and if everything is ok, It will sets the use/dirty bits in the translation table entry and store the translated physical address in `physAddr`. If there was an error, it will return the type of the exception.

##### `userprog/syscall.h`: Nachos system call interface

* There are Nachos kernel operations that can be invoked from user programs, by trapping to the kernel via the "syscall" instruction. (see `start.s`)

##### `test/start.s`: **Assembly language** assisting user programs in running on top of Nachos

* `__start` initializes running a C program by calling "main".

* System call stubs: Assembly language assisting to make system calls to the Nachos kernel. There is one stub for each system call, that places the system call number into register r2. For example: 

  ```assembly
  PrintInt:
  	addiu	$2, $0, SC_PrintInt	/* r2 = r0 + SC_PrintInt */
  	syscall						/* execute the system call instruction */
  	j		$31					/* return to address of user program */
  ```

  * System call numbers (e.g., value of `SC_PrintInt`) are defined in `syscall.h`
  * `ExceptionHandler()` will then read register r2 to know which type of the system call it is going to handle.

* For example, if we run `./nachos -e ../test/test1`:

  ```c++
  #include "syscall.h"
  main() {
      int n;
      for (n=9; n>5; n--) PrintInt(n);
  }
  ```

  `__start` will initialize running the program by calling the main function, and `PrintInt()` will load the value of `SC_PrintInt` into register r2, so that `ExceptionHandler()` can execute the according system call.

  * `PrintInt()` &rarr; the "syscall" instruction &rarr; `Machine::OneInstruction()` (go into the case `OP_SYSCALL`) &rarr; `Machine::RaiseException()` &rarr; `ExceptionHandler()`

##### `machine/interrupt.cc`: routines to simulate hardware interrupts

* The class `Interrupt` defines the data structures for the simulation of hardware interrupts. We record whether interrupts are enabled or disabled, and any hardware interrupts that are scheduled to occur in the future.

* `Interrupt::Schedule()` arranges for the CPU to be interrupted when simulated time reaches "now + when".

  ```c++
  int when = kernel->status->totalTicks + fromNow;
  PendingInterrupt *toOccur = new PendingInterrupt(tocall, when, type);
  pending->Insert(toOccur);
  ```

  * `toCall` is the object the call when the interrupt occurs. (see `callback.h`)
  * `fromNow` is how far in the future (in simulated time) the interrupt is to occur.
  * `type` is the hardware device that generated the interrupt.

* `Interrupt::Idle()`: routine called when there is nothing in the ready queue

  ```c++
  status = IdleMode;
  if (CheckIfDue(TRUE)) { // check for pending interrupts
  	status = SystemMode;
  	return; //return in case there's now a runnable thread
  }
  
  // if there are no pending interrupts, and nothing is on the ready
  // queue, it is time to stop.
  Halt(); //delete kernel, never return
  ```

* `Interrupt::OneTick()` advances simulated time and check if there are any pending interrupts to be called. It is implemented as the following steps:

  1. Advance simulated time

  2. Turn off interrupts (**interrupts cannot be interrupted**)

  3. Call `Interrupt::CheckIfDue()` to check for pending interrupts

  4. Re-enable interrupts

  5. If the timer device handler asked for a context switch (i.e., the time slice expires) , do it now

     ```c++
     kernel->currentThread->Yield();
     ```

* `Interrupt::CheckIfDue()` checks if any interrupts are supposed to occur now, and if so, do them.

  ```c++
  do {
      next = pending->RemoveFront();		// pull an interrupt off the list
      next->callOnInterrupt->CallBack();	// call the interrupt handler
      delete next;
  } while (!pending->IsEmpty() 			
           && (pending->Front()->when <= stats->totalTicks));
  		// the interrupt is supposed to fire before now
  ```

  * The interrupt handler (`CallBack()`) is implemented through virtual function. The class `CallBackObj` (defined in `machine/callback.h`) is an abstract base class for objects that register callbacks. When we pass a pointer to the object to a lower level module, that module calls back via `obj->CallBack()`, without knowing the type of the object being called back. For example:

    * `Alarm::CallBack()` is called when the hardware timer generates an interrupt.
    * `ConsoleOutput::CallBack()` is invoked when the next character can be put out to the display.
    * `Disk::CallBack()` is invoked when disk request finishes.

##### `threads/alarm.cc`: routines to use a hardware timer device to provide a software alarm clock

* The class `Alarm` defines a **software alarm clock**.

  * The class `Alarm` inherits from the class `CallBackObj`.

* `Alarm::Alarm()` initializes a software alarm clock.

  ```c++
  timer = new Timer(doRandom, this);
  ```

* `Alarm::CallBack()`: software interrupt handler for the timer device. The timer device is set up to interrupt the CPU periodically (once every TimerTicks). This routine is called each time there is a timer interrupt, with interrupts disabled (see `inetrrupt::OneTick()`).

  ```c++
  Interrupt * interrupt = kernel->interrupt;
  MachineStatus status = interrupt->getStatus();
  if (status == IdleMode) {	// is it time to quit?
      if (!interrupt->AnyFutureInterrupts()) {
          timer->Disable(); } // turn off the timer
  }
  else { 						// there's someone to preempt
      interrupt->YieldOnReturn();	// tell Interrupt::OneTick() to 
  }								// do a context switch
  ```

##### `machine/timer.cc`: routines to emulate a hardware timer device

* The class `Timer` defines a **hardware timer**.

  * Note: nothing in here is part of Nachos. It is just an emulation for the hardware that Nachos is running on top of.
  * The class `Timer` inherits from the class `CallBackObj`.

* `Timer::Timer()` initializes a hardware timer device. 

  ```c++
  randomize = doRandom; 		// if true, arrange for the interrupts to
  							// occur at random, instead of fixed, intervals
  callPeriodically = tocall; 	// the interrupt handler to call when the
  							// timer expiers
  disable = FALSE;
  SetInterrupt();
  ```

* `Timer::CallBack()`: routine called when interrupt is generated by the hardware timer device.  It schedules the next interrupt, and invokes the interrupt handler.

  ```c++
  callPeriodically->CallBack();
  SetInterrupt();
  ```

  * To let software interrupt handler decide if it wants to disable future interrupts, `SetInterrupt()` is the last thing to do.

* `Timer::SetInterrupt()` causes a timer interrupt to occur in the future, unless future interrupts have been disabled. The delay is either fixed or random.

  ```c++
  if (!disable) {
  	int delay = TimerTicks; // TimerTicks = 100
  	if (randomize) {
  		delay = 1 + (RandomNumber() % (TimerTicks * 2));
  	}
      
  	// schedule the next timer device interrupt
  	kernel->interrupt->Schedule(this, delay, TimerInt);
  }
  ```
