/*
Authour: Grant Hughes
Date: September 3, 2025
File: This program manages child process with a message queue
*/

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
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

void incrementClock(SimulatedClock* clock, int activeChildren) 
{
    // 250ms/number of children
    int increment = (activeChildren > 0) ? (250000000 / activeChildren) : 250000000;
    clock->nanoSeconds += increment;
    if (clock->nanoSeconds >= 1000000000) 
    {
        clock->seconds++;
        clock->nanoSeconds -= 1000000000;
    }
}

bool isTimeToLaunch(SimulatedClock* clock, int lastLaunchS, int lastLaunchN, double interval) 
{
    int intervalSeconds = (int)interval;
    int intervalNano = (int)((interval - intervalSeconds) * 1000000000);
    
    long long currentTime = (long long)clock->seconds * 1000000000 + clock->nanoSeconds;
    long long lastLaunchTime = (long long)lastLaunchS * 1000000000 + lastLaunchN;
    long long intervalTime = (long long)intervalSeconds * 1000000000 + intervalNano;
    
    return (currentTime - lastLaunchTime) >= intervalTime;
}

void printProcessTable(SimulatedClock* clock) 
{
    ostringstream oss;
    oss << "OSS PID:" << getpid() << " SysClockS: " << clock->seconds
        << " SysclockNano: " << clock->nanoSeconds << endl;
    oss << "Process Table:" << endl;
    oss << "Entry Occupied PID    StartS StartN MessagesSent" << endl;
    for (int i = 0; i < 20; i++) 
    {
        oss << setw(5) << i << " "
            << setw(8) << processTable[i].occupied << " "
            << setw(6) << processTable[i].pid << " "
            << setw(6) << processTable[i].startSeconds << " "
            << setw(6) << processTable[i].startNano << " "
            << setw(12) << processTable[i].messagesSent << endl;
    }
    oss << endl;
    log_output(oss.str());
}

int findFreeSlot() 
{
    for (int i = 0; i < 20; i++) 
    {
        if (processTable[i].occupied == 0) 
        {
            return i;
        }
    }
    return -1;
}

void launchWorker(int slot, double timeLimit, SimulatedClock* clock) 
{
    // random time between 1 and timeLimit
    int timeLimitSeconds = rand() % ((int)timeLimit) + 1;
    int timeLimitNano = rand() % 1000000000;
    
    pid_t pid = fork();
    if (pid == 0) 
    {
        // child process
        string secondsStr = to_string(timeLimitSeconds);
        string nanoStr = to_string(timeLimitNano);
        execl("./worker", "worker", secondsStr.c_str(), nanoStr.c_str(), nullptr);
        perror("execl failed");
        exit(1);
    } else if (pid > 0) 
    {
        // parent process
        processTable[slot].occupied = 1;
        processTable[slot].pid = pid;
        processTable[slot].startSeconds = clock->seconds;
        processTable[slot].startNano = clock->nanoSeconds;
        processTable[slot].messagesSent = 0;
        
        ostringstream oss;
        oss << "OSS: Generating process with PID " << pid << " and putting it in process table at index " << slot << " at time "
            << clock->seconds << ":" << clock->nanoSeconds << endl;
        log_output(oss.str());
        
        printProcessTable(clock);
    } else {
        perror("fork failed");
    }
}