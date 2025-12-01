/*
Author: Grant Hughes
Date: December 11 2025
File: Generates memory requests
*/

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <ctime>
#include "shared.h"

using namespace std;

SharedMemory* sharedMem = nullptr;
int processId = -1;
int msgid = -1;

void cleanup();
int generateMemoryAddress();
bool shouldTerminate();

int main(int argc, char* argv[])
{
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <processId>" << endl;
        exit(1);
    }
    processId = atoi(argv[1]);
    
    // attaching to shared memory
    key_t shmkey = ftok(".", 'S');
    int shmid = shmget(shmkey, sizeof(SharedMemory), 0666);
    if (shmid == -1) {
        perror("user: shmget");
        exit(1);
    }
    sharedMem = (SharedMemory*)shmat(shmid, nullptr, 0);
    if (sharedMem == (void*)-1) {
        perror("user: shmat");
        exit(1);
    }
    
    // attaching to message queue
    key_t msgkey = ftok(".", 'M');
    msgid = msgget(msgkey, 0666);
    if (msgid == -1) {
        perror("user: msgget");
        cleanup();
        exit(1);
    }
    // random seed number generator with process-specific seed
    srand(time(nullptr) ^ (getpid() << 16));
    
    int requestCount = 0;
    int maxRequests = 50 + rand() % 100; // random between 50-150 requests
    
    while (requestCount < maxRequests)
    {
        // generatuing memory address
        int address = generateMemoryAddress();
        
        // deciding if read or write (70% read, 30% write)
        bool isWrite = (rand() % 100) < 30;
        
        // sending a request to OSS
        Message request;
        request.mtype = 1; // OSS message type
        request.processId = processId;
        request.address = address;
        request.isWrite = isWrite;
        request.terminate = false;
        
        if (msgsnd(msgid, &request, sizeof(Message) - sizeof(long), 0) == -1) {
            perror("user: msgsnd");
            break;
        }
        // waiting for response from OSS
        Message response;
        if (msgrcv(msgid, &response, sizeof(Message) - sizeof(long), 
                   processId + 2, 0) == -1) {
            perror("user: msgrcv");
            break;
        }
        
        requestCount++;
        
        // chekcing if it should terminate
        if (requestCount > 10 && shouldTerminate()) {
            break;
        }
    }
    
    // sending a termination message
    Message termMsg;
    termMsg.mtype = 1;
    termMsg.processId = processId;
    termMsg.terminate = true;
    termMsg.address = 0;
    termMsg.isWrite = false;
    
    msgsnd(msgid, &termMsg, sizeof(Message) - sizeof(long), 0);
    
    cleanup();

    return 0;
}

int generateMemoryAddress()
{
    // generating a random page number (0 to 15)
    int pageNumber = rand() % PAGES_PER_PROCESS;
    
    // generating a random offset within page (0 to 1023)
    int offset = rand() % PAGE_SIZE;
    
    // actual memory address
    int address = (pageNumber * PAGE_SIZE) + offset;
    
    return address;
}

bool shouldTerminate()
{
    // 5% chance to terminate after minimum requests
    return (rand() % 100) < 5;
}

void cleanup()
{
    if (sharedMem != nullptr) {
        shmdt(sharedMem);
    }
}