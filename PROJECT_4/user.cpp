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

// Process Control Block structure (must match oss.cpp)
struct PCB {
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

// Shared memory structure (must match oss.cpp)
struct SharedMemory {
    unsigned int clockSeconds;
    unsigned int clockNano;
    PCB processTable[20];
};

// Message structure for IPC (must match oss.cpp)
struct Message {
    long mtype;
    int quantum;
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <pcb_index> <time_limit>\n";
        return 1;
    }
    int pcbIndex = atoi(argv[1]);
    
    // Seed random number generator with unique seed based on PID
    srand(getpid());
    
    // Attach to shared memory
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
    // Attach to message queue
    key_t msgkey = ftok(".", 'M');
    int msgid = msgget(msgkey, 0666);
    if (msgid == -1) {
        perror("msgget");
        shmdt(shm);
        return 1;
    }
    // Get this process's PCB
    PCB& myPCB = shm->processTable[pcbIndex];
    
    // Get parent PID for sending messages back
    pid_t parentPid = getppid();
    
    // Probability of being blocked (30% chance)
    const double BLOCK_PROBABILITY = 0.30;
    
    // Track total time used
    int totalTimeUsedSec = 0;
    int totalTimeUsedNano = 0;
    
    // wait for scheduling messages
    while (true) {
        // Wait for message from OSS
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
        
        // prepare response
        Message response;
        response.mtype = parentPid;
        
        // Determine what to do
        if (remainingNano <= 0) {
            // Already used all burst time, terminate immediately
            response.quantum = -1;
            msgsnd(msgid, &response, sizeof(response.quantum), 0);
            shmdt(shm);
            exit(0);
        } else if (remainingNano <= quantum) {
            // Case 3: Terminate after using remaining time
            response.quantum = -(int)remainingNano;
            
            // Send response and exit
            if (msgsnd(msgid, &response, sizeof(response.quantum), 0) == -1) {
                perror("msgsnd");
            }
            shmdt(shm);
            exit(0);
        } else {
            // Still have more time than one quantum
            // Check if we should be blocked
            double randVal = (double)rand() / RAND_MAX;
            
            if (randVal < BLOCK_PROBABILITY) {
                // Case 2: Use part of quantum and become blocked
                int timeUsed = rand() % quantum + 1;
                response.quantum = timeUsed;
                
                totalTimeUsedNano += timeUsed;
                if (totalTimeUsedNano >= 1000000000) {
                    totalTimeUsedSec++;
                    totalTimeUsedNano -= 1000000000;
                }
            } else {
                // Case 1: Use full quantum
                response.quantum = quantum;
                
                totalTimeUsedNano += quantum;
                if (totalTimeUsedNano >= 1000000000) {
                    totalTimeUsedSec++;
                    totalTimeUsedNano -= 1000000000;
                }
            }
            // send response
            if (msgsnd(msgid, &response, sizeof(response.quantum), 0) == -1) {
                perror("msgsnd");
                break;
            }
        }
    }
    
    shmdt(shm);
    return 0;
}