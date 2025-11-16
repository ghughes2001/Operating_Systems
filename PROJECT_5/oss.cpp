/*
Author: Grant Hughes
Date: Novemebr 5, 2025
File: master process/
*/

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <vector>
#include <queue>
#include <sstream>
#include "shared.h"

using namespace std;

// cleanup variables
int shmClock = -1, shmResource = -1, shmPCB = -1, msgQueue = -1;
Clock* sharedClock = nullptr;
ResourceDescriptor* resources = nullptr;
PCB* processTable = nullptr;
ofstream logFile;
bool verbose = true;
int lineCount = 0;
const int MAX_LINES = 10000;

// statistics variables
int totalRequests = 0;
int immediateGrants = 0;
int massReleaseCount = 0;
int totalResourcesRequested = 0;
int grantedRequestsSinceTable = 0;

// blocked queue
vector<BlockedProcess> blockedQueue;

// signal handler
void signalHandler(int sig)
{
    // cout << "\nReceived signal " << sig << ". Cleaning up...\n";
    
    // killing all child processes
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].isActive && processTable[i].pid > 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }
    // cleanup
    if (logFile.is_open()) logFile.close();
    if (sharedClock) shmdt(sharedClock);
    if (resources) shmdt(resources);
    if (processTable) shmdt(processTable);
    if (shmClock != -1) shmctl(shmClock, IPC_RMID, nullptr);
    if (shmResource != -1) shmctl(shmResource, IPC_RMID, nullptr);
    if (shmPCB != -1) shmctl(shmPCB, IPC_RMID, nullptr);
    if (msgQueue != -1) msgctl(msgQueue, IPC_RMID, nullptr);
    
    exit(0);
}

void writeLog(const string& msg)
{
    if (lineCount >= MAX_LINES)
    {
        return;
    }
    cout << msg;
    if (logFile.is_open()) {
        logFile << msg;
    }
    lineCount++;
}

void printResourceTable()
{
    if (lineCount >= MAX_LINES) return;
    
    ostringstream oss;
    oss << "\nCurrent system resources at time " 
        << sharedClock->seconds << ":" 
        << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
    
    oss << "     ";
    for (int i = 0; i < NUM_RESOURCES; i++) {
        oss << "R" << i << "  ";
    }
    oss << "\n";
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].isActive) {
            oss << "P" << setw(2) << i << "   ";
            for (int j = 0; j < NUM_RESOURCES; j++) {
                oss << setw(2) << processTable[i].allocated[j] << "  ";
            }
            oss << "\n";
        }
    }
    
    oss << "Avail ";
    for (int i = 0; i < NUM_RESOURCES; i++) {
        oss << setw(2) << resources[i].availableInstances << "  ";
    }
    oss << "\n\n";
    
    writeLog(oss.str());
}

bool canGrantRequest(int processIdx, int request[NUM_RESOURCES])
{
    for (int i = 0; i < NUM_RESOURCES; i++) {
        if (request[i] > resources[i].availableInstances) {
            return false;
        }
    }
    return true;
}

void grantRequest(int processIdx, int request[NUM_RESOURCES]) 
{
    for (int i = 0; i < NUM_RESOURCES; i++) {
        if (request[i] > 0) {
            resources[i].availableInstances -= request[i];
            resources[i].allocated[processIdx] += request[i];
            processTable[processIdx].allocated[i] += request[i];
        }
    }
}

void releaseResources(int processIdx, int release[NUM_RESOURCES])
{
    for (int i = 0; i < NUM_RESOURCES; i++) {
        if (release[i] > 0) {
            resources[i].availableInstances += release[i];
            resources[i].allocated[processIdx] -= release[i];
            processTable[processIdx].allocated[i] -= release[i];
        }
    }
}

void checkBlockedQueue()
{
    for (auto it = blockedQueue.begin(); it != blockedQueue.end(); ) {
        if (canGrantRequest(it->processIndex, it->requestedResources)) {
            grantRequest(it->processIndex, it->requestedResources);
            
            ostringstream oss;
            oss << "OSS: Granting blocked process P" << it->processIndex 
                << " resources at time " << sharedClock->seconds << ":" 
                << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
            writeLog(oss.str());
            
            // sending grant message
            Message msg;
            msg.mtype = processTable[it->processIndex].pid;
            msg.requestType = MSG_GRANT;
            msg.processIndex = it->processIndex;
            msgsnd(msgQueue, &msg, sizeof(Message) - sizeof(long), 0);
            
            immediateGrants++;
            grantedRequestsSinceTable++;
            
            it = blockedQueue.erase(it);
        } else {
            ++it;
        }
    }
}

int findFreeProcessSlot()
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processTable[i].isActive) {
            return i;
        }
    }
    return -1;
}

int countActiveProcesses()
{
    int count = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].isActive) count++;
    }
    return count;
}

