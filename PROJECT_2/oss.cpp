/*
Authour: Grant Hughes
Date: September 2, 2025
File: The OSS process like..
- parsing command line arguments
- creates/manages shared memory
- signal handling
*/

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <cstring>
#include <cstdlib>
#include <getopt.h>
#include <algorithm>

using namespace std;

struct PCB {
    int occupied; // 1 if yes, 0 if no
    pid_t pid; // id
    int startSeconds; // start time in seconds
    int startNano; // star time in nano seconds
};

// shared memory

struct SimulatedClock {
    int seconds;
    int nanoSeconds;
};

// global variables
int shmid = -1;
SimulatedClock* sharedClock = nullptr;
vector<PCB> processTable(20);
vector<pid_t> activePids;

// professor's signal handler
void signal_handler(int sig)
{
    // code to send kill signal to all children based on their PIDs in process table
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied && processTable[i].pid > 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }
    // code to free up shared memory
    if (sharedClock != nullptr) {
        shmdt(sharedClock);
    }
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, nullptr);
    }
    exit(1);
}

void printUsage()
{
    cout << "Usage: oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren]" << endl;
    cout << "  -h: Display this help message" << endl;
    cout << "  -n proc: Total number of processes to launch" << endl;
    cout << "  -s simul: Maximum number of simultaneous processes" << endl;
    cout << "  -t time: Time limit for children (in simulated seconds, can be float)" << endl;
    cout << "  -i interval: Minimum interval between launches (in seconds, can be float)" << endl;
}

void incrementClock(SimulatedClock* clock, int incrementNano = 100000)
{
    clock->nanoSeconds += incrementNano;
    
    if (clock->nanoSeconds >= 1000000000) {
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
    cout << "OSS PID:" << getpid() << " SysClockS: " << clock->seconds << " SysClockNano: " << clock->nanoSeconds << endl;
    cout << "Process Table:" << endl;
    cout << "Entry Occupied PID    StartS StartN" << endl;
    
    for (int i = 0; i < 20; i++) {
        cout << setw(5) << i << " " 
            << setw(8) << processTable[i].occupied << " "
            << setw(6) << processTable[i].pid << " "
            << setw(6) << processTable[i].startSeconds << " "
            << setw(6) << processTable[i].startNano << endl;
    }
    cout << endl;
}

int findFreeSlot()
{
    for (int i = 0; i < 20; i++) {
        if (processTable[i].occupied == 0) {
            return i;
        }
    }
    return -1; // no free slot
}

void launchWorker(int slot, double timeLimit, SimulatedClock* clock)
{
    pid_t pid = fork();
    if (pid == 0) {
        // child process -> exec worker
        int timeLimitSeconds = (int)timeLimit;
        int timeLimitNano = (int)((timeLimit - timeLimitSeconds) * 1000000000);
        
        string secondsStr = to_string(timeLimitSeconds);
        string nanoStr = to_string(timeLimitNano);
        
        execl("./worker", "worker", secondsStr.c_str(), nanoStr.c_str(), nullptr);
        perror("execl failed");
        
        exit(1);
    } else if (pid > 0) {
        // parent process -> update process table
        processTable[slot].occupied = 1;
        processTable[slot].pid = pid;
        processTable[slot].startSeconds = clock->seconds;
        processTable[slot].startNano = clock->nanoSeconds;
        activePids.push_back(pid);
    } else {
        perror("fork failed");
    }
}

int main(int argc, char* argv[])
{
    // defaults
    int proc = 1;
    int simul = 1;
    double timeLimit = 1.0;
    double interval = 0.2;
    
    // parsingarse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1) {
        switch (opt) {
            case 'h':
                printUsage();
                return 0;
            case 'n':
                proc = atoi(optarg);
                break;
            case 's':
                simul = atoi(optarg);
                break;
            case 't':
                timeLimit = atof(optarg);
                break;
            case 'i':
                interval = atof(optarg);
                break;
            default:
                printUsage();
                return 1;
        }
    }
    cout << "OSS starting, PID:" << getpid() << " PPID:" << getppid() << endl;
    cout << "Called with:" << endl;
    cout << "-n " << proc << "-s " << simul << "-t " << timeLimit << "-i " << interval << endl;
    
    // seting up signal handlers using professor's patter code
    signal(SIGALRM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    // seting up alarm call
    alarm(60);
    
    // creating shared memory for clock
    key_t key = ftok(".", 'c');
    shmid = shmget(key, sizeof(SimulatedClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        return 1;
    }
    
    sharedClock = (SimulatedClock*)shmat(shmid, nullptr, 0);
    if (sharedClock == (SimulatedClock*)-1) {
        perror("shmat failed");
        return 1;
    }
    // initialize clock
    sharedClock->seconds = 0;
    sharedClock->nanoSeconds = 0;
    
    // initialize process table
    for (int i = 0; i < 20; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
    }
    int processesLaunched = 0;
    int activeProcesses = 0;
    int lastOutputS = -1;
    int lastOutputN = 0;
    int lastLaunchS = 0;
    int lastLaunchN = 0;
    
    // statistics
    int totalProcesses = 0;
    long long totalRunTime = 0; // in nanoseconds
    
    // main loop
    while (processesLaunched < proc || activeProcesses > 0) {
        // increment clock
        incrementClock(sharedClock);
        
        // output process table every half second
        if (sharedClock->seconds > lastOutputS || 
            (sharedClock->seconds == lastOutputS && sharedClock->nanoSeconds - lastOutputN >= 500000000)) {
            printProcessTable(sharedClock);
            lastOutputS = sharedClock->seconds;
            lastOutputN = sharedClock->nanoSeconds;
        }
        
        // check for terminated children (non-blocking)
        int status;
        pid_t terminatedPid = waitpid(-1, &status, WNOHANG);
        if (terminatedPid > 0) {
            // Find and clear the process table entry
            for (int i = 0; i < 20; i++) {
                if (processTable[i].pid == terminatedPid) {
                    // Calculate runtime
                    long long startTime = (long long)processTable[i].startSeconds * 1000000000 + processTable[i].startNano;
                    long long endTime = (long long)sharedClock->seconds * 1000000000 + sharedClock->nanoSeconds;
                    totalRunTime += (endTime - startTime);
                    totalProcesses++;
                    
                    processTable[i].occupied = 0;
                    processTable[i].pid = 0;
                    processTable[i].startSeconds = 0;
                    processTable[i].startNano = 0;
                    break;
                }
            }
            
            // remove from active pids
            activePids.erase(remove(activePids.begin(), activePids.end(), terminatedPid), activePids.end());
            activeProcesses--;
        }
        // launch new process if conditions are met
        if (processesLaunched < proc && activeProcesses < simul) {
            if (isTimeToLaunch(sharedClock, lastLaunchS, lastLaunchN, interval)) {
                int slot = findFreeSlot();
                if (slot != -1) {
                    launchWorker(slot, timeLimit, sharedClock);
                    processesLaunched++;
                    activeProcesses++;
                    lastLaunchS = sharedClock->seconds;
                    lastLaunchN = sharedClock->nanoSeconds;
                }
            }
        }
    }
    cout << "OSS PID:" << getpid() << " Terminating" << endl;
    cout << totalProcesses << " workers were launched and terminated" << endl;
    
    int totalSeconds = totalRunTime / 1000000000;
    int totalNano = totalRunTime % 1000000000;
    cout << "Workers ran for a combined time of " << totalSeconds << " seconds " << totalNano << " nanoseconds." << endl;
    
    // cleanup
    shmdt(sharedClock);
    shmctl(shmid, IPC_RMID, nullptr);
    
    return 0;
}