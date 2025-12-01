/*
Author: [Your Name]
Date: Dec 11 2025
File: Memory Management with FIFO Page Replacement
*/

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <vector>
#include <queue>
#include "shared.h"

using namespace std;

// variables
SharedMemory* sharedMem = nullptr;
int shmid = -1;
int msgid = -1;
ofstream logFile;
vector<pid_t> childPids;
BlockedProcess* blockedQueueHead = nullptr;
unsigned int nextLaunchTime = 0;
int processesLaunched = 0;
unsigned int nextDisplayTime = 500000000; // 0.5 seconds in nanoseconds

// statistics
unsigned long long totalPageFaults = 0;
unsigned long long totalReads = 0;
unsigned long long totalWrites = 0;
unsigned long long totalMemoryAccesses = 0;

// configuration
int maxProcesses = 18;
int simultaneousProcesses = 5;
int timeLimitForChildren = 250000; // in milliseconds
int launchInterval = 100000; // in nanoseconds
string logFileName = "logfile.txt";

// prototypes
void cleanup();
void signalHandler(int sig);
void initializeSharedMemory();
void addTime(unsigned int ns);
unsigned long long getTimeInNanoseconds();
void launchChild();
void handleMessage(Message& msg);
void processPageRequest(int processId, int address, bool isWrite);
int findFreeFrame();
int evictFrameFIFO();
void addToBlockedQueue(int processId, int address, bool isWrite, unsigned long long unblockTime);
void checkBlockedProcesses();
void displayMemoryLayout();
void terminateProcess(int processId);
void parseArgs(int argc, char* argv[]);
void printUsage(const char* progName);

int main(int argc, char* argv[])
{
    parseArgs(argc, argv);
    
    // signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(5);
    
    // open log file
    logFile.open(logFileName);
    if (!logFile.is_open()) {
        cerr << "Error: Could not open log file " << logFileName << endl;
        exit(1);
    }
    // shared memory and message queue
    initializeSharedMemory();
    
    cout << "OSS: Starting Operating System Simulator" << endl;
    logFile << "OSS: Starting Operating System Simulator" << endl;
    
    while (processesLaunched < maxProcesses || childPids.size() > 0) {
        // increasing clock by small amount each iteration
        addTime(1000); // 1 microsecond
        
        // seeing if we should launch a new child
        if (processesLaunched < maxProcesses && 
            childPids.size() < (size_t)simultaneousProcesses &&
            getTimeInNanoseconds() >= nextLaunchTime) {
            launchChild();
            nextLaunchTime = getTimeInNanoseconds() + launchInterval;
        }
        // chekc for blocked processes that should be unblocked
        checkBlockedProcesses();
        
        // non-blocking receive from message queue
        Message msg;
        if (msgrcv(msgid, &msg, sizeof(Message) - sizeof(long), 1, IPC_NOWAIT) > 0) {
            handleMessage(msg);
        }
        // show memory layout every 0.5 seconds
        if (getTimeInNanoseconds() >= nextDisplayTime) {
            displayMemoryLayout();
            nextDisplayTime += 500000000; // Add 0.5 seconds
        }
        // chekcing for terminated children
        int status;
        pid_t terminated = waitpid(-1, &status, WNOHANG);
        if (terminated > 0) {
            childPids.erase(remove(childPids.begin(), childPids.end(), terminated), childPids.end());
        }
    }
    
    // final statistics
    cout << "\nFinal Statistics:" << endl;
    cout << "Total Page Faults: " << totalPageFaults << endl;
    cout << "Total Reads: " << totalReads << endl;
    cout << "Total Writes: " << totalWrites << endl;
    cout << "Total Memory Accesses: " << totalMemoryAccesses << endl;
    
    if (totalMemoryAccesses > 0) {
        double pageFaultRate = (double)totalPageFaults / totalMemoryAccesses * 100.0;
        cout << "Page Fault Rate: " << pageFaultRate << "%" << endl;
        
        logFile << "\nFinal Statistics:" << endl;
        logFile << "Total Page Faults: " << totalPageFaults << endl;
        logFile << "Total Reads: " << totalReads << endl;
        logFile << "Total Writes: " << totalWrites << endl;
        logFile << "Total Memory Accesses: " << totalMemoryAccesses << endl;
        logFile << "Page Fault Rate: " << pageFaultRate << "%" << endl;
    }
    
    cleanup();
    return 0;
}

