/*
Authour: Grant Hughes
Date: September 3, 2025
This program manages child process
*/

#include <iostream>
#include <unistd.h>
#include <vector>
#include <sys/types.h>
#include <string>
#include <sys/wait.h>
#include <getopt.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

using namespace std;

class OSS {
    private:
        // class instance variables
        int numberOfChilds;
        int numberOfSimul;
        int iterations;
        vector<pid_t> processesRunning;
    public:
        // Constructor with default values if nohting is given
        OSS(): numberOfChilds(1), numberOfSimul(1), iterations(1) {}
        // Getter method for instance variables
        int getNumberOfChilds() const {return numberOfChilds;}
        int getNumberOfSimul() const {return numberOfSimul;}
        int getIterations() const {return iterations;}
        size_t getProcessesRunning() const { return processesRunning.size();}


        // help display
        void help(const string& name) {
            cout << "Usage: " << name << " [-h] [-n proc] [-s simul] [-t iter]" << endl;
            cout << "Options:" << endl;
            cout << "   -h     display help message and exit" << endl;
            cout << "   -n proc    Number of children to launch (DEFAULT: " << numberOfChilds << ")" << endl;
            cout << "   -s simul   Number of children running at same time (DEFAULT: " << numberOfSimul << ")" << endl;
            cout << "   -t iter    Number of iterations for each children (DEFAULT: " << iterations << ")" << endl;
            cout << "Example: " << name << " -n 5 -s 3 -t 7" << endl;
        }

        // grabbing command line option
        bool commandOptions(int argc, char* argv[]) {
            int opt;

            optind = 1; // making sure getopt starts at the first command argument each time

            while ((opt = getopt(argc, argv, "hn:s:t:")) != -1) {
                switch(opt) {
                    case 'h':
                        help(argv[0]);
                        return false; // exit
                    case 'n':
                        try {
                            numberOfChilds = stoi(optarg);
                            if (numberOfChilds <= 0) {
                                cerr << "ERROR: Number of children must be greater than 0" << endl;
                                return false; // exit
                            }
                        } catch (const exception& e) {
                            cerr << "ERROR: Invalid format for -n option" << endl;
                            return false; // exit
                        }
                        break;
                    case 's':
                        try {
                            numberOfSimul = stoi(optarg);
                            if (numberOfSimul <= 0) {
                                cerr << "ERROR: Number of simul(s) must be positive" << endl;
                                return false;
                            }
                        } catch (const exception& e) {
                            cerr << "ERROR: Invalid format for -s option";
                            return false; // exit
                        }
                        break;
                    case 't':
                        try {
                            iterations = stoi(optarg);
                            if (iterations <= 0) {
                                cerr << "ERROR: Number of iterations must be postive" << endl;
                                return false;
                            }
                        } catch (const exception& e) {
                            cerr << "ERROR: Invalid format for -t option" << endl;
                            return false; // exit
                        }
                        break;
                    case '?':
                        // getopt got error message
                        help(argv[0]);
                        return false;
                }
            }
            return true;
        }
        bool childLaunched(int children) {
            pid_t childPid = fork();

            if (childPid == 0) {
                // execute program
                string iterateString = to_string(iterations);
                execl("./user", "user", iterateString.c_str(), static_cast<char*>(nullptr));

                // if error
                perror("execl failed");
                exit(1);
            } else if (childPid > 0) {
                // fork for parent
                cout << "OSS: Launched child " << children << " (PID: " << childPid << ")" << endl;
                processesRunning.push_back(childPid);
                
                return true;
            } else {
                // error
                perror("fork failed");
                return false;
            }
        }
        // wait() for child to complete
        bool waitForChildren() {
            if (processesRunning.empty()) {
                return false;
            }
            int status;
            pid_t finishedPid = wait(&status);

            if (finishedPid > 0) {
                // Remove from running processes list
                auto it = find(processesRunning.begin(), processesRunning.end(), finishedPid);
                
                if (it != processesRunning.end()) {
                    processesRunning.erase(it);
                }
                cout << "OSS: Child with PID " << finishedPid << " has finished";
                
                // Check how the child terminated
                if (WIFEXITED(status)) {
                    int exitCode = WEXITSTATUS(status);
                    
                    cout << " with exit code " << exitCode;
                    
                    if (exitCode != 0) {
                        cout << " (ERROR)";
                    }
                } else if (WIFSIGNALED(status)) {
                    cout << " due to signal " << WTERMSIG(status);
                    }
                cout << endl;
                
                return true;
            } else {
                perror("wait failed");        
                return false;
            }
        }
        // method to execute program (MAIN)
        bool run() {
            // varaibles to track when program starts and ends
            int programLaunched = 0;
            int programCompleted = 0;

            while (programCompleted < numberOfChilds) {
                // created a process until limit is reached
                while(static_cast<int>(processesRunning.size()) < numberOfSimul && programLaunched < numberOfChilds)
                {
                    if (!childLaunched(programLaunched + 1))
                    {
                        cerr << "Error to launch child process" << endl;
                        return 1;
                    }
                    programLaunched++;
                }
                // waiting for child process
                if (!processesRunning.empty())
                {
                    if (waitForChildren())
                    {
                        programCompleted++;
                    } else {
                        cerr << "ERROR: Wating for child process error" << endl;
                        return 1;
                    }
                }
            }
            return 0;
        }
};

int main(int argc, char* argv[])
{
    // instantiating an bject for the OSS class
    OSS processManager;

    // parsing command line arguments
    if (!processManager.commandOptions(argc, argv))
    {
        // seeing if the help option is called
        if (argc >= 2 && string(argv[1]) == "-h")
        {
            return 0;
        }
        return 1;
    }
    // run the program
    return processManager.run();
}