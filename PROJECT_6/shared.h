/*
Auhtor: GRant Hughes
Date: Dec 11, 2025
File: file that holds resources for the main files
*/

#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <ctime>

// constants
#define MAX_PROCESSES 18
#define MEMORY_SIZE 65536        // 64K total memory
#define PAGE_SIZE 1024           // 1K page size
#define PROCESS_MEMORY 16384     // 16K per process
#define PAGES_PER_PROCESS 16     // 16 pages per process
#define TOTAL_FRAMES 64          // 64 frames total
#define DISK_IO_TIME_NS 14000000 // 14ms in nanoseconds

// simulated clock structure
struct Clock
{
    unsigned int seconds;
    unsigned int nanoseconds;
};

// frame table entry
struct Frame
{
    bool occupied;
    bool dirtyBit;
    int processId;
    int pageNumber;
    unsigned int loadTime;  // When this frame was loaded (for FIFO)
};

// page table for each process
struct PageTableEntry
{
    int frameNumber;  // -1 if not in memory
    bool valid;
    bool dirty;
};

// Process Control Block
struct PCB
{
    pid_t pid;
    bool inUse;
    int processId;
    unsigned int startTime;
    unsigned int totalMemoryAccesses;
    unsigned int pageFaults;
    unsigned long long totalAccessTime;
    PageTableEntry pageTable[PAGES_PER_PROCESS];
};

// message structure for IPC
struct Message
{
    long mtype;
    int processId;
    int address;
    bool isWrite;
    bool terminate;
    bool granted;
};

// shared memory structure
struct SharedMemory
{
    Clock clock;
    PCB processTable[MAX_PROCESSES];
    Frame frameTable[TOTAL_FRAMES];
};

// blocked processes
struct BlockedProcess
{
    int processId;
    int address;
    bool isWrite;
    unsigned long long unblockTime;
    BlockedProcess* next;
};

#endif