void parseArgs(int argc, char* argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'n':
                maxProcesses = atoi(optarg);
                if (maxProcesses > MAX_PROCESSES) maxProcesses = MAX_PROCESSES;
                break;
            case 's':
                simultaneousProcesses = atoi(optarg);
                break;
            case 't':
                timeLimitForChildren = atoi(optarg);
                break;
            case 'i':
                launchInterval = atoi(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                printUsage(argv[0]);
                exit(1);
        }
    }
}

void printUsage(const char* progName)
{
    cout << "Usage: " << progName << " [-h] [-n proc] [-s simul] [-t timeLimitForChildren] "
         << "[-i fractionOfSecondToLaunchChildren] [-f logfile]" << endl;
    cout << "  -h: Display this help message" << endl;
    cout << "  -n: Maximum number of processes (default: 18)" << endl;
    cout << "  -s: Maximum simultaneous processes (default: 5)" << endl;
    cout << "  -t: Time limit for children in ms (default: 250000)" << endl;
    cout << "  -i: Launch interval in nanoseconds (default: 100000)" << endl;
    cout << "  -f: Log file name (default: logfile.txt)" << endl;
}

void initializeSharedMemory()
{
    // shared memory
    key_t shmkey = ftok(".", 'S');
    shmid = shmget(shmkey, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    sharedMem = (SharedMemory*)shmat(shmid, nullptr, 0);
    if (sharedMem == (void*)-1) {
        perror("shmat");
        exit(1);
    }
    // clock
    sharedMem->clock.seconds = 0;
    sharedMem->clock.nanoseconds = 0;
    
    // process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        sharedMem->processTable[i].inUse = false;
        sharedMem->processTable[i].processId = i;
        sharedMem->processTable[i].totalMemoryAccesses = 0;
        sharedMem->processTable[i].pageFaults = 0;
        sharedMem->processTable[i].totalAccessTime = 0;
        
        for (int j = 0; j < PAGES_PER_PROCESS; j++) {
            sharedMem->processTable[i].pageTable[j].frameNumber = -1;
            sharedMem->processTable[i].pageTable[j].valid = false;
            sharedMem->processTable[i].pageTable[j].dirty = false;
        }
    }
    // frame table
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        sharedMem->frameTable[i].occupied = false;
        sharedMem->frameTable[i].dirtyBit = false;
        sharedMem->frameTable[i].processId = -1;
        sharedMem->frameTable[i].pageNumber = -1;
        sharedMem->frameTable[i].loadTime = 0;
    }
    // creating message queue
    key_t msgkey = ftok(".", 'M');
    msgid = msgget(msgkey, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        cleanup();
        exit(1);
    }
}

void addTime(unsigned int ns) {
    sharedMem->clock.nanoseconds += ns;
    while (sharedMem->clock.nanoseconds >= 1000000000)
    {
        sharedMem->clock.seconds++;
        sharedMem->clock.nanoseconds -= 1000000000;
    }
}

unsigned long long getTimeInNanoseconds()
{
    return (unsigned long long)sharedMem->clock.seconds * 1000000000ULL + 
           sharedMem->clock.nanoseconds;
}

void launchChild()
{
    // Looking for a free PCB slot
    int processId = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!sharedMem->processTable[i].inUse) {
            processId = i;
            break;
        }
    }
    if (processId == -1) return;
    
    pid_t pid = fork();
    if (pid == 0) {
        // child process
        char pidStr[20];
        snprintf(pidStr, sizeof(pidStr), "%d", processId);
        execl("./user", "user", pidStr, nullptr);
        perror("execl");
        exit(1);
    } else if (pid > 0) {
        // parent process
        sharedMem->processTable[processId].pid = pid;
        sharedMem->processTable[processId].inUse = true;
        sharedMem->processTable[processId].startTime = getTimeInNanoseconds();
        childPids.push_back(pid);
        processesLaunched++;
        
        cout << "OSS: Launching process P" << processId << " (PID " << pid 
             << ") at time " << sharedMem->clock.seconds << ":" 
             << sharedMem->clock.nanoseconds << endl;
        logFile << "OSS: Launching process P" << processId << " (PID " << pid 
                << ") at time " << sharedMem->clock.seconds << ":" 
                << sharedMem->clock.nanoseconds << endl;
    } else {
        perror("fork");
    }
}

void handleMessage(Message& msg)
{
    if (msg.terminate) {
        terminateProcess(msg.processId);
    } else {
        processPageRequest(msg.processId, msg.address, msg.isWrite);
    }
}

