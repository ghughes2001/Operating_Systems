/*
Author: Grant Hughes
Date Nov 3, 2025
File: Shared file for shared memoery and other resources (mnake files look neater)
*/

#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

// resource configuration
#define NUM_RESOURCES 10
#define INSTANCES_PER_RESOURCE 5
#define MAX_PROCESSES 18

// message types
#define MSG_REQUEST 1
#define MSG_RELEASE 2
#define MSG_TERMINATE 3
#define MSG_GRANT 4
#define MSG_RELEASE_ACK 5

// shared memory keys
#define CLOCK_KEY 0x1234
#define RESOURCE_KEY 0x1235
#define PROCESS_TABLE_KEY 0x1236
#define MSG_QUEUE_KEY 0x1237

// simulated clock structure
struct Clock
{
    unsigned int seconds;
    unsigned int nanoSeconds;
};

// Process Control Block
struct PCB 
{
    pid_t pid;
    int isActive;
    unsigned int startSeconds;
    unsigned int startNanoSeconds;
    int allocated[NUM_RESOURCES];  // Resources currently allocated
};

// resource descriptor
struct ResourceDescriptor
{
    int totalInstances;
    int availableInstances;
    int allocated[MAX_PROCESSES];  // Allocation per process
};

// message structure for IPC
struct Message
{
    long mtype;  // Process ID for targeted messages
    int requestType;  // MSG_REQUEST, MSG_RELEASE, MSG_TERMINATE
    int resources[NUM_RESOURCES];  // Resources being requested/released
    int processIndex;  // Index in process table
};

// blocked process queue entry
struct BlockedProcess
{
    int processIndex;
    int requestedResources[NUM_RESOURCES];
};

// function to add time
void addTime(Clock* clk, unsigned int ns)
{
    clk->nanoSeconds += ns;
    while (clk->nanoSeconds >= 1000000000) {
        clk->seconds++;
        clk->nanoSeconds -= 1000000000;
    }
}

// function to compare times
int timePassed(Clock* clk, unsigned int sec, unsigned int ns) 
{
    if (clk->seconds > sec) return 1;
    if (clk->seconds == sec && clk->nanoSeconds >= ns)
    {
        return 1;
    }

    return 0;
}

#endif