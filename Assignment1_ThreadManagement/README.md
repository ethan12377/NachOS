# Assignment 1: Thread Management

### Description

Both the test cases `test/test1` and `test/test2` print out integers in series.

```c++
# include "syscall.h"
main() {
	int n;
	for (n=9; n>5; n--) PrintInt(n);
}
```

```c++
# include "syscall.h"
main() {
	int n;
	for (n=20; n<25; n++) PrintInt(n);
}
```

The original kernel can only support uni-programming:

```
$ cd ./userprog
$ ./nachos -e ../test/test1
  Print integer:9
  Print integer:8
  Print integer:7
  Print integer:6
  return value:0
$ ./nachos -e ../test/test2
  Print integer:20
  Print integer:21
  Print integer:22
  Print integer:23
  Print integer:24
  Print integer:25
  return value:0
```

Wrong results without multi-programming:

```
$ ./nachos -e ../test/test1 -e ../test/test2
  Print integer:9
  Print integer:8
  Print integer:7
  Print integer:20
  Print integer:21
  Print integer:22
  Print integer:23
  Print integer:24
  Print integer:6
  Print integer:7
  Print integer:8
  Print integer:9
  Print integer:10
  ...
  Print integer:24
  Print integer:25
  return value:0
  Print integer:26
  return value:0
```

### Solution

We declare an array (`usedPhysPages[NumPhysPages]`) in the class `ThreadedKernel` to record the physical pages that have been used.

When loading a user program (`AddrSpace::Load()`) , we calculate the number of pages first, and create a page table with a size of `numPages` according to the `usedPhysPages` array.

```c++
// how big is the address space?
size = noffH.code.size + noffH.initData.size + noffH.uninitData.size
    	+ UserStackSize;
numPages = divRoundUp(size, PageSize);

// set page table
pageTable = new TranslationEntry[NumPhysPages];
for (unsigned int i = 0, j = 0; i < numPages; i++) {
    pageTable[i].virtualPage = i;
	while (j < NumPhysPages && kernel->usedPhysPages[j]) j++;
	kernel->usedPhysPages[j] = TRUE;
	pageTable[i].physicalPage = j;
	pageTable[i].valid = TRUE;
    pageTable[i].use = FALSE;
	pageTable[i].dirty = FALSE;
	pageTable[i].readOnly = FALSE;
}
```

Then we copy the code and data segments into memory. Notice that the allocated physical space is continuous for now, so we can use `ReadAt()` to load file content into memory once.

```c++
vpn = (unsigned) noffH.code.virtualAddr / PageSize; // virtual page number
offset = (unsigned) noffH.code.virtualAddr % PageSize;
physicalAddr = pageTable[vpn].physicalPage * PageSize + offset;
executable->ReadAt(
	&(kernel->machine->mainMemory[physicalAddr]),
	noffH.code.size, noffH.code.inFileAddr);

// And so on data segment
```

Last, remember to update the `usedPhysPages` array when deallocating the address space. (`AddrSpace::~AddrSpace()`)

```c++
for (unsigned int i = 0; i < numPages; i++) {
	kernel->usedPhysPages[pageTable[i].physicalPage] = FALSE;
}
```

### Building

Copy the files in `code` to replace the original ones:

* `threads/kernel.h`, `threads/kernel.cc`
* `userprog/addrspace.cc`

```
$ cd ~/nachos-4.0/code
$ make
```

### Result

We get the correct results with multi-programming!

```
$ ./nachos -e ../test/test1 -e ../test/test2
  Print integer:9
  Print integer:8
  Print integer:7
  Print integer:20
  Print integer:21
  Print integer:22
  Print integer:23
  Print integer:24
  Print integer:6
  return value:0
  Print integer:25
  return value:0
```