void launchProcess(int processIdx, int timeLimit) 
{
    // cerr << "OSS: About to fork for P" << processIdx << endl;
    
    pid_t pid = fork();

    if (pid == 0) {
        // child process
        char stderrFile[50];
        snprintf(stderrFile, sizeof(stderrFile), "user_%d.err", processIdx);
        freopen(stderrFile, "w", stderr);
        setbuf(stderr, NULL);  // unbuffered
        
        // cerr << "CHILD: In child process, about to exec" << endl;
        
        char idxStr[10], timeLimitStr[10];
        snprintf(idxStr, sizeof(idxStr), "%d", processIdx);
        snprintf(timeLimitStr, sizeof(timeLimitStr), "%d", timeLimit);
        
        // grabbing absolute path to user executable
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            char userPath[1100];
            snprintf(userPath, sizeof(userPath), "%s/user", cwd);
            // cerr << "CHILD: Executing " << userPath << " with args: " << idxStr << " " << timeLimitStr << endl;
            execl(userPath, "user", idxStr, timeLimitStr, (char*)nullptr);
        } else {
            // cerr << "CHILD: getcwd failed, trying relative path" << endl;
            execl("./user", "user", idxStr, timeLimitStr, (char*)nullptr);
        }
        perror("CHILD: execl failed");
        // cerr << "CHILD: execl failed! errno=" << errno << endl;
        exit(1);
    } else if (pid > 0) {
        // cerr << "OSS: Forked child with PID " << pid << endl;
        
        processTable[processIdx].pid = pid;
        processTable[processIdx].isActive = 1;
        processTable[processIdx].startSeconds = sharedClock->seconds;
        processTable[processIdx].startNanoSeconds = sharedClock->nanoSeconds;
        for (int i = 0; i < NUM_RESOURCES; i++) {
            processTable[processIdx].allocated[i] = 0;
        }
        
        // let the slow clock startup handle synchronization
        usleep(1000);  // Just 1ms
        
        ostringstream oss;
        oss << "OSS: Launching process P" << processIdx << " (PID " << pid 
            << ") at time " << sharedClock->seconds << ":" 
            << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
        writeLog(oss.str());
        
        // cerr << "Launched P" << processIdx << " with timeLimit " << timeLimit 
        //      << " at clock " << sharedClock->seconds << ":" << sharedClock->nanoSeconds << endl;
    } else {
        // fork failed
        perror("fork failed");
        // cerr << "OSS: Failed to fork process!" << endl;
    }
}

