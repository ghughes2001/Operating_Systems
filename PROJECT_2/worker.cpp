/*
Author: Grant Hughes
Date September 24, 2025
File: Accessing/Using shared memory and outputs information after doing calculations
*/

#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstdlib>
#include <cstring>

using namespace std;

// shared memory
struct SimulatedClock {
    int seconds;
    int nanoSeconds;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <seconds> <nanoseconds>" << endl;
        return 1;
    }
    
    int maxSeconds = atoi(argv[1]);
    int maxNanoSeconds = atoi(argv[2]);
    
    cout << "Worker starting, PID:" << getpid() << " PPID:" << getppid() << endl;
    cout << "Called with:" << endl;
    cout << "Interval: " << maxSeconds << " seconds, " << maxNanoSeconds << " nanoseconds" << endl;
    
    // attach to shared memory
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
    
    // calculating termination time
    int startSeconds = sharedClock->seconds;
    int startNanoseconds = sharedClock->nanoSeconds;
    
    int termSeconds = startSeconds + maxSeconds;
    int termNanoseconds = startNanoseconds + maxNanoSeconds;
    
    // Handle nanosecond overflow
    if (termNanoseconds >= 1000000000) {
        termSeconds++;
        termNanoseconds -= 1000000000;
    }
    
    cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << endl;
    cout << "SysClockS: " << startSeconds 
         << " SysclockNano: " << startNanoseconds 
         << " TermTimeS: " << termSeconds 
         << " TermTimeNano: " << termNanoseconds << endl;
    cout << "--Just Starting" << endl;
    
    int lastSecondsReported = startSeconds;
    int secondsPassed = 0;
    
    // checking for termination time
    while (true) {
        int currentSeconds = sharedClock->seconds;
        int currentNanoseconds = sharedClock->nanoSeconds;
        
        // checking if we should report a new second
        if (currentSeconds > lastSecondsReported) {
            secondsPassed += (currentSeconds - lastSecondsReported);
            lastSecondsReported = currentSeconds;
            
            cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << endl;
            cout << "SysClockS: " << currentSeconds 
                 << " SysclockNano: " << currentNanoseconds 
                 << " TermTimeS: " << termSeconds 
                 << " TermTimeNano: " << termNanoseconds << endl;
            cout << "--" << secondsPassed << " seconds have passed since starting" << endl;
        }
        
        // checking if termination time has been reached
        if (currentSeconds > termSeconds || 
            (currentSeconds == termSeconds && currentNanoseconds >= termNanoseconds)) {
            
            cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << endl;
            cout << "SysClockS: " << currentSeconds 
                 << " SysclockNano: " << currentNanoseconds 
                 << " TermTimeS: " << termSeconds 
                 << " TermTimeNano: " << termNanoseconds << endl;
            cout << "--Terminating" << endl;
            break;
        }
    }
    
    // detach from shared memory
    shmdt(sharedClock);
    
    return 0;
}