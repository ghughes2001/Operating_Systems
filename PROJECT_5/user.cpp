/*
Author: Grant Hughes
Date: Nov 4, 2025
File: Where child processes request/release resources
*/

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <ctime>
#include <cerrno>
#include "shared.h"

using namespace std;

int main(int argc, char* argv[]) {
    // cerr << "USER PROCESS STARTED with " << argc << " arguments" << endl;
    // cerr.flush();
    
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <processIndex> <timeLimit>\n";
        // cerr.flush();
        return 1;
    }
    
    int processIndex = atoi(argv[1]);
    int timeLimit = atoi(argv[2]);
    
    // cerr << "USER: processIndex=" << processIndex << " timeLimit=" << timeLimit << endl;
    // cerr.flush();
    
    // seed random number generator
    srand(time(nullptr) ^ (getpid() << 16));

    // cerr << "USER: Attaching to shared memory..." << endl;
    // cerr.flush();
    
    // attach to shared memory
    int shmClock = shmget(CLOCK_KEY, sizeof(Clock), 0666);
    int shmResource = shmget(RESOURCE_KEY, sizeof(ResourceDescriptor) * NUM_RESOURCES, 0666);
    int shmPCB = shmget(PROCESS_TABLE_KEY, sizeof(PCB) * MAX_PROCESSES, 0666);
    int msgQueue = msgget(MSG_QUEUE_KEY, 0666);
    
    // cerr << "USER: shmget results: clock=" << shmClock << " resource=" << shmResource 
    //      << " pcb=" << shmPCB << " msg=" << msgQueue << endl;
    
    if (shmClock == -1 || shmResource == -1 || shmPCB == -1 || msgQueue == -1) {
        cerr << "P" << processIndex << ": Failed to attach to shared memory\n";
        perror("shmget/msgget error");

        return 1;
    }
    // cerr << "USER: Calling shmat..." << endl;
    
    Clock* sharedClock = (Clock*)shmat(shmClock, nullptr, 0);
    ResourceDescriptor* resources = (ResourceDescriptor*)shmat(shmResource, nullptr, 0);
    PCB* processTable = (PCB*)shmat(shmPCB, nullptr, 0);
    
    // cerr << "USER: shmat complete, pointers: clock=" << sharedClock 
    //      << " resources=" << resources << " processTable=" << processTable << endl;
    
    if (sharedClock == (void*)-1 || resources == (void*)-1 || processTable == (void*)-1) {
        cerr << "P" << processIndex << ": shmat failed\n";
        perror("shmat error");

        return 1;
    }
    // cerr << "USER: Successfully attached to all shared memory" << endl;
    // cerr.flush();
    
    // delay for parent to finish PCB initialization
    usleep(1000);  // Just 1ms
    
    // if clock seems invalid, wait a bit more
    if (sharedClock->seconds > 100000) {
        // cerr << "P" << processIndex << " waiting for clock initialization..." << endl;
        usleep(50000);
    }
    // tracking my resources
    int myResources[NUM_RESOURCES] = {0};
    
    // calculating termination time
    unsigned int terminateSeconds = processTable[processIndex].startSeconds + timeLimit;
    unsigned int terminateNanoSeconds = processTable[processIndex].startNanoSeconds;
    
    // calculating next action time (random 0-100ms from current clock)
    unsigned int currentSeconds = sharedClock->seconds;
    unsigned int currentNanoSeconds = sharedClock->nanoSeconds;
    
    unsigned int nextActionSeconds = currentSeconds;
    unsigned int nextActionNanoSeconds = currentNanoSeconds + (rand() % 100000000);
    while (nextActionNanoSeconds >= 1000000000) {
        nextActionSeconds++;
        nextActionNanoSeconds -= 1000000000;
    }
    // cerr << "P" << processIndex << " initialized: will terminate at " 
    //      << terminateSeconds << ":" << terminateNanoSeconds 
    //      << ", first action at " << nextActionSeconds << ":" << nextActionNanoSeconds 
    //      << " (current time: " << currentSeconds << ":" << currentNanoSeconds << ")" << endl;
    
    while (1) {
        // checking if time to terminate
        if (timePassed(sharedClock, terminateSeconds, terminateNanoSeconds)) {
            // cerr << "P" << processIndex << " reached time limit, terminating" << endl;
            // cerr.flush();
            
            Message msg;
            msg.mtype = 1;  // message type must be > 0
            msg.requestType = MSG_TERMINATE;
            msg.processIndex = processIndex;
            memcpy(msg.resources, myResources, sizeof(int) * NUM_RESOURCES);
            msgsnd(msgQueue, &msg, sizeof(Message) - sizeof(long), 0);
            break;
        }
        
        // checking if time for next action
        if (timePassed(sharedClock, nextActionSeconds, nextActionNanoSeconds)) {
            // cerr << "P" << processIndex << " taking action at " 
            //      << sharedClock->seconds << ":" << sharedClock->nanoSeconds << endl;
            
            // 60% chance to request, 40% chance to release
            bool shouldRequest = (rand() % 100) < 60;
            
            if (shouldRequest) {
                // random resource to request
                int resourceToRequest = rand() % NUM_RESOURCES;
                int amountToRequest = 1 + (rand() % 2);  // Request 1-2 instances
                
                // we don't exceed maximum instances
                if (myResources[resourceToRequest] + amountToRequest > INSTANCES_PER_RESOURCE) {
                    amountToRequest = INSTANCES_PER_RESOURCE - myResources[resourceToRequest];
                }
                
                if (amountToRequest > 0) {
                    // checking if this violates resource ordering
                    bool needsMassRelease = false;
                    int releaseResources[NUM_RESOURCES] = {0};
                    
                    // find if we have any resources numbered higher than what we're requesting
                    // and we also have resources numbered lower
                    for (int i = 0; i < resourceToRequest; i++) {
                        if (myResources[i] > 0) {
                            // lower numbered resource
                            // checking if we have any higher numbered ones
                            for (int j = resourceToRequest + 1; j < NUM_RESOURCES; j++) {
                                if (myResources[j] > 0) {
                                    needsMassRelease = true;
                                    break;
                                }
                            }
                            if (needsMassRelease) break;
                        }
                    }
                    
                    if (needsMassRelease) {
                        // release all resources numbered less than what we want
                        for (int i = 0; i < resourceToRequest; i++) {
                            releaseResources[i] = myResources[i];
                        }
                        // sending release message
                        Message releaseMsg;
                        releaseMsg.mtype = 1;  // Must be > 0
                        releaseMsg.requestType = MSG_RELEASE;
                        releaseMsg.processIndex = processIndex;
                        memcpy(releaseMsg.resources, releaseResources, sizeof(int) * NUM_RESOURCES);
                        msgsnd(msgQueue, &releaseMsg, sizeof(Message) - sizeof(long), 0);
                        
                        // waiting for acknowledgment
                        Message ack;
                        msgrcv(msgQueue, &ack, sizeof(Message) - sizeof(long), getpid(), 0);
                        
                        // updating my resource counts
                        for (int i = 0; i < NUM_RESOURCES; i++) {
                            myResources[i] -= releaseResources[i];
                        }
                        // request everything back plus the new resource
                        int requestResources[NUM_RESOURCES] = {0};
                        for (int i = 0; i < resourceToRequest; i++) {
                            requestResources[i] = releaseResources[i];
                        }
                        requestResources[resourceToRequest] = amountToRequest;
                        
                        Message requestMsg;
                        requestMsg.mtype = 1;  // Must be > 0
                        requestMsg.requestType = MSG_REQUEST;
                        requestMsg.processIndex = processIndex;
                        memcpy(requestMsg.resources, requestResources, sizeof(int) * NUM_RESOURCES);
                        msgsnd(msgQueue, &requestMsg, sizeof(Message) - sizeof(long), 0);
                        
                        // waiting for grant
                        Message grant;
                        msgrcv(msgQueue, &grant, sizeof(Message) - sizeof(long), getpid(), 0);
                        
                        // updating my resources
                        for (int i = 0; i < NUM_RESOURCES; i++) {
                            myResources[i] += requestResources[i];
                        }
                    } else {
                        // simple request
                        int requestResources[NUM_RESOURCES] = {0};
                        requestResources[resourceToRequest] = amountToRequest;
                        
                        Message requestMsg;
                        requestMsg.mtype = 1;  // Must be > 0
                        requestMsg.requestType = MSG_REQUEST;
                        requestMsg.processIndex = processIndex;
                        memcpy(requestMsg.resources, requestResources, sizeof(int) * NUM_RESOURCES);
                        
                        // cerr << "P" << processIndex << " sending REQUEST for R" << resourceToRequest 
                        //      << ":" << amountToRequest << endl;
                        // cerr << "DEBUG: mtype=" << requestMsg.mtype << " requestType=" << requestMsg.requestType 
                        //      << " processIndex=" << requestMsg.processIndex << endl;
                        // cerr << "DEBUG: sizeof(Message)=" << sizeof(Message) << " sizeof(long)=" << sizeof(long) 
                        //      << " sending " << (sizeof(Message) - sizeof(long)) << " bytes" << endl;
                        // cerr << "DEBUG: msgQueue ID=" << msgQueue << endl;
                        // cerr.flush();
                        
                        int msgsnd_result = msgsnd(msgQueue, &requestMsg, sizeof(Message) - sizeof(long), 0);
                        
                        // cerr << "DEBUG: msgsnd returned " << msgsnd_result << endl;
                        // cerr.flush();
                        
                        if (msgsnd_result == -1) {
                            perror("P: msgsnd failed");
                            // cerr << "P" << processIndex << " msgsnd failed! errno=" << errno << " (" << strerror(errno) << ")" << endl;
                            // struct msqid_ds buf;
                            // if (msgctl(msgQueue, IPC_STAT, &buf) == 0) {
                            //     cerr << "msgQueue stats: qnum=" << buf.msg_qnum << " qbytes=" << buf.msg_qbytes << endl;
                            // } else {
                            //     cerr << "msgctl failed to get stats" << endl;
                            // }
                            // cerr.flush();
                            break;
                        }
                        // else {
                        //     cerr << "P" << processIndex << " msgsnd successful, waiting for reply..." << endl;
                        //     cerr.flush();
                        // }
                        
                        // waiting for grant (might be blocked)
                        Message grant;
                        pid_t myPid = getpid();
                        
                        // cerr << "P" << processIndex << " (PID=" << myPid << ") calling msgrcv for reply (waiting for mtype=" << myPid << ")..." << endl;
                        // cerr.flush();
                        
                        int recv_result = msgrcv(msgQueue, &grant, sizeof(Message) - sizeof(long), myPid, 0);
                        
                        if (recv_result == -1) {
                            perror("P: msgrcv failed");
                            // cerr << "P" << processIndex << " msgrcv failed! errno=" << errno << " (" << strerror(errno) << ")" << endl;
                            // cerr.flush();
                            break;
                        }
                        // cerr << "P" << processIndex << " received reply (recv_result=" << recv_result << ", grant.mtype=" << grant.mtype << ")" << endl;
                        // cerr.flush();
                        
                        myResources[resourceToRequest] += amountToRequest;
                        
                        // cerr << "P" << processIndex << " got R" << resourceToRequest 
                        //      << ":" << amountToRequest << endl;
                    }
                }
            } else {
                // releasing a random resource
                int totalResources = 0;
                for (int i = 0; i < NUM_RESOURCES; i++) {
                    totalResources += myResources[i];
                }
                if (totalResources > 0) {
                    // Pick a resource we have
                    int resourceToRelease = -1;
                    for (int i = 0; i < NUM_RESOURCES; i++) {
                        if (myResources[i] > 0) {
                            if (rand() % 2 == 0 || resourceToRelease == -1) {
                                resourceToRelease = i;
                            }
                        }
                    }
                    if (resourceToRelease != -1) {
                        int amountToRelease = 1 + (rand() % myResources[resourceToRelease]);
                        
                        int releaseResources[NUM_RESOURCES] = {0};
                        releaseResources[resourceToRelease] = amountToRelease;
                        
                        Message releaseMsg;
                        releaseMsg.mtype = 1;  // Must be > 0
                        releaseMsg.requestType = MSG_RELEASE;
                        releaseMsg.processIndex = processIndex;
                        memcpy(releaseMsg.resources, releaseResources, sizeof(int) * NUM_RESOURCES);
                        msgsnd(msgQueue, &releaseMsg, sizeof(Message) - sizeof(long), 0);
                        
                        // wating for acknowledgment
                        Message ack;
                        msgrcv(msgQueue, &ack, sizeof(Message) - sizeof(long), getpid(), 0);
                        
                        myResources[resourceToRelease] -= amountToRelease;
                        
                        // cerr << "P" << processIndex << " released R" << resourceToRelease 
                        //      << ":" << amountToRelease << endl;
                    }
                }
            }
            // calculate next action time
            nextActionSeconds = sharedClock->seconds;
            nextActionNanoSeconds = sharedClock->nanoSeconds + (rand() % 100000000);
            while (nextActionNanoSeconds >= 1000000000) {
                nextActionSeconds++;
                nextActionNanoSeconds -= 1000000000;
            }
        }
        // sleep to avoid busy waiting
        usleep(100);  // 100 microseconds for responsiveness
    }
    // detach shared memory
    shmdt(sharedClock);
    shmdt(resources);
    shmdt(processTable);
    
    return 0;
}