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

// signal handler
void signal_handler(int sig)
{
    (void)sig;
    
    // Kill all children
    for (int i = 0; i < 20; i++) 
    {
        if (processTable[i].occupied && processTable[i].pid > 0) 
        {
            kill(processTable[i].pid, SIGTERM);
        }
    }
    // cleaning shared memory
    if (sharedClock != nullptr) 
    {
        shmdt(sharedClock);
    }
    if (shmid != -1) 
    {
        shmctl(shmid, IPC_RMID, nullptr);
    }
    // cleaning message queue
    if (msgid != -1) 
    {
        msgctl(msgid, IPC_RMID, nullptr);
    }
    // closing log file
    if (logFile.is_open()) 
    {
        logFile.close();
    }
    exit(1);
}

void printUsage() 
{
    cout << "Usage: oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]" << endl;
    cout << "  -h: Display this help message" << endl;
    cout << "  -n proc: Total number of processes to launch" << endl;
    cout << "  -s simul: Maximum number of simultaneous processes" << endl;
    cout << "  -t time: Time limit for children (in simulated seconds, can be float)" << endl;
    cout << "  -i interval: Minimum interval between launches (in seconds, can be float)" << endl;
    cout << "  -f logfile: Log file name" << endl;
}

void log_output(const string& message) 
{
    cout << message;
    
    if (logFile.is_open()) 
    {
        logFile << message; //writing message in file
    }
}