void processPageRequest(int processId, int address, bool isWrite)
{
    totalMemoryAccesses++;
    sharedMem->processTable[processId].totalMemoryAccesses++;
    
    if (isWrite) totalWrites++;
    else totalReads++;
    
    // extract page number from address
    int pageNumber = address / PAGE_SIZE;
    
    string action = isWrite ? "write" : "read";
    cout << "OSS: P" << processId << " requesting " << action << " of address " 
         << address << " at time " << sharedMem->clock.seconds << ":" 
         << sharedMem->clock.nanoseconds << endl;
    logFile << "OSS: P" << processId << " requesting " << action << " of address " 
            << address << " at time " << sharedMem->clock.seconds << ":" 
            << sharedMem->clock.nanoseconds << endl;
    
    // check if page is in memory
    int frameNumber = sharedMem->processTable[processId].pageTable[pageNumber].frameNumber;
    
    if (frameNumber != -1 && sharedMem->processTable[processId].pageTable[pageNumber].valid) {
        // page hit
        addTime(100); // 100 nanoseconds for memory access
        
        // update dirty bit if write
        if (isWrite) {
            sharedMem->frameTable[frameNumber].dirtyBit = true;
            sharedMem->processTable[processId].pageTable[pageNumber].dirty = true;
        }
        cout << "OSS: Address " << address << " in frame " << frameNumber 
             << ", giving data to P" << processId << " at time " 
             << sharedMem->clock.seconds << ":" << sharedMem->clock.nanoseconds << endl;
        logFile << "OSS: Address " << address << " in frame " << frameNumber 
                << ", giving data to P" << processId << " at time " 
                << sharedMem->clock.seconds << ":" << sharedMem->clock.nanoseconds << endl;
        
        // send response back
        Message response;
        response.mtype = processId + 2;
        response.granted = true;
        msgsnd(msgid, &response, sizeof(Message) - sizeof(long), 0);
        
    } else {
        // page fault
        totalPageFaults++;
        sharedMem->processTable[processId].pageFaults++;
        
        cout << "OSS: Address " << address << " is not in a frame, pagefault" << endl;
        logFile << "OSS: Address " << address << " is not in a frame, pagefault" << endl;
        
        // Looking for a free frame or evict one
        int newFrame = findFreeFrame();
        if (newFrame == -1) {
            newFrame = evictFrameFIFO();
        }
        // calculate unblock time (current time + 14ms)
        unsigned long long unblockTime = getTimeInNanoseconds() + DISK_IO_TIME_NS;
        
        // adding process to blocked queue
        addToBlockedQueue(processId, address, isWrite, unblockTime);
        
        cout << "OSS: Clearing frame " << newFrame << " and swapping in P" 
             << processId << " page " << pageNumber << endl;
        logFile << "OSS: Clearing frame " << newFrame << " and swapping in P" 
                << processId << " page " << pageNumber << endl;
        
        // update frame table
        sharedMem->frameTable[newFrame].occupied = true;
        sharedMem->frameTable[newFrame].processId = processId;
        sharedMem->frameTable[newFrame].pageNumber = pageNumber;
        sharedMem->frameTable[newFrame].loadTime = getTimeInNanoseconds();
        sharedMem->frameTable[newFrame].dirtyBit = false;
        
        // update page table
        sharedMem->processTable[processId].pageTable[pageNumber].frameNumber = newFrame;
        sharedMem->processTable[processId].pageTable[pageNumber].valid = true;
        sharedMem->processTable[processId].pageTable[pageNumber].dirty = false;
        
        if (isWrite) {
            sharedMem->frameTable[newFrame].dirtyBit = true;
            sharedMem->processTable[processId].pageTable[pageNumber].dirty = true;
        }
    }
}

int findFreeFrame()
{
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (!sharedMem->frameTable[i].occupied) {
            return i;
        }
    }
    return -1;
}

