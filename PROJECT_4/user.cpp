/*
Auhtor: Grant Hughes
Date: October 21, 20225
File: Simulates the MAIN(oss.cpp)
*/

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

// from oss.cpp
struct PCB
{
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int serviceTimeSeconds;
    int serviceTimeNano;
    int eventWaitSec;
    int eventWaitNano;
    int blocked;
    int totalBurstSec;
    int totalBurstNano;
};

struct SharedMemory
{
    unsigned int clockSeconds;
    unsigned int clockNano;
    PCB processTable[20];
};

struct Message
{
    long mtype;
    int quantum;
};

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <pcb_index> <time_limit>\n";
        return 1;
    }
    int pcbIndex = atoi(argv[1]);
    double timeLimit = atof(argv[2]);
    
    // random number generator with unique seed based on PID
    srand(getpid());
    
    // attatching to shared memory
    key_t shmkey = ftok(".", 'S');
    int shmid = shmget(shmkey, sizeof(SharedMemory), 0666);
    if (shmid == -1) {
        perror("shmget");
        return 1;
    }
    SharedMemory* shm = (SharedMemory*)shmat(shmid, nullptr, 0);
    if (shm == (void*)-1) {
        perror("shmat");
        return 1;
    }
    // attaching to message queue
    key_t msgkey = ftok(".", 'M');
    int msgid = msgget(msgkey, 0666);
    if (msgid == -1) {
        perror("msgget");
        shmdt(shm);
        return 1;
    }
    // getting this process's PCB
    PCB& myPCB = shm->processTable[pcbIndex];
    // getting parent PID
    pid_t parentPid = getppid();
    // Chances of being blocked
    const double BLOCK_PROBABILITY = 0.30;
    // tracking total time used
    int totalTimeUsedSec = 0;
    int totalTimeUsedNano = 0;
    
    //wait for scheduling messages
    while (true) {
        // wait for OSS
        Message msg;
        if (msgrcv(msgid, &msg, sizeof(msg.quantum), getpid(), 0) == -1) {
            perror("msgrcv");
            break;
        }
        int quantum = msg.quantum;
        
        // remaining burst time
        long long totalBurstNano = myPCB.totalBurstSec * 1000000000LL + myPCB.totalBurstNano;
        long long usedNano = totalTimeUsedSec * 1000000000LL + totalTimeUsedNano;
        long long remainingNano = totalBurstNano - usedNano;
        
        // response
        Message response;
        response.mtype = parentPid;
        
        // Process Scheduling
        if (remainingNano <= quantum) {
            // case 3: Terminate after using remaining time
            response.quantum = -(int)remainingNano; // Negative indicates termination
            
            // sending response and exiting
            msgsnd(msgid, &response, sizeof(response.quantum), 0);
            shmdt(shm);
            exit(0);
        } else {
            // checking if we should be blocked
            double randVal = (double)rand() / RAND_MAX;
            
            if (randVal < BLOCK_PROBABILITY) {
                // case 2: Use part of quantum and become blocked
                int timeUsed = rand() % quantum + 1; // using random portion
                response.quantum = timeUsed;
                
                totalTimeUsedNano += timeUsed;
                if (totalTimeUsedNano >= 1000000000) {
                    totalTimeUsedSec++;
                    totalTimeUsedNano -= 1000000000;
                }
            } else {
                // case 1: using full quantum
                response.quantum = quantum;
                
                totalTimeUsedNano += quantum;
                if (totalTimeUsedNano >= 1000000000) {
                    totalTimeUsedSec++;
                    totalTimeUsedNano -= 1000000000;
                }
            }
            // sending response
            if (msgsnd(msgid, &response, sizeof(response.quantum), 0) == -1) {
                perror("msgsnd");
                break;
            }
        }
    }
    shmdt(shm);
    return 0;
}