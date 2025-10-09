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

int main(int argc, char* argv[])
{
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <seconds> <nanoseconds>" << endl;
        return 1;
    }
    int maxSeconds = atoi(argv[1]);
    int maxNanoSeconds = atoi(argv[2]);

    // attching to shared memory
    key_t key = ftok(".", 'c');
    int shmid = shmget(key, sizeof(SimulatedClock), 0666);
    if (shmid == -1)
    {
        perror("shmget failed");
        return 1;
    }
    SimulatedClock* sharedClock = (SimulatedClock*)shmat(shmid, nullptr, 0);
    if (sharedClock == (SimulatedClock*)-1) {
        perror("shmat failed");
        return 1;
    }
    // attaching to message queue
    key_t msgkey = ftok(".", 'm');
    int msgid = msgget(msgkey, 0666);
    if (msgid == -1)
    {
        perror("msgget failed");
        return 1;
    }
    // caalculating termination time
    int startSeconds = sharedClock->seconds;
    int startNanoSeconds = sharedClock->nanoSeconds;
    int termSeconds = startSeconds + maxSeconds;
    int termNanoSeconds = startNanoSeconds + maxNanoSeconds;

    // nanosecond overflow
    if (termNanoSeconds >= 1000000000)
    {
        termSeconds++;
        termNanoSeconds -= 1000000000;
    }
    // output
    cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << " SysClockS: " << startSeconds << " SysclockNano: " << startNanoSeconds << endl;
    cout << "TermTimeS: " << termSeconds << " TermTimeNano: " << termNanoSeconds << endl;
    cout << "--Just Starting" << endl;
}