int evictFrameFIFO() {
    // frame with earliest load time (FIFO)
    int oldestFrame = 0;
    unsigned int oldestTime = sharedMem->frameTable[0].loadTime;
    
    for (int i = 1; i < TOTAL_FRAMES; i++) {
        if (sharedMem->frameTable[i].loadTime < oldestTime) {
            oldestTime = sharedMem->frameTable[i].loadTime;
            oldestFrame = i;
        }
    }
    // checking if frame is dirty
    if (sharedMem->frameTable[oldestFrame].dirtyBit) {
        cout << "OSS: Dirty bit of frame " << oldestFrame 
             << " set, adding additional time to the clock" << endl;
        logFile << "OSS: Dirty bit of frame " << oldestFrame 
                << " set, adding additional time to the clock" << endl;
        addTime(DISK_IO_TIME_NS); // Additional write-back time
    }
    // invalidate old page table entry
    int oldProcessId = sharedMem->frameTable[oldestFrame].processId;
    int oldPageNumber = sharedMem->frameTable[oldestFrame].pageNumber;
    
    if (oldProcessId != -1 && sharedMem->processTable[oldProcessId].inUse) {
        sharedMem->processTable[oldProcessId].pageTable[oldPageNumber].valid = false;
        sharedMem->processTable[oldProcessId].pageTable[oldPageNumber].frameNumber = -1;
    }
    
    return oldestFrame;
}

void addToBlockedQueue(int processId, int address, bool isWrite, unsigned long long unblockTime)
{
    BlockedProcess* newNode = new BlockedProcess;
    newNode->processId = processId;
    newNode->address = address;
    newNode->isWrite = isWrite;
    newNode->unblockTime = unblockTime;
    newNode->next = nullptr;
    
    if (blockedQueueHead == nullptr) {
        blockedQueueHead = newNode;
    } else {
        BlockedProcess* current = blockedQueueHead;
        while (current->next != nullptr) {
            current = current->next;
        }
        current->next = newNode;
    }
}

void checkBlockedProcesses()
{
    unsigned long long currentTime = getTimeInNanoseconds();
    
    BlockedProcess* prev = nullptr;
    BlockedProcess* current = blockedQueueHead;
    
    while (current != nullptr) {
        if (currentTime >= current->unblockTime) {
            // unblocking this process
            cout << "OSS: Indicating to P" << current->processId 
                 << " that " << (current->isWrite ? "write" : "read") 
                 << " has happened to address " << current->address << endl;
            logFile << "OSS: Indicating to P" << current->processId 
                    << " that " << (current->isWrite ? "write" : "read") 
                    << " has happened to address " << current->address << endl;
            
            // sending message to process
            Message response;
            response.mtype = current->processId + 2;
            response.granted = true;
            msgsnd(msgid, &response, sizeof(Message) - sizeof(long), 0);
            
            // removing from queue
            BlockedProcess* toDelete = current;
            if (prev == nullptr) {
                blockedQueueHead = current->next;
                current = blockedQueueHead;
            } else {
                prev->next = current->next;
                current = prev->next;
            }
            delete toDelete;
        } else {
            prev = current;
            current = current->next;
        }
    }
    // checking if all processes are blocked
    bool allBlocked = true;
    int activeProcesses = 0;
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (sharedMem->processTable[i].inUse) {
            activeProcesses++;
            // Check if this process is in blocked queue
            bool isBlocked = false;
            BlockedProcess* check = blockedQueueHead;
            while (check != nullptr) {
                if (check->processId == i) {
                    isBlocked = true;
                    break;
                }
                check = check->next;
            }
            if (!isBlocked) {
                allBlocked = false;
                break;
            }
        }
    }
    // if ALL blocked, advance clock to next unblock time
    if (allBlocked && blockedQueueHead != nullptr && activeProcesses > 0) {
        unsigned long long nextUnblock = blockedQueueHead->unblockTime;
        BlockedProcess* check = blockedQueueHead->next;
        while (check != nullptr) {
            if (check->unblockTime < nextUnblock) {
                nextUnblock = check->unblockTime;
            }
            check = check->next;
        }
        if (nextUnblock > currentTime) {
            unsigned long long timeDiff = nextUnblock - currentTime;
            addTime(timeDiff);
        }
    }
}

