/*
Auhtor: Grant Hughes
Date: October 21, 20225
File: Main program
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

// Process Control Block structure
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

// message structure
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

// prototypes
void signalHandler(int signum);
void cleanupResources();
void setupSignalHandlers();
void incrementClock(unsigned int& sec, unsigned int& nano, int addNano);
void writeLog(const std::string& message);
int findEmptyPCB(SharedMemory* shm);
double calculatePriority(const PCB& pcb, unsigned int clockSec, unsigned int clockNano);
int selectProcessToSchedule(SharedMemory* shm);
void checkBlockedProcesses(SharedMemory* shm);
void printProcessTable(SharedMemory* shm);

int main(int argc, char* argv[]) {
    // defaults
    int maxProc = 1;
    int maxSimul = 1;
    double timeLimit = 2.0;
    double launchInterval = 0.5;
    std::string logFileName = "log.txt";
    
    // parsing command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                std::cout << "Usage: " << argv[0] << " [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i fractionOfSecondToLaunchChildren] [-f logfile]\n";
                return 0;
            case 'n':
                maxProc = atoi(optarg);
                break;
            case 's':
                maxSimul = atoi(optarg);
                break;
            case 't':
                timeLimit = atof(optarg);
                break;
            case 'i':
                launchInterval = atof(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                std::cerr << "Invalid option\n";
                return 1;
        }
    }
    // log file
    logFile.open(logFileName);
    if (!logFile.is_open()) {
        std::cerr << "Error opening log file\n";
        return 1;
    }
    // signal handlers
    setupSignalHandlers();
    
    // create shared memory
    key_t shmkey = ftok(".", 'S');
    shmid = shmget(shmkey, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        cleanupResources();
        return 1;
    }
    shm = (SharedMemory*)shmat(shmid, nullptr, 0);
    if (shm == (void*)-1) {
        perror("shmat");
        cleanupResources();
        return 1;
    }
    // initializing shared memory
    shm->clockSeconds = 0;
    shm->clockNano = 0;
    for (int i = 0; i < 20; i++) {
        shm->processTable[i].occupied = 0;
    }
    // create message queue
    key_t msgkey = ftok(".", 'M');
    msgid = msgget(msgkey, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        cleanupResources();
        return 1;
    }
    // statistics tracking
    int totalProcesses = 0;
    int processesLaunched = 0;
    unsigned int nextLaunchSec = 0;
    unsigned int nextLaunchNano = 0;
    unsigned int lastTableOutputSec = 0;
    time_t startRealTime = time(nullptr);
    double totalWaitTime = 0;
    double totalBlockedTime = 0;
    double totalCPUTime = 0;
    double totalIdleTime = 0;
    int completedProcesses = 0;
    
    writeLog("OSS: Starting simulation");
    
    // scheduling loop
    while (processesLaunched < maxProc || totalProcesses > 0) {
        if (time(nullptr) - startRealTime > 3) {
            writeLog("OSS: Terminating due to timeout");
            break;
        }
        
        // seeing if we should launch a new process
        int activeProcesses = 0;
        for (int i = 0; i < 20; i++) {
            if (shm->processTable[i].occupied) activeProcesses++;
        }
        
        bool shouldLaunch = (processesLaunched < maxProc) && (activeProcesses < maxSimul) && (shm->clockSeconds > nextLaunchSec || (shm->clockSeconds == nextLaunchSec && shm->clockNano >= nextLaunchNano));
        
        if (shouldLaunch) {
            int pcbIndex = findEmptyPCB(shm);
            if (pcbIndex != -1) {
                pid_t pid = fork();
                if (pid == 0) {
                    // child
                    char indexStr[10];
                    char timeLimitStr[20];
                    sprintf(indexStr, "%d", pcbIndex);
                    sprintf(timeLimitStr, "%.2f", timeLimit);
                    execl("./user_proc", "user_proc", indexStr, timeLimitStr, nullptr);
                    perror("execl");
                    exit(1);
                } else if (pid > 0) {
                    // parent
                    PCB& pcb = shm->processTable[pcbIndex];
                    pcb.occupied = 1;
                    pcb.pid = pid;
                    pcb.startSeconds = shm->clockSeconds;
                    pcb.startNano = shm->clockNano;
                    pcb.serviceTimeSeconds = 0;
                    pcb.serviceTimeNano = 0;
                    pcb.blocked = 0;
                    pcb.eventWaitSec = 0;
                    pcb.eventWaitNano = 0;
                    
                    // random total burst time
                    double randBurst = ((double)rand() / RAND_MAX) * timeLimit;
                    pcb.totalBurstSec = (int)randBurst;
                    pcb.totalBurstNano = (int)((randBurst - pcb.totalBurstSec) * 1e9);
                    
                    processesLaunched++;
                    totalProcesses++;
                    
                    char msg[256];
                    sprintf(msg, "OSS: Generating process with PID %d and putting it in ready queue at time %u:%09u", pid, shm->clockSeconds, shm->clockNano);
                    writeLog(msg);
                    
                    // next launch time
                    int launchIntervalNano = (int)(launchInterval * 1e9);
                    nextLaunchNano = shm->clockNano + launchIntervalNano;
                    nextLaunchSec = shm->clockSeconds;
                    if (nextLaunchNano >= 1000000000) {
                        nextLaunchSec++;
                        nextLaunchNano -= 1000000000;
                    }
                    
                    // increment clock for process creation overhead
                    incrementClock(shm->clockSeconds, shm->clockNano, 1000);
                }
            }
        }
        
        // looking for blocked processes that should be unblocked
        checkBlockedProcesses(shm);
        
        // finding a ready process
        int selectedPCB = selectProcessToSchedule(shm);
        
        if (selectedPCB != -1) {
            PCB& pcb = shm->processTable[selectedPCB];
            
            // priority for logging
            double priority = calculatePriority(pcb, shm->clockSeconds, shm->clockNano);
            
            unsigned int dispatchStartNano = shm->clockNano;
            unsigned int dispatchStartSec = shm->clockSeconds;
            
            // dispatch
            Message msg;
            msg.mtype = pcb.pid;
            msg.quantum = 10000000; // 10ms time quantum
            
            if (msgsnd(msgid, &msg, sizeof(msg.quantum), 0) == -1) {
                perror("msgsnd");
                break;
            }
            // increment clock for dispatch overhead
            incrementClock(shm->clockSeconds, shm->clockNano, rand() % 1000 + 500);
            
            unsigned int dispatchTime = (shm->clockSeconds - dispatchStartSec) * 1000000000 + (shm->clockNano - dispatchStartNano);
            
            char logMsg[256];
            sprintf(logMsg, "OSS: Dispatching process with PID %d priority %.2f from ready queue at time %u:%09u", pcb.pid, priority, shm->clockSeconds, shm->clockNano);
            writeLog(logMsg);
            sprintf(logMsg, "OSS: total time this dispatch was %u nanoseconds", dispatchTime);
            writeLog(logMsg);
            
            // waiting for response
            Message response;
            if (msgrcv(msgid, &response, sizeof(response.quantum), getpid(), 0) == -1) {
                perror("msgrcv");
                break;
            }
            int timeUsed = abs(response.quantum);
            bool terminated = (response.quantum < 0);
            bool fullQuantum = (response.quantum == msg.quantum);
            
            // updating service time
            pcb.serviceTimeNano += timeUsed;
            if (pcb.serviceTimeNano >= 1000000000) {
                pcb.serviceTimeSeconds++;
                pcb.serviceTimeNano -= 1000000000;
            }
            // incrementing clock
            incrementClock(shm->clockSeconds, shm->clockNano, timeUsed);
            totalCPUTime += timeUsed / 1e9;
            
            sprintf(logMsg, "OSS: Receiving that process with PID %d ran for %d nanoseconds", pcb.pid, timeUsed);
            writeLog(logMsg);
            
            if (terminated) {
                sprintf(logMsg, "OSS: Process with PID %d has terminated", pcb.pid);
                writeLog(logMsg);
                
                // wait time
                unsigned long long totalTime = (shm->clockSeconds - pcb.startSeconds) * 1000000000ULL + (shm->clockNano - pcb.startNano);
                unsigned long long serviceTime = pcb.serviceTimeSeconds * 1000000000ULL + pcb.serviceTimeNano;
                totalWaitTime += (totalTime - serviceTime) / 1e9;
                
                pcb.occupied = 0;
                totalProcesses--;
                completedProcesses++;
                waitpid(pcb.pid, nullptr, 0);
            } else if (fullQuantum) {
                sprintf(logMsg, "OSS: Putting process with PID %d into ready queue", pcb.pid);
                writeLog(logMsg);
            } else {
                // blocked
                sprintf(logMsg, "OSS: not using its entire time quantum");
                writeLog(logMsg);
                sprintf(logMsg, "OSS: Putting process with PID %d into blocked queue", pcb.pid);
                writeLog(logMsg);
                
                pcb.blocked = 1;
                pcb.eventWaitNano = shm->clockNano + 600000000; // 0.6 seconds
                pcb.eventWaitSec = shm->clockSeconds;

                if (pcb.eventWaitNano >= 1000000000) {
                    pcb.eventWaitSec++;
                    pcb.eventWaitNano -= 1000000000;
                }
                totalBlockedTime += 0.6;
                incrementClock(shm->clockSeconds, shm->clockNano, 5000); // overhead
            }
        } else {
            // no ready processes, incrementing clock
            if (totalProcesses > 0) {
                incrementClock(shm->clockSeconds, shm->clockNano, 100000);
                totalIdleTime += 0.0001;
            } else if (processesLaunched < maxProc) {
                // next launch time
                shm->clockSeconds = nextLaunchSec;
                shm->clockNano = nextLaunchNano;
            }
        }
        // outpt process table every 0.5 seconds
        if (shm->clockSeconds - lastTableOutputSec >= 1 || 
            (shm->clockSeconds == lastTableOutputSec && shm->clockNano >= 500000000)) {
            printProcessTable(shm);
            lastTableOutputSec = shm->clockSeconds;
        }
    }
    // final statistics
    writeLog("\n========== FINAL STATISTICS ==========");
    char statMsg[256];
    sprintf(statMsg, "Total processes completed: %d", completedProcesses);
    writeLog(statMsg);
    sprintf(statMsg, "Average wait time: %.6f seconds", completedProcesses > 0 ? totalWaitTime / completedProcesses : 0);
    writeLog(statMsg);
    sprintf(statMsg, "Average CPU utilization: %.2f%%", (totalCPUTime / (shm->clockSeconds + shm->clockNano / 1e9)) * 100);
    writeLog(statMsg);
    sprintf(statMsg, "Average blocked time per process: %.6f seconds", completedProcesses > 0 ? totalBlockedTime / completedProcesses : 0);
    writeLog(statMsg);
    sprintf(statMsg, "Total idle time: %.6f seconds", totalIdleTime);
    writeLog(statMsg);
    sprintf(statMsg, "Simulation ended at time %u:%09u", shm->clockSeconds, shm->clockNano);
    writeLog(statMsg);

    //cleaning
    cleanupResources();

    return 0;
}

void signalHandler(int signum)
{
    std::cout << "\nReceived signal " << signum << ", cleaning up...\n";
    cleanupResources();
    exit(0);
}

void setupSignalHandlers()
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGALRM, signalHandler);
}

void cleanupResources()
{
    // killing all child processes
    if (shm != nullptr) {
        for (int i = 0; i < 18; i++) {
            if (shm->processTable[i].occupied) {
                kill(shm->processTable[i].pid, SIGTERM);
                waitpid(shm->processTable[i].pid, nullptr, 0);
            }
        }
        shmdt(shm);
    }
    // reomve shared memory
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, nullptr);
    }
    // remove message queue
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, nullptr);
    }
    // closing log file
    if (logFile.is_open()) {
        logFile.close();
    }
}

void incrementClock(unsigned int& sec, unsigned int& nano, int addNano) 
{
    nano += addNano;

    if (nano >= 1000000000) {
        sec += nano / 1000000000;
        nano %= 1000000000;
    }
}

void writeLog(const std::string& message) 
{
    if (logLines < MAX_LOG_LINES && logFile.is_open()) {
        logFile << message << std::endl;
        logLines++;
    }
}

int findEmptyPCB(SharedMemory* shm)
{
    for (int i = 0; i < 18; i++) {
        if (!shm->processTable[i].occupied) {
            return i;
        }
    }
    return -1;
}

double calculatePriority(const PCB& pcb, unsigned int clockSec, unsigned int clockNano)
{
    unsigned long long totalTime = (clockSec - pcb.startSeconds) * 1000000000ULL + (clockNano - pcb.startNano);
    unsigned long long serviceTime = pcb.serviceTimeSeconds * 1000000000ULL + pcb.serviceTimeNano;
    
    if (totalTime == 0) {
        return 0.0;
    }
    return (double)serviceTime / (double)totalTime;
}

int selectProcessToSchedule(SharedMemory* shm)
{
    int selectedPCB = -1;
    double lowestPriority = 2.0;
    
    for (int i = 0; i < 18; i++) {
        if (shm->processTable[i].occupied && !shm->processTable[i].blocked) {
            double priority = calculatePriority(shm->processTable[i], shm->clockSeconds, shm->clockNano);
            if (priority < lowestPriority) {
                lowestPriority = priority;
                selectedPCB = i;
            }
        }
    }
    return selectedPCB;
}

void checkBlockedProcesses(SharedMemory* shm)
{
    for (int i = 0; i < 18; i++) {
        if (shm->processTable[i].occupied && shm->processTable[i].blocked) {
            PCB& pcb = shm->processTable[i];
            if (shm->clockSeconds > pcb.eventWaitSec ||
                (shm->clockSeconds == pcb.eventWaitSec && shm->clockNano >= pcb.eventWaitNano)) {
                pcb.blocked = 0;
                char msg[256];
                sprintf(msg, "OSS: Process with PID %d unblocked at time %u:%09u", pcb.pid, shm->clockSeconds, shm->clockNano);
                writeLog(msg);
                incrementClock(shm->clockSeconds, shm->clockNano, 3000);
            }
        }
    }
}

void printProcessTable(SharedMemory* shm)
{
    writeLog("\n========== PROCESS TABLE ==========");
    char msg[256];
    sprintf(msg, "Current time: %u:%09u", shm->clockSeconds, shm->clockNano);
    writeLog(msg);
    
    for (int i = 0; i < 18; i++) {
        if (shm->processTable[i].occupied) {
            PCB& pcb = shm->processTable[i];
            double priority = calculatePriority(pcb, shm->clockSeconds, shm->clockNano);
            sprintf(msg, "PCB[%d]: PID=%d Priority=%.4f Service=%d:%09d Blocked=%d", i, pcb.pid, priority, pcb.serviceTimeSeconds, pcb.serviceTimeNano, pcb.blocked);
            writeLog(msg);
        }
    }
    writeLog("===================================\n");
}