// userkernel.h
//	Global variables for the Nachos kernel, for the assignment
//	supporting running user programs.
//
//	The kernel supporting user programs is a version of the 
//	basic multithreaded kernel.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef USERKERNEL_H  
#define USERKERNEL_H

#include "kernel.h"
#include "filesys.h"
#include "machine.h"
#include "synchdisk.h"
#include "list.h"
class SynchDisk;

class FrameInfoEntry {
    public:
        bool valid;             // If being used
        bool lock;              // doing I/O
        AddrSpace *addrSpace;   // which process is using this page
        unsigned int vpn;       // which virtual page of the process
                                // is stored in this page
};

class MemoryManager {
    public:
        MemoryManager();
        ~MemoryManager();
        int TransAddr(AddrSpace *space, int virtAddr, bool loadTime = FALSE);
                // return phyAddr (translated from virtAddr)
        unsigned int AcquirePage(AddrSpace *space, unsigned int vpn, bool loadTime = FALSE);
                // ask a page (frame) for vpn
        void ReleasePage(AddrSpace *space, unsigned int vpn);
                // free a page
        unsigned int PageFaultHandler(unsigned int vpn, bool loadTime = FALSE);
                // will be called when manager want to swap a page from SwapTable
                // to frameTable
        void UpdateLRUStack(unsigned int recentlyUsedPage);
        void CheckLock(unsigned int page);
    
    private:
        unsigned int KickVictim(bool loadTime = FALSE);
	    List<unsigned int> *LRUstack;
        FrameInfoEntry *frameTable; // record every physical page's information
        FrameInfoEntry *swapTable;  // record every sector's information in swapDisk
};

class UserProgKernel : public ThreadedKernel {
  public:
    UserProgKernel(int argc, char **argv);
				// Interpret command line arguments
    ~UserProgKernel();		// deallocate the kernel

    void Initialize();		// initialize the kernel 

    void Run();			// do kernel stuff 

    void SelfTest();		// test whether kernel is working

// These are public for notational convenience.
    Machine *machine;
    FileSystem *fileSystem;
    
    SynchDisk *swapDisk;        // use the disk as the swap space 
    MemoryManager *memoryManager;

#ifdef FILESYS
    SynchDisk *synchDisk;
#endif // FILESYS

  private:
    bool debugUserProg;		// single step user program
	Thread* t[10];
	char*	execfile[10];
	int	execfileNum;
};

#endif //USERKERNEL_H