void displayMemoryLayout()
{
    cout << "\nCurrent memory layout at time " << sharedMem->clock.seconds 
         << ":" << sharedMem->clock.nanoseconds << " is:" << endl;
    cout << "        Occupied DirtyBit Process Page" << endl;
    
    logFile << "\nCurrent memory layout at time " << sharedMem->clock.seconds 
            << ":" << sharedMem->clock.nanoseconds << " is:" << endl;
    logFile << "        Occupied DirtyBit Process Page" << endl;
    
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        char occStr[10], dirtyStr[10], procStr[10], pageStr[10];
        
        if (sharedMem->frameTable[i].occupied) {
            snprintf(occStr, sizeof(occStr), "Yes");
            snprintf(dirtyStr, sizeof(dirtyStr), "%d", sharedMem->frameTable[i].dirtyBit ? 1 : 0);
            snprintf(procStr, sizeof(procStr), "%d", sharedMem->frameTable[i].processId);
            snprintf(pageStr, sizeof(pageStr), "%d", sharedMem->frameTable[i].pageNumber);
        } else {
            snprintf(occStr, sizeof(occStr), "No");
            snprintf(dirtyStr, sizeof(dirtyStr), "0");
            snprintf(procStr, sizeof(procStr), "-1");
            snprintf(pageStr, sizeof(pageStr), "-1");
        }
        cout << "Frame " << i << ": " << occStr << " " << dirtyStr 
             << " " << procStr << " " << pageStr << endl;
        logFile << "Frame " << i << ": " << occStr << " " << dirtyStr 
                << " " << procStr << " " << pageStr << endl;
    }
    // displaying active processes from table
    cout << "\nPage Tables:" << endl;
    logFile << "\nPage Tables:" << endl;
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (sharedMem->processTable[i].inUse) {
            cout << "P" << i << " page table: [";
            logFile << "P" << i << " page table: [";
            
            for (int j = 0; j < PAGES_PER_PROCESS; j++) {
                cout << " " << sharedMem->processTable[i].pageTable[j].frameNumber;
                logFile << " " << sharedMem->processTable[i].pageTable[j].frameNumber;
            }
            
            cout << " ]" << endl;
            logFile << " ]" << endl;
        }
    }
    cout << endl;
    logFile << endl;
}

void terminateProcess(int processId)
{
    if (!sharedMem->processTable[processId].inUse) return;
    
    unsigned long long avgAccessTime = 0;
    if (sharedMem->processTable[processId].totalMemoryAccesses > 0) {
        avgAccessTime = sharedMem->processTable[processId].totalAccessTime / 
                       sharedMem->processTable[processId].totalMemoryAccesses;
    }
    cout << "OSS: P" << processId << " terminating at time " 
         << sharedMem->clock.seconds << ":" << sharedMem->clock.nanoseconds << endl;
    cout << "     Total Memory Accesses: " << sharedMem->processTable[processId].totalMemoryAccesses << endl;
    cout << "     Page Faults: " << sharedMem->processTable[processId].pageFaults << endl;
    cout << "     Effective Memory Access Time: " << avgAccessTime << " ns" << endl;
    
    logFile << "OSS: P" << processId << " terminating at time " 
            << sharedMem->clock.seconds << ":" << sharedMem->clock.nanoseconds << endl;
    logFile << "     Total Memory Accesses: " << sharedMem->processTable[processId].totalMemoryAccesses << endl;
    logFile << "     Page Faults: " << sharedMem->processTable[processId].pageFaults << endl;
    logFile << "     Effective Memory Access Time: " << avgAccessTime << " ns" << endl;
    
    // freeing all frames used by this process
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (sharedMem->frameTable[i].occupied && 
            sharedMem->frameTable[i].processId == processId) {
            sharedMem->frameTable[i].occupied = false;
            sharedMem->frameTable[i].dirtyBit = false;
            sharedMem->frameTable[i].processId = -1;
            sharedMem->frameTable[i].pageNumber = -1;
        }
    }
    // clearing page table
    for (int i = 0; i < PAGES_PER_PROCESS; i++) {
        sharedMem->processTable[processId].pageTable[i].frameNumber = -1;
        sharedMem->processTable[processId].pageTable[i].valid = false;
        sharedMem->processTable[processId].pageTable[i].dirty = false;
    }
    sharedMem->processTable[processId].inUse = false;
}

void cleanup()
{
    // kill any remaining children
    for (pid_t pid : childPids) {
        kill(pid, SIGTERM);
    }
    // waiting for all children
    while (wait(nullptr) > 0);
    
    // cleaning blocked queue
    while (blockedQueueHead != nullptr) {
        BlockedProcess* temp = blockedQueueHead;
        blockedQueueHead = blockedQueueHead->next;
        delete temp;
    }
    // detach and remove shared memory
    if (sharedMem != nullptr) {
        shmdt(sharedMem);
    }
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, nullptr);
    }
    // remove message queue
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, nullptr);
    }
    if (logFile.is_open()) {
        logFile.close();
    }
    cout << "OSS: Cleanup complete" << endl;
}

void signalHandler(int sig)
{
    cout << "\nOSS: Received signal " << sig << ", terminating..." << endl;
    
    cleanup();
    exit(0);
}