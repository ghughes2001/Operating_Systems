/*
Authour: Grant Hughes
Date: September 3, 2025
File: This program manages child process with a message queue
*/

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <cstring>
#include <cstdlib>
#include <getopt.h>
#include <algorithm>

using namespace std;

// shared memory structure for simulated clock
struct SimulatedClock 
{
    int seconds;
    int nanoSeconds;
};

// Process Control Block
struct PCB 
{
    int occupied;        // 1 if occupied, 0 if free
    pid_t pid;          // process id
    int startSeconds;   // start time in seconds
    int startNano;      // start time in nano-seconds
    int messagesSent;   // total messages sent to this process
};

// Message structure
struct Message 
{
    long mtype;     // message type (PID)
    int status;     // 1 = running, 0 = terminating
};

// global variables
int shmid = -1;
int msgid = -1;
SimulatedClock* sharedClock = nullptr;
vector<PCB> processTable(20);
ofstream logFile;

