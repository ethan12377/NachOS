// userkernel.cc 
//	Initialization and cleanup routines for the version of the
//	Nachos kernel that supports running user programs.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synchconsole.h"
#include "userkernel.h"
#include "synchdisk.h"

MemoryManager::MemoryManager()
{
    frameTable = new FrameInfoEntry[NumPhysPages];
    for (unsigned int i = 0; i < NumPhysPages; i++) {
        frameTable[i].valid = TRUE;
        frameTable[i].lock = FALSE;
        frameTable[i].addrSpace = 0;
        frameTable[i].vpn = 0;
    }
    swapTable = new FrameInfoEntry[NumSectors];
    for (unsigned int i = 0; i < NumSectors; i++) {
        swapTable[i].valid = TRUE;
        swapTable[i].lock = FALSE;
        swapTable[i].addrSpace = 0;
        swapTable[i].vpn = 0;
    }
    LRUstack = new List<unsigned int>;
}

MemoryManager::~MemoryManager()
{
    delete[] frameTable;
    delete[] swapTable;
    while (!LRUstack->IsEmpty()) LRUstack->RemoveFront();
    delete LRUstack;
}

int
MemoryManager::TransAddr(AddrSpace *space, int virtAddr, bool loadTime)
{
    unsigned int vpn = (unsigned) virtAddr / PageSize; // virtual page number
    unsigned int offset = (unsigned) virtAddr % PageSize;
    unsigned int pageFrame = NumPhysPages;
    for (unsigned int i = 0; i < NumPhysPages; i++)
        if (frameTable[i].addrSpace == space && frameTable[i].vpn == vpn)
            pageFrame = i;
    if (pageFrame == NumPhysPages)  // the page is in swap disk
        pageFrame = PageFaultHandler(vpn, loadTime);
    unsigned int physAddr = pageFrame * PageSize + offset;
    return physAddr;
}

unsigned int
MemoryManager::AcquirePage(AddrSpace *space, unsigned int vpn, bool loadTime)
{
    unsigned int newPage;
    
    for (unsigned int i = 0; i < NumPhysPages; i++) {   // find valid frame
        if (frameTable[i].valid && !(frameTable[i].lock)) {
            frameTable[i].valid = FALSE;
            frameTable[i].addrSpace = space;
            frameTable[i].vpn = vpn;
            newPage = i;
            LRUstack->Append(newPage);
            DEBUG(dbgSwap, "Acquring frame page " << newPage);
            return newPage;
        }
    }
    
    newPage = KickVictim(loadTime); // pick a victim and kick it to swap disk
    
    ASSERT(!(frameTable[newPage].valid));
    frameTable[newPage].addrSpace = space;
    frameTable[newPage].vpn = vpn;
    LRUstack->Append(newPage);
    DEBUG(dbgSwap, "Acquring frame page " << newPage);
    return newPage;
}

void
MemoryManager::ReleasePage(AddrSpace *space, unsigned int vpn)
{
    for (unsigned int i = 0; i < NumPhysPages; i++)
        if (frameTable[i].addrSpace == space && frameTable[i].vpn == vpn) {
            frameTable[i].valid = TRUE;
            LRUstack->Remove(i);
        }
    for (unsigned int i = 0; i < NumSectors; i++)
        if (swapTable[i].addrSpace == space && swapTable[i].vpn == vpn) {
            swapTable[i].valid = TRUE;
        }
}

unsigned int
MemoryManager::PageFaultHandler(unsigned int vpn, bool loadTime)
{
    AddrSpace* space = kernel->currentThread->space;
    
    unsigned int swapBackPage = NumSectors;
    for (unsigned int i = 0; i < NumSectors; i++)
        if (!swapTable[i].valid &&
            swapTable[i].addrSpace == space && swapTable[i].vpn == vpn) {
            swapBackPage = i;
            break;
        }
    ASSERT(swapBackPage != NumSectors);     // the page must in swap disk
    while (swapTable[swapBackPage].lock) kernel->currentThread->Yield();
    
    unsigned int newPage = AcquirePage(space, vpn, loadTime);
    char* newPos = kernel->machine->mainMemory + newPage * PageSize;
    
    DEBUG(dbgSwap, "Reading sector " << swapBackPage << " to frame page " << newPage);
    ASSERT(!(frameTable[newPage].lock));
    ASSERT(!(swapTable[swapBackPage].lock));
    frameTable[newPage].lock = TRUE;
    swapTable[swapBackPage].lock = TRUE;
    kernel->swapDisk->ReadSector(swapBackPage, newPos, loadTime);
                                // return only after the data has been read
    frameTable[newPage].lock = FALSE;
    swapTable[swapBackPage].lock = FALSE;
    
    space->UpdatePhysPage(vpn, newPage);    // set the page table
    
    swapTable[swapBackPage].valid = TRUE;
    
    return newPage;
}

void 
MemoryManager::UpdateLRUStack(unsigned int recentlyUsedPage)
{
    LRUstack->Remove(recentlyUsedPage);
    LRUstack->Append(recentlyUsedPage);
}

void 
MemoryManager::CheckLock(unsigned int page)
{
    while (frameTable[page].lock) kernel->currentThread->Yield();
    ASSERT(!(frameTable[page].lock));
}

