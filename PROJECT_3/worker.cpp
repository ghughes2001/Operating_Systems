/*
Author: GRant Hughes
Date: October 8, 2025
File:  Worker process with message queue communication
*/

#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <cstdlib>
#include <cstring>

using namespace std;

// shared memory structure
struct SimulatedClock {
    int seconds;
    int nanoSeconds;
};

// message structure
struct Message {
    long mtype;  // message type (PID)
    int status;  // 1 = running, 0 = terminating
};