int main(int argc, char* argv[])
{
    int maxProc = 18;
    int simultaneousProc = 18;
    int timeLimit = 5;
    double launchInterval = 0.5;
    string logFileName = "log.txt";
    
    // parsing command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                cout << "Usage: " << argv[0] << " [-h] [-n proc] [-s simul] [-t timeLimit] [-i interval] [-f logfile]\n";
                return 0;
            case 'n':
                maxProc = atoi(optarg);
                if (maxProc > MAX_PROCESSES) maxProc = MAX_PROCESSES;
                break;
            case 's':
                simultaneousProc = atoi(optarg);
                if (simultaneousProc > MAX_PROCESSES) simultaneousProc = MAX_PROCESSES;
                break;
            case 't':
                timeLimit = atoi(optarg);
                break;
            case 'i':
                launchInterval = atof(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
        }
    }
    // opening log file
    logFile.open(logFileName);
    if (!logFile.is_open()) {
        cerr << "Error opening log file\n";
        return 1;
    }
    
    // signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(5);  // 5 second real-time limit
    
    // creating shared memory
    shmClock = shmget(CLOCK_KEY, sizeof(Clock), IPC_CREAT | 0666);
    shmResource = shmget(RESOURCE_KEY, sizeof(ResourceDescriptor) * NUM_RESOURCES, IPC_CREAT | 0666);
    shmPCB = shmget(PROCESS_TABLE_KEY, sizeof(PCB) * MAX_PROCESSES, IPC_CREAT | 0666);
    msgQueue = msgget(MSG_QUEUE_KEY, IPC_CREAT | 0666);
    
    // cerr << "OSS: Created IPC resources - clock=" << shmClock << " resource=" << shmResource 
    //      << " pcb=" << shmPCB << " msgQueue=" << msgQueue << endl;
    
    if (shmClock == -1 || shmResource == -1 || shmPCB == -1 || msgQueue == -1) {
        perror("Shared memory/message queue creation failed");
        return 1;
    }
    
    // attaching shared memory
    sharedClock = (Clock*)shmat(shmClock, nullptr, 0);
    resources = (ResourceDescriptor*)shmat(shmResource, nullptr, 0);
    processTable = (PCB*)shmat(shmPCB, nullptr, 0);
    
    // initializing the clock
    sharedClock->seconds = 0;
    sharedClock->nanoSeconds = 0;
    
    // initializing the resources
    for (int i = 0; i < NUM_RESOURCES; i++) {
        resources[i].totalInstances = INSTANCES_PER_RESOURCE;
        resources[i].availableInstances = INSTANCES_PER_RESOURCE;
        for (int j = 0; j < MAX_PROCESSES; j++) {
            resources[i].allocated[j] = 0;
        }
    }
    
    // initializing process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processTable[i].pid = 0;
        processTable[i].isActive = 0;
        for (int j = 0; j < NUM_RESOURCES; j++) {
            processTable[i].allocated[j] = 0;
        }
    }
    
    writeLog("OSS: System initialized\n");
    
    int processesLaunched = 0;
    unsigned int nextLaunchSeconds = 0;
    unsigned int nextLaunchNanoSeconds = 0;
    unsigned int nextTablePrintSeconds = 0;
    unsigned int nextTablePrintNanoSeconds = 500000000;  // 0.5 seconds
    
    int activeMessageCount = 0;  // track how many processes have sent messages
    
    while (processesLaunched < maxProc || countActiveProcesses() > 0) {
        // slow clock until all active processes have sent at least one message
        int activeProcs = countActiveProcesses();
        if (activeMessageCount >= activeProcs && activeProcs > 0) {
            addTime(sharedClock, 100000);  // normal: 100,000 nanoseconds = 0.1ms
        } else if (processesLaunched > 0) {
            // very slow clock until all processes are ready
            addTime(sharedClock, 1000);  // slow: 1,000 nanoseconds = 0.001ms
        }
        // static unsigned int lastSecond = 0;
        // static int msgCheckCount = 0;
        // if (sharedClock->seconds > lastSecond) {
        //     cerr << "Clock at " << sharedClock->seconds << " seconds, " 
        //          << countActiveProcesses() << " active processes, " 
        //          << msgCheckCount << " msgrcv attempts since last second" << endl;
        //     lastSecond = sharedClock->seconds;
        //     msgCheckCount = 0;
        // }
        // msgCheckCount++;
        
        // checking if we should launch a new process
        if (processesLaunched < maxProc && 
            countActiveProcesses() < simultaneousProc &&
            timePassed(sharedClock, nextLaunchSeconds, nextLaunchNanoSeconds)) {
            
            int slot = findFreeProcessSlot();
            if (slot != -1) {
                launchProcess(slot, timeLimit);
                processesLaunched++;
                
                // calculating next launch time
                unsigned int intervalNs = (unsigned int)(launchInterval * 1000000000);
                nextLaunchSeconds = sharedClock->seconds;
                nextLaunchNanoSeconds = sharedClock->nanoSeconds + intervalNs;
                while (nextLaunchNanoSeconds >= 1000000000) {
                    nextLaunchSeconds++;
                    nextLaunchNanoSeconds -= 1000000000;
                }
            }
        }
        // checking blocked queue
        checkBlockedQueue();
        
        // non-blocking message receive (mtype=1 means only receive messages sent to OSS)
        Message msg;
        
        int msgrcv_result = msgrcv(msgQueue, &msg, sizeof(Message) - sizeof(long), 1, IPC_NOWAIT);
        
        if (msgrcv_result != -1) {
            // tracking unique processes that have sent messages
            static bool processHasSentMessage[MAX_PROCESSES] = {false};
            if (!processHasSentMessage[msg.processIndex]) {
                processHasSentMessage[msg.processIndex] = true;
                activeMessageCount++;
                // cerr << "OSS: P" << msg.processIndex << " sent first message, " 
                //      << activeMessageCount << "/" << countActiveProcesses() << " processes active" << endl;
            }
            int pIdx = msg.processIndex;
            
            // cerr << "OSS received message type " << msg.requestType 
            //      << " from P" << pIdx << " (msgrcv_result=" << msgrcv_result << ")" << endl;
            
            if (msg.requestType == MSG_REQUEST) {
                totalRequests++;
                
                // counting total resources in request
                int resourceCount = 0;
                ostringstream resourceStr;

                for (int i = 0; i < NUM_RESOURCES; i++) {
                    if (msg.resources[i] > 0) {
                        totalResourcesRequested += msg.resources[i];
                        resourceCount++;
                        if (resourceCount > 1) resourceStr << ", ";
                        resourceStr << "R" << i << ":" << msg.resources[i];
                    }
                }
                // checking if this is a mass release/reacquire
                bool isMassReacquire = (resourceCount > 2);

                if (isMassReacquire)
                {
                    massReleaseCount++;
                }
                if (verbose) {
                    ostringstream oss;
                    oss << "OSS: P" << pIdx << " requesting " << resourceStr.str() 
                        << " at time " << sharedClock->seconds << ":" 
                        << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
                    writeLog(oss.str());
                }
                if (canGrantRequest(pIdx, msg.resources)) {
                    grantRequest(pIdx, msg.resources);
                    
                    if (verbose) {
                        ostringstream oss;
                        oss << "OSS: Granting P" << pIdx << " request " << resourceStr.str() 
                            << " at time " << sharedClock->seconds << ":" 
                            << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
                        writeLog(oss.str());
                    }
                    Message reply;
                    reply.mtype = processTable[pIdx].pid;
                    reply.requestType = MSG_GRANT;
                    reply.processIndex = pIdx;
                    
                    // cerr << "OSS: Sending GRANT reply to P" << pIdx << " (PID " << processTable[pIdx].pid 
                    //      << ", mtype=" << reply.mtype << ")" << endl;
                    
                    int send_result = msgsnd(msgQueue, &reply, sizeof(Message) - sizeof(long), 0);
                    
                    // if (send_result == -1) {
                    //     perror("OSS: msgsnd grant failed");
                    //     cerr << "OSS: Failed to send grant to P" << pIdx << " errno=" << errno << endl;
                    // } else {
                    //     cerr << "OSS: Grant sent successfully to P" << pIdx << endl;
                    // }
                    
                    immediateGrants++;
                    grantedRequestsSinceTable++;
                    
                    if (grantedRequestsSinceTable >= 20) {
                        printResourceTable();
                        grantedRequestsSinceTable = 0;
                    }
                } else {
                    if (verbose) {
                        ostringstream oss;
                        oss << "OSS: No instances available for P" << pIdx 
                            << ", added to block queue at time " << sharedClock->seconds << ":" 
                            << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
                        writeLog(oss.str());
                    }
                    
                    BlockedProcess bp;
                    bp.processIndex = pIdx;
                    memcpy(bp.requestedResources, msg.resources, sizeof(int) * NUM_RESOURCES);
                    blockedQueue.push_back(bp);
                }
            } else if (msg.requestType == MSG_RELEASE) {
                ostringstream resourceStr;
                int resourceCount = 0;
                for (int i = 0; i < NUM_RESOURCES; i++) {
                    if (msg.resources[i] > 0) {
                        resourceCount++;
                        if (resourceCount > 1) resourceStr << ", ";
                        resourceStr << "R" << i << ":" << msg.resources[i];
                    }
                }
                if (verbose) {
                    ostringstream oss;
                    oss << "OSS: P" << pIdx << " releasing " << resourceStr.str() 
                        << " at time " << sharedClock->seconds << ":" 
                        << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
                    writeLog(oss.str());
                }
                releaseResources(pIdx, msg.resources);
                
                Message reply;
                reply.mtype = processTable[pIdx].pid;
                reply.requestType = MSG_RELEASE_ACK;
                reply.processIndex = pIdx;
                msgsnd(msgQueue, &reply, sizeof(Message) - sizeof(long), 0);
                
            } else if (msg.requestType == MSG_TERMINATE) {
                ostringstream oss;
                oss << "OSS: P" << pIdx << " terminating and releasing all resources at time " 
                    << sharedClock->seconds << ":" 
                    << setfill('0') << setw(9) << sharedClock->nanoSeconds << "\n";
                writeLog(oss.str());
                
                releaseResources(pIdx, processTable[pIdx].allocated);
                processTable[pIdx].isActive = 0;
                
                // Decrement active message count when process terminates
                static bool processHasSentMessage[MAX_PROCESSES] = {false};
                if (processHasSentMessage[pIdx]) {
                    activeMessageCount--;
                    processHasSentMessage[pIdx] = false;
                }
                waitpid(processTable[pIdx].pid, nullptr, 0);
            }
        }
        // printing resource table every 0.5 seconds
        if (timePassed(sharedClock, nextTablePrintSeconds, nextTablePrintNanoSeconds)) {
            printResourceTable();
            nextTablePrintSeconds = sharedClock->seconds;
            nextTablePrintNanoSeconds = sharedClock->nanoSeconds + 500000000;

            while (nextTablePrintNanoSeconds >= 1000000000) {
                nextTablePrintSeconds++;
                nextTablePrintNanoSeconds -= 1000000000;
            }
        }
    }
    // final statistics
    ostringstream stats;
    stats << "\n=== Final Statistics ===\n";
    stats << "Total resources requested: " << totalResourcesRequested << "\n";
    stats << "Mass release/reacquire count: " << massReleaseCount << "\n";
    stats << "Total requests: " << totalRequests << "\n";
    stats << "Immediate grants: " << immediateGrants << "\n";
    stats << "Percentage granted immediately: " << (totalRequests > 0 ? (100.0 * immediateGrants / totalRequests) : 0) << "%\n";
    writeLog(stats.str());
    
    // cleanup
    signalHandler(0);
    
    return 0;
}