/*
Author: GRant Hughes
Date: August 28, 2025
File: Acess the pid/ppid and prints the requried iterations before and after sleeping for 1 second
*/

#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/types.h>

using namespace std;

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        cerr << "Usage: " <<  argv[0] << "iterations" << endl;
        return 1;
    }
    int iterations = 0;
    try {
        iterations = stoi(argv[1]);
    } catch (const exception& e) {
        cerr << "Error: iterations are an invalid format" << endl;
        return 1;
    }
    if (iterations <= 0)
    {
        cerr << "Error: Iterations must be postive" << endl;
        return 1;
    }
    // grabbing id's
    pid_t pid = getpid();
    pid_t ppid = getppid();

    for (int i = 0; i <= iterations; i++)
    {
        // Before 1 second sleep
        cout << "USER PID: " << pid << " PPID: " << ppid << " Iteration: " << i << "before sleeping" << endl;

        // 1 second sleep
        sleep(1);

        // After 1 second sleep
        cout << "USER PID: " << pid << " PPID: " << ppid << " Iteration: " << i << "after sleeping" << endl;
    }
    return 0;
}