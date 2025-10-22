/*
Auhtor: Grant Hughes
Date: October 21, 20225
File: Main implementation of process scheduling (fork, shared memory, etc)
*/

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctime>
#include <fstream>
#include <vector>
#include <algorithm>

struct PCB {
    int occupied;           // true or false
    pid_t pid;              // process id of this child
    int startSeconds;       // time when it was created
    int startNano;          // time when it was created
    int serviceTimeSeconds; // total seconds scheduled
    int serviceTimeNano;    // total nanoseconds scheduled
    int eventWaitSec;       // when does its event happen?
    int eventWaitNano;      // when does its event happen?
    int blocked;            // is waiting on event?
    int totalBurstSec;      // total CPU burst time allocated
    int totalBurstNano;     // total CPU burst time allocated
};

// shared memory structure
struct SharedMemory {
    unsigned int clockSeconds;
    unsigned int clockNano;
    PCB processTable[20];
};

// message structure for IPC
struct Message {
    long mtype;       // message type (PID)
    int quantum;      // time quantum or time used
};

// global variables
int shmid = -1;
int msgid = -1;
SharedMemory* shm = nullptr;
std::ofstream logFile;
int logLines = 0;
const int MAX_LOG_LINES = 10000;