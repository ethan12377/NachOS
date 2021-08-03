// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

static int 
SleepTimeCompare(SleepingThread *x, SleepingThread *y) 
{
    if (x->sleepTime < y->sleepTime) { return -1; }
    else if (x->sleepTime > y->sleepTime) { return 1; }
    else { return 0; }
}

static int 
BurstTimeCompare(Thread *x, Thread *y)
{
    int xBurstTime = kernel->scheduler->GetRestBurstTime(x);
    int yBurstTime = kernel->scheduler->GetRestBurstTime(y);
    if (xBurstTime < yBurstTime) { return -1; }
    else if (xBurstTime > yBurstTime) { return 1; }
    else { return 0; }
}

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler(SchedulerType type)
{
    schedulerType = type;
	if (type == RR || type == FCFS ) readyList = new List<Thread *>; 
    else readyList = new SortedList<Thread *>(BurstTimeCompare); 
	toBeDestroyed = NULL;
    sleepingList = new SortedList<SleepingThread *>(SleepTimeCompare);
    burstTimeMap = new std::map<Thread*, std::pair<int, int> >;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyList; 
    while (!sleepingList->IsEmpty()) {
	delete sleepingList->RemoveFront();
    }
    delete sleepingList;
    delete burstTimeMap;
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());

    thread->setStatus(READY);
    if (burstTimeMap->find(thread) == burstTimeMap->end())
                                    // thread not in map yet
        (*burstTimeMap)[thread] = std::make_pair(0, 0);
                                    // initialize the CPU burst time to 0
	readyList->Append(thread);
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (readyList->IsEmpty()) {
	return NULL;
    } else {
    	return readyList->RemoveFront();
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
 
//	cout << "Current Thread" <<oldThread->getName() << "    Next Thread"<<nextThread->getName()<<endl;
   
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
        ASSERT(toBeDestroyed == NULL);
	    toBeDestroyed = oldThread;
        Account(); // account the burst time of the thread going to finish
    }
    
#ifdef USER_PROGRAM			// ignore until running user programs 
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	oldThread->space->SaveState();
    }
#endif
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    DEBUG(dbgScheduling, "Context Switching...");
    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
#ifdef USER_PROGRAM
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	oldThread->space->RestoreState();
    }
#endif
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    readyList->Apply(ThreadPrint);
}


void
Scheduler::SetToSleep(int sleepTime)
{
    Thread* sleepyThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    
    Account(); // account the burst time of the thread going to sleep
    
    SleepingThread* toSleep = new SleepingThread(sleepyThread, sleepTime);
    sleepingList->Insert(toSleep); // insert the thread in sorted order
    sleepyThread->Sleep(FALSE);
}

void
Scheduler::AlarmTicks()
{
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

bool 
Scheduler::NoOneSleeping()
{ 
    return sleepingList->IsEmpty(); 
};

int
Scheduler::GetRestBurstTime(Thread* thread)
{
    int estiBurst = (*burstTimeMap)[thread].first;
    int accumBurst = (*burstTimeMap)[thread].second;
    int restBurst = estiBurst - accumBurst;
    return (restBurst < 0) ? 0 : restBurst;
}

void Scheduler::AccumNewBurst()
{
    Thread* thread = kernel->currentThread;
    (*burstTimeMap)[thread].second += (kernel->stats->userTicks - startTicks);
    startTicks = kernel->stats->userTicks;
}

void Scheduler::Account()
{
    Thread* sleepyThread = kernel->currentThread;
    
    AccumNewBurst();
    ASSERT(burstTimeMap->find(sleepyThread) != burstTimeMap->end());
    int histBurst = (*burstTimeMap)[sleepyThread].first;
    int newBurst = (*burstTimeMap)[sleepyThread].second;
    int estiBurst = (int) (RATE * newBurst + (1-RATE) * histBurst);
    (*burstTimeMap)[sleepyThread].first = estiBurst;
    (*burstTimeMap)[sleepyThread].second = 0;
    if (schedulerType == SJF || schedulerType == NSJF) {
        DEBUG(dbgScheduling, "Estimating the next CPU busrt time of thread " 
                << sleepyThread->getName() << " ...");
        DEBUG(dbgScheduling, "histBurst: " << histBurst << ", newBusrt: " << newBurst
                << ", estiBusrt: " << estiBurst);
    }
}
