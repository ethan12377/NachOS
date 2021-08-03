# Assignment 3: Memory Management

### Description

The test cases `test/matmult.c` and `test/sort.c` require lots of memory. Originally, when running the test cases, Nachos will run out of memory. (`NumPhysPages` is set to 32).

`test/matmult.c` is to do matrix multiplication on large arrays.

```c++
#include "syscall.h"
#define Dim 20

int A[Dim][Dim];
int B[Dim][Dim];
int C[Dim][Dim];

int main() {
    int i, j, k;
    for (i = 0; i < Dim; i++)		/* first initialize the matrices */
        for (j = 0; j < Dim; j++) {
             A[i][j] = i;
             B[i][j] = j;
             C[i][j] = 0; }
    for (i = 0; i < Dim; i++)		/* then multiply them together */
		for (j = 0; j < Dim; j++)
            for (k = 0; k < Dim; k++)
		 		C[i][j] += A[i][k] * B[k][j];

    Exit(C[Dim-1][Dim-1]);		/* and then we're done -- should be 7220! */
}
```

`test/sort.c` is to sort a large number of integers.

```c++
#include "syscall.h"

int A[1024];

int main() {
    int i, j, tmp;
    /* first initialize the array, in reverse sorted order */
    for (i = 0; i < 1024; i++) A[i] = 1024 - i;

    /* then sort! */
    for (i = 0; i < 1023; i++)
        for (j = 0; j < (1023 - i); j++)
            if (A[j] > A[j + 1]) {  /* out of order -> need to swap ! */
                tmp = A[j];
                A[j] = A[j + 1];
                A[j + 1] = tmp; }
    Exit(A[0]);		/* and then we're done -- should be 1! */
}
```

Running these test cases will result in a failed assertion.

```
$ cd ./userprog
$ ./nachos -e ../test/matmult
  Assertion failed: line 122 file ../userprog/addrspace.cc
  Aborted (core dumped)
```

To make these files runnable on Nachos, virtual memory should be implemented.

### Solution

We implement a virtual memory manager to realize **demand paging**. In `userprog/userkenel.h`, we create a disk as a swap space.

```c++
SynchDisk *synchDisk;
```

We then use the class `MemoryManager` to manage the swap space.

```c++
class MemoryManager{
	public:
		MemoryManager();
		~MemoryManager();
		int TransAddr(AddrSpace *space, int virtAddr, bool loadTime = FALSE);
			// return phyAddr (translated from virtAddr)
		unsigned int AcquirePage(AddrSpace *space, int vpn); 
			// ask a page (frame) for vpn 
		bool ReleasePage(AddrSpace *space, int vpn);
			// free a page
		void PageFaultHandler();
			// will be called when manager want to swap a page from SwapTable
			// to frameTable
		void UpdateLRUStack(unsigned int recentlyUsedPage);
		void CheckLock(unsigned int page);
	private:
    	unsigned int KickVictim(bool loadTime = FALSE);
    	List<unsigned int> *LRUstack;
    	FrameInfoEntry *frameTable; // record every physical page's information
    	FrameInfoEntry *swapTable;	// record every sector's information 
}    								// in swapDisk
```

```c++
class FrameInfoEntry {
	public:
		bool valid;				// if being used
		vool lock;				// if doing I/O
		AddrSpace *addrSpace;	// which process is using this page
		unsigned int vpn;		// which virtual page of the process
}								// is stored in this page
```

In `addrspace.cc`, we modify the code for loading page to load one page at a time. If `frameTable` is full, it will select a frame and kick it to the swap space. Here we use **LRU (least recently used) algorithm** as our page replacement method.

```c++
bool AddrSpace::Load(char *fileName) {
	...
	pageTable = new TranslationEntry[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = 
            kernel->memoryManager->AcquirePage(this, i, TRUE);
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;
    }
    int physAddr;
    int s, va, ia, remain;
    if (noffH.code.size > 0) {
        s = noffH.code.size;
        va = noffH.code.virtualAddr;
        ia = noffH.code.inFileAddr;
        while (s>0) {
            remain = PageSize - va % PageSize;
            if (s < remain) remain = s;
            physAddr = kernel->memoryManager->TransAddr(this, va, TRUE);
            executabl->ReadAt(
                &kernel->machine->mainMemory[physAddr], remain, ia);
            s -= remain;
            va += remain;
            ia += remain;
        }
    }
    // And so on data segment
    ...
}
```

Notice that when calling `MemoryManager::AcquirePage()` and `MemoryManager::TransAddr()`, we should pass an argument `loadTime = TRUE` to indicate that we are loading pages now. By doing so, we can accessing `synchdisk` without following synchronization. Since synchronization is implemented based on interrupt mechanism (see `synchdisk.cc`, `disk.cc`, and `synch.cc`), it only works after the machine starts to "Tick". However, `Machine::Run()` is called after `AddrSpace::Load()`, so synchronization does not work at load time. Therefore, we must disable synchronization at this stage.