unsigned int
MemoryManager::KickVictim(bool loadTime)
{
    unsigned int victimPage;
    ListIterator<unsigned int> iter(LRUstack);
    for (; !iter.IsDone(); iter.Next()) {
        if (!(frameTable[iter.Item()].lock)) {
            LRUstack->Remove(iter.Item());
            victimPage = iter.Item();
            break;
        }
    }
    ASSERT(!(frameTable[victimPage].lock));   // not doing I/O
    ASSERT(!(frameTable[victimPage].valid));  // keep FALSE
    
    AddrSpace* victimSpace = frameTable[victimPage].addrSpace;
    unsigned int victimVPN = frameTable[victimPage].vpn;
    char* victimData = kernel->machine->mainMemory + victimPage * PageSize;
    
    victimSpace->SetInvalid(victimVPN); // set the page table
    
    for (unsigned int i = 0; i < NumSectors; i++) { // find valid swap sector
        if (swapTable[i].valid && !(swapTable[i].lock)) {
            swapTable[i].valid = FALSE;
            swapTable[i].addrSpace = victimSpace;
            swapTable[i].vpn = victimVPN;
            
            DEBUG(dbgSwap, "Writing frame page " << victimPage << " to sector " << i);
            ASSERT(!(frameTable[victimPage].lock));
            ASSERT(!(swapTable[i].lock));
            frameTable[victimPage].lock = TRUE;
            swapTable[i].lock = TRUE;
            kernel->swapDisk->WriteSector(i, victimData, loadTime);
                                // return only after the data has been written
            frameTable[victimPage].lock = FALSE;
            swapTable[i].lock = FALSE;
            
            return victimPage;
        }
    }
    ASSERT(FALSE); // assume always have empty sector
    return 0;
}

//----------------------------------------------------------------------
// UserProgKernel::UserProgKernel
// 	Interpret command line arguments in order to determine flags 
//	for the initialization (see also comments in main.cc)  
//----------------------------------------------------------------------

UserProgKernel::UserProgKernel(int argc, char **argv) 
		: ThreadedKernel(argc, argv)
{
    debugUserProg = FALSE;
	execfileNum=0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
	    debugUserProg = TRUE;
	}
	else if (strcmp(argv[i], "-e") == 0) {
		execfile[++execfileNum]= argv[++i];
	}
    	 else if (strcmp(argv[i], "-u") == 0) {
		cout << "===========The following argument is defined in userkernel.cc" << endl;
		cout << "Partial usage: nachos [-s]\n";
		cout << "Partial usage: nachos [-u]" << endl;
		cout << "Partial usage: nachos [-e] filename" << endl;
	}
	else if (strcmp(argv[i], "-h") == 0) {
		cout << "argument 's' is for debugging. Machine status  will be printed " << endl;
		cout << "argument 'e' is for execting file." << endl;
		cout << "atgument 'u' will print all argument usage." << endl;
		cout << "For example:" << endl;
		cout << "	./nachos -s : Print machine status during the machine is on." << endl;
		cout << "	./nachos -e file1 -e file2 : executing file1 and file2."  << endl;
	}
    }
}

//----------------------------------------------------------------------
// UserProgKernel::Initialize
// 	Initialize Nachos global data structures.
//----------------------------------------------------------------------

void
UserProgKernel::Initialize()
{
    ThreadedKernel::Initialize();	// init multithreading

    machine = new Machine(debugUserProg);
    fileSystem = new FileSystem();
    swapDisk = new SynchDisk("New SwapDisk");
    memoryManager = new MemoryManager();
#ifdef FILESYS
    synchDisk = new SynchDisk("New SynchDisk");
#endif // FILESYS
}

//----------------------------------------------------------------------
// UserProgKernel::~UserProgKernel
// 	Nachos is halting.  De-allocate global data structures.
//	Automatically calls destructor on base class.
//----------------------------------------------------------------------

UserProgKernel::~UserProgKernel()
{
    delete fileSystem;
    delete machine;
    delete swapDisk;
    delete memoryManager;
#ifdef FILESYS
    delete synchDisk;
#endif
}

//----------------------------------------------------------------------
// UserProgKernel::Run
// 	Run the Nachos kernel.  For now, just run the "halt" program. 
//----------------------------------------------------------------------
void
ForkExecute(Thread *t)
{
	t->space->Execute(t->getName());
}

void
UserProgKernel::Run()
{

	cout << "Total threads number is " << execfileNum << endl;
	for (int n=1;n<=execfileNum;n++)
		{
		t[n] = new Thread(execfile[n]);
		t[n]->space = new AddrSpace();
		t[n]->Fork((VoidFunctionPtr) &ForkExecute, (void *)t[n]);
		cout << "Thread " << execfile[n] << " is executing." << endl;
		}
//	Thread *t1 = new Thread(execfile[1]);
//	Thread *t1 = new Thread("../test/test1");
//	Thread *t2 = new Thread("../test/test2");

//    AddrSpace *halt = new AddrSpace();
//	t1->space = new AddrSpace();
//	t2->space = new AddrSpace();

//    halt->Execute("../test/halt");
//	t1->Fork((VoidFunctionPtr) &ForkExecute, (void *)t1);
//	t2->Fork((VoidFunctionPtr) &ForkExecute, (void *)t2);
    ThreadedKernel::Run();
//	cout << "after ThreadedKernel:Run();" << endl;	// unreachable
}

//----------------------------------------------------------------------
// UserProgKernel::SelfTest
//      Test whether this module is working.
//----------------------------------------------------------------------

void
UserProgKernel::SelfTest() {
/*    char ch;

    ThreadedKernel::SelfTest();

    // test out the console device

    cout << "Testing the console device.\n" 
	<< "Typed characters will be echoed, until q is typed.\n"
    	<< "Note newlines are needed to flush input through UNIX.\n";
    cout.flush();

    SynchConsoleInput *input = new SynchConsoleInput(NULL);
    SynchConsoleOutput *output = new SynchConsoleOutput(NULL);

    do {
    	ch = input->GetChar();
    	output->PutChar(ch);   // echo it!
    } while (ch != 'q');

    cout << "\n";

    // self test for running user programs is to run the halt program above
*/




//	cout << "This is self test message from UserProgKernel\n" ;
}
