# NachOS

[Nachos (Not Another Completely Heuristic Operating System)](https://homes.cs.washington.edu/~tom/nachos/), designed by Thomas Anderson at UC Berkeley in 1992, is an **educational operating system** that some components can be implemented by users, including threads and concurrency, multiprogramming, system calls, virtual memory, software-loaded TLB's, file systems, network protocols, remote procedure call, and distributed systems.

## Installation

* Environment: [Ubuntu 14.04.6 LTS 32-bit](https://releases.ubuntu.com/14.04/)

1. Install C Shell: `sudo apt-get install csh`

2. Install G++: `sudo apt-get install g++`

3. Download Nachos 4.0 & Cross-compiler

4. Decompressing:

   ```
   $ tar -zxvf nachos-4.0.tar.gz
   $ sudo mv mips-x86.linux-xgcc.tar.gz /
   $ sudo mv mips-decstation.linux-xgcc.gz /
   $ cd /
   $ sudo tar -zxvf /mips-x86.linux-xgcc.tar.gz
   $ sudo tar -zxvf /mips-decstation.linux-xgcc.gz
   ```

## Building

```
$ cd ~/nachos-4.0/code
$ make
```

