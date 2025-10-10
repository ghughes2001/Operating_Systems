/*
Author: Grant Hughes
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
struct SimulatedClock
{
    int seconds;
    int nanoSeconds;
};

// message structure
struct Message
{
    long mtype;  // message type (PID)
    int status;  // 1 = running, 0 = terminating
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <seconds> <nanoseconds>" << endl;
        return 1;
    }

    int maxSeconds = atoi(argv[1]);
    int maxNanoSeconds = atoi(argv[2]);

    // Attach to shared memory
    key_t key = ftok(".", 'c');
    int shmid = shmget(key, sizeof(SimulatedClock), 0666);
    if (shmid == -1) {
        perror("shmget failed");
        return 1;
    }

    SimulatedClock* sharedClock = (SimulatedClock*)shmat(shmid, nullptr, 0);
    if (sharedClock == (SimulatedClock*)-1) {
        perror("shmat failed");
        return 1;
    }

    // Attach to message queue
    key_t msgkey = ftok(".", 'm');
    int msgid = msgget(msgkey, 0666);
    if (msgid == -1) {
        perror("msgget failed");
        return 1;
    }

    // Calculate termination time
    int startSeconds = sharedClock->seconds;
    int startNanoSeconds = sharedClock->nanoSeconds;
    int termSeconds = startSeconds + maxSeconds;
    int termNanoSeconds = startNanoSeconds + maxNanoSeconds;

    // Handle nanosecond overflow
    if (termNanoSeconds >= 1000000000) {
        termSeconds++;
        termNanoSeconds -= 1000000000;
    }

    // Initial output
    cout << "WORKER PID:" << getpid() << " PPID:" << getppid() 
         << " SysClockS: " << startSeconds
         << " SysclockNano: " << startNanoSeconds << endl;
    cout << "TermTimeS: " << termSeconds
         << " TermTimeNano: " << termNanoSeconds << endl;
    cout << "--Just Starting" << endl;

    int messagesReceived = 0;
    Message msg;
    pid_t ossPid = getppid();  // Store OSS PID

    // wating for oss
    while (true) {
        //  message from OSS
        if (msgrcv(msgid, &msg, sizeof(msg.status), getpid(), 0) == -1) {
            perror("msgrcv failed");
            break;
        }

        messagesReceived++;

        // checking the clock
        int currentSeconds = sharedClock->seconds;
        int currentNanoSeconds = sharedClock->nanoSeconds;

        // every iteration
        cout << "WORKER PID:" << getpid() << " PPID:" << getppid() 
             << " SysClockS: " << currentSeconds
             << " SysclockNano: " << currentNanoSeconds << endl;
        cout << "TermTimeS: " << termSeconds
             << " TermTimeNano: " << termNanoSeconds << endl;
        cout << "--" << messagesReceived << " messages received from oss" << endl;

        // checking if termination time has been reached
        if (currentSeconds > termSeconds ||
            (currentSeconds == termSeconds && currentNanoSeconds >= termNanoSeconds)) {
            
            // sending termination message
            msg.mtype = ossPid;  // Send to OSS
            msg.status = 0;  // Terminating
            if (msgsnd(msgid, &msg, sizeof(msg.status), 0) == -1) {
                perror("msgsnd failed");
            }

            cout << "WORKER PID:" << getpid() << " PPID:" << getppid() 
                 << " SysClockS: " << currentSeconds
                 << " SysclockNano: " << currentNanoSeconds << endl;
            cout << "TermTimeS: " << termSeconds
                 << " TermTimeNano: " << termNanoSeconds << endl;
            cout << "--Terminating after sending message back to oss after " 
                 << messagesReceived << " received messages." << endl;
            break;
        } else {
            // sending continuing message
            msg.mtype = ossPid;  // Send to OSS
            msg.status = 1;  // Still running
            if (msgsnd(msgid, &msg, sizeof(msg.status), 0) == -1)
            {
                perror("msgsnd failed");
                break;
            }
        }
    }

    // detaching from shared memory
    shmdt(sharedClock);
    return 0;
}