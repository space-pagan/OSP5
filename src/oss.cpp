/* Author:      Zoya Samsonov
 * Created:     September 9, 2020
 * Last edit:   November 05, 2020
 */

#include <iostream>              //cout
#include <string>                //std::string
#include <cstring>               //strcmp()
#include <unistd.h>              //alarm()
#include <csignal>               //signal()
#include <vector>                //std::vector
#include <list>                  //std::list
#include "cli_handler.h"         //cli arguments and parsing runpath
#include "child_handler.h"       //forking and handling child processes
#include "error_handler.h"       //error reporting
#include "shm_handler.h"         //shared memory and message queues
#include "help_handler.h"        //printhelp()
#include "sys_clk.h"             //struct clk
#include "res_handler.h"         //struct Descriptor, struct resman
#include "log_handler.h"         //logging utilities
#include "util.h"

// variables used in interrupt handling
volatile bool earlyquit = false;
volatile bool handlinginterrupt = false;
volatile int quittype = 0;

// set flags so program can quit gracefully
void signalhandler(int signum) {
    if (signum == SIGALRM || signum == SIGINT) {
        earlyquit = true;
        quittype = signum;
    }
}

void cleanup(clk* shclk, resman r, int& conc_count) {
    // remove all child processes
    while (conc_count > 0) {
        killallchildren();
        updatechildcount(conc_count);
    }
    // release all shared memory created
    shmdetach(shclk);
    shmdetach(r.desc);
    shmdetach(r.sysmax);
    ipc_cleanup();
}

void testopts(int argc, char** argv, std::string pref, int optind, bool* flags) {
    if (flags[0]) {
        printhelp(pref);
        exit(0);
    }

    if (argc > optind) custerrhelpprompt(
        "Unknown argument '" + std::string(argv[optind]) + "'");
}

void main_loop(std::string runpath) {
    int max_count = 0;    //count of children created
    int conc_count = 0;   //count of currently running children
    int currID = 0;       //value of next unused ftok id
    int spawnConst = 500000000; //the maximum time between new fork calls
    int pid;              // pid for new child
    srand(getpid());      //set random seed;
    // create shared clock
    clk* shclk = (clk*)shmcreate(sizeof(clk), currID);
    float nextSpawnTime = shclk->nextrand(spawnConst);
    msgcreate(currID);
    // instantiate resource manager
    resman r(currID);
    pcbmsgbuf* buf = new pcbmsgbuf;

    std::list<Data> blockedRequests;

    while (!earlyquit) {
        if (max_count >= 40 && conc_count == 0) earlyquit = true;
        if (shclk->tofloat() >= nextSpawnTime && max_count < 40) {
            r.findandsetpid(pid);
            if (pid >= 0) {
                logNewPID(shclk, pid, ++max_count);
                forkexec(runpath + "user " + std::to_string(pid), conc_count);
                nextSpawnTime = shclk->nextrand(spawnConst);
            }
        }
        buf->mtype = 1; // set explicitly
        if (msgreceivenw(1, buf)) {
            if (buf->data.status == CLAIM) {
                logMaxClaim(shclk, buf->data);
                r.stateclaim(buf->data.pid, buf->data.resarray);
                msgsend(1, buf->data.pid+2);
            } else if (buf->data.status == REQ) {
                int allocated = r.allocate(
                        buf->data.pid, buf->data.resi, buf->data.resamount);
                if (allocated == 0) {
                    msgsend(1, buf->data.pid+2);
                    logReqGranted(shclk, buf->data);
                } else if (allocated == 1) {
                    blockedRequests.push_back(buf->data);
                    logReqDenied(shclk, buf->data);
                } else if (allocated == 2) {
                    blockedRequests.push_back(buf->data);
                    logReqDeadlock(shclk, buf->data);
                }
            } else if (buf->data.status == REL) {
                r.release(buf->data.pid, buf->data.resi, buf->data.resamount);
                msgsend(1, buf->data.pid+2);
                logRel(shclk, buf->data, blockedRequests.size());
                auto i = blockedRequests.begin();
                while (i != blockedRequests.end()) {
                    if (r.allocate((*i).pid, (*i).resi, (*i).resamount) == 0) {
                        msgsend(1, (*i).pid+2);
                        logUnblock(shclk, (*i));
                        blockedRequests.erase(i++);
                    } else {
                        i++;
                    }
                }
            } else if (buf->data.status == TERM) {
                for (int i : range(20)) {
                    r.release(buf->data.pid, i, r.desc[i].alloc[buf->data.pid]);
                }
                waitforchildpid(buf->data.realpid, conc_count);
                r.unsetpid(buf->data.pid);
                logTerm(shclk, buf->data, blockedRequests.size());
                auto i = blockedRequests.begin();
                while (i != blockedRequests.end()) {
                    if (r.allocate((*i).pid, (*i).resi, (*i).resamount) == 0) {
                        msgsend(1, (*i).pid+2);
                        logUnblock(shclk, (*i));
                        blockedRequests.erase(i++);
                    } else {
                        i++;
                    }
                }
            }
        } else {
            // std::cout << shclk->tostring() << ": Waiting\n";
        }
        shclk->inc(2e5);
    }

    std::cout << logExit(shclk, quittype, max_count) << "\n";
    std::cout << "For simulation details, please see " << getLogPath() << "\n";
    cleanup(shclk, r, conc_count);

}

int main(int argc, char** argv) {
    // register SIGINT (^C) and SIGALRM with signal handler
    signal(SIGINT, signalhandler);
    signal(SIGALRM, signalhandler);
    // set perror to display the correct program name
    std::string runpath, pref;
    parserunpath(argv, runpath, pref);
    setupprefix(pref);
    if (!pathdepcheck(runpath, "user")) pathdeperror();

    std::vector<std::string> opts{};    // sacrificial vector
    bool flags[2] = {false, false};    // -h, -v
    int optind = getcliarg(argc, argv, "", "hv", opts, flags);
    createLogFile(runpath, 100000, flags[1]); //do not exceed 10k lines
    int max_time = 5;
    // this line will terminate the program if any options are mis-set
    testopts(argc, argv, pref, optind, flags);
    // set up kill timer
    alarm(max_time);
    main_loop(runpath);
    // done
    return 0;
}