```c++
unsigned int
MemoryManager::AcquirePage(AddrSpace *space, unsigned int vpn, bool loadTime) {
    unsigned int newPage;
    for (unsigned int i = 0; i < NumPhysPages; i++) { // find valid frame
        if (frameTable[i].valid && !(frameTable[i].lock)) {
            frameTable[i].valid = FALSE;
            frameTable[i].addrSpace = space;
            frameTable[i].vpn = vpn;
            newPage = i;
            LRUstack->Append(newPage);
            return newPage;
        }
    }
    newPage = KickVictim(loadTime); // pick a victim and kick it to swap disk
    ASSERT(!(frameTable[newPage].valid));
    frameTable[newPage].addrSpace = space;
    frameTable[newPage].vpn = vpn;
    LRUstack->Append(newPage);
    return newPage;
}
```

```c++
unsigned int MemoryManager::KickVictim(bool loadTime) {
	unsigned int victimPage;
    ListIterator<unsigned int> iter(LRUstack);
    for (; !iter.IsDone(); iter.Next()) {	// choose the one not doing I/O
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
```

To maintain `LRUstack`, we should update the stack each time when accessing a page (`Machine::Translate()`).

```c++
kernel->memoryManager->UpdateLRUStack(pageFrame);
```

```c++
void MemoryManager::UpdateLRUStack(unsigned int recentlyUsedPage)
{
    LRUstack->Remove(recentlyUsedPage);
    LRUstack->Append(recentlyUsedPage);
}
```

If the page going to be accessed is not in memory, `Machine::Translate()` will return `PageFaultException`, invoking the exception handler (in `exception.cc`).

```c++
case PageFaultException:
	val = kernel->machine->ReadRegister(BadVAddrReg) / PageSize;
	kernel->stats->numPageFaults ++;
	kernel->memoryManager->PageFaultHandler(val);
	return;
```

```c++
unsigned int MemoryManager::PageFaultHandler(unsigned int vpn, bool loadTime)
{
    AddrSpace* space = kernel->currentThread->space;
    
    unsigned int swapBackPage = NumSectors;
    for (unsigned int i = 0; i < NumSectors; i++)
        if (!swapTable[i].valid &&
            swapTable[i].addrSpace == space && swapTable[i].vpn == vpn) {
            swapBackPage = i;
            break;
        }
    ASSERT(swapBackPage != NumSectors);	// the page must in swap disk
    while (swapTable[swapBackPage].lock) kernel->currentThread->Yield();
    
    unsigned int newPage = AcquirePage(space, vpn, loadTime);
    char* newPos = kernel->machine->mainMemory + newPage * PageSize;
    
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
```

Note that in order to maintain synchronization, we should update the value of `lock` to `TRUE` before accessing the swap disk, and update it to `FALSE` after the function returns. When finding valid frame pages or swap sectors, we can only choose the page with the `lock` value being `FALSE`.

Finally, remember to modify `NumPhysPages` back to 32 in `machine.h`

```c++
const unsigned int NumPhysPages = 32;
```

### Testing

There is a bug in the original `test/sort.c`. We have modified it.

### Building

Copy the files in `code` to replace the original ones:

* `threads/kernel.h`, `threads/kernel.cc`
* `userprog/exception.cc`, `userprog/userkernel.h`, `userprog/userkernel.cc`, `userprog/addrspace.h`, `userprog/addrspace.cc`
* `filesys/synchdisk.h`, `filesys/synchdisk.cc`
* `machine/machine.h`, `machine/translate.cc`, `machine/disk.h`, `machine/dick.cc`
* `lib/debug.h`
* `test/sort.c`

```
$ cd ~/nachos-4.0/code
$ make
```

### Result

We get the correct results!

```
$ ./nachos -e ../test/sort -e ../test/matmult -e ../test/sort -e ../test/matmult -e ../test/matmult -e ../test/sort
  return value:7220
  return value:1
  return value:1
  return value:7220
  return value:7220
  return value:1

  Ticks: total 463625022, idle 220939777, system 124352240, user 118333005
  Disk I/O: reads 20541, writes 20809
  Console I/O: reads 0, writes 0
  Paging: faults 20496
  Network I/O: packets received 0, sent 0
```

##### Observation: Why context switches don't seem to be working?

For example, let's assume `sort` first acquires the lock. Then once `matmult` asks for the lock, it will be made sleep immediately (see `synch.cc`).  Although `matmult` will be put in the ready queue once `sort` releases the lock, `sort` will regain the lock before interrupted by the alarm because of the frequent disk accesses. When later `matmult` ask for the lock, it will be made sleep again. Therefore, `sort` will keep obtaining the lock, and `matmult` will keep sleeping, which make the system seem not doing context switches.

