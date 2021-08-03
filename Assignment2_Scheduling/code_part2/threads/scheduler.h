// scheduler.h 
//	Data structures for the thread dispatcher and scheduler.
//	Primarily, the list of threads that are ready to run.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "copyright.h"
#include "list.h"
#include "thread.h"
#include <map>

class SleepingThread {
    public:
        SleepingThread(Thread* t, int x)
            : sleeper(t), sleepTime(x) {};
        
        Thread* sleeper;
        int sleepTime; // the remaining sleeping time
};

// The following class defines the scheduler/dispatcher abstraction -- 
// the data structures and operations needed to keep track of which 
// thread is running, and which threads are ready but not running.

enum SchedulerType {
        FCFS,       // First Come First Served 
        RR,         // Round Robin
        NSJF,       // Shortest Job First (Non-preemptive)
        SJF         // Shortest Job First (Preemptive)
};

const float RATE = 0.5;

class Scheduler {
  public:
	Scheduler(SchedulerType type);  // Initialize list of ready threads 
	~Scheduler();				    // De-allocate ready list

	void ReadyToRun(Thread* thread);	
    					// Thread can be dispatched.
	Thread* FindNextToRun();	// Dequeue first thread on the ready 
					// list, if any, and return thread.
	void Run(Thread* nextThread, bool finishing);
	    				// Cause nextThread to start running
	void CheckToBeDestroyed();	// Check if thread that had been
    					// running needs to be deleted
	void Print();			// Print contents of ready list
    
    void SetToSleep(int sleepTime); 
                    // insert the thread to the sleepingList
    void AlarmTicks();
                    // minus sleepTime by 1 for each sleeping thread
                    // if some thread should wake up now, do so
    bool NoOneSleeping(); // return TRUE if sleepingList is empty
    
    SchedulerType GetSchedulerType() { return schedulerType; };

    int GetRestBurstTime(Thread* thread);
    
    void AccumNewBurst(); // accumulate the new burst time
    
    void Account(); // account the burst time of the current thread
    
    // SelfTest for scheduler is implemented in class Thread
    
  private:
	SchedulerType schedulerType;
	List<Thread *> *readyList;	// queue of threads that are ready to run,
					// but not running
	Thread *toBeDestroyed;		// finishing thread to be destroyed
    					// by the next thread that runs

    SortedList<SleepingThread *> *sleepingList;
    
    std::map<Thread *, std::pair<int, int> > *burstTimeMap;
                        // record the CPU burst time of each thread
                        // (*burstTimeMap)[thread]: (histBurst, newBurst)
    int startTicks;
};

#endif // SCHEDULER_H
