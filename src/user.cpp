/* Author:      Zoya Samsonov
 * Created:     October 15, 2020
 * Last edit:   October 21, 2020
 */

#include <iostream>          //cout, cerr
#include <unistd.h>          //getpid()
#include <signal.h>          //signal()
#include "shm_handler.h"     //shmlookup(), shmdetach()
#include "error_handler.h"   //setupprefix()
#include "file_handler.h"    //add_outfile_append(), writeline()
#include "sys_clk.h"         //struct clk
#include "sched_handler.h"   //struct pcb

volatile bool earlyquit = false;

void signalhandler(int signum) {
    exit(-1); // child does not create any ipc, safe to just kill
}

int main(int argc, char **argv) {
    // register SIGINT (^C)
    signal(SIGINT, signalhandler);
    // set up perror prefix
    setupprefix(argv[0]);
    // set seed for rand()
    srand(getpid());
    // attach shared clock
    clk* shclk = (clk*)shmlookup(0);
    //pcb* pcbtable = (pcb*)shmlookup(1);
    int pcbnum = std::stoi(argv[1]);
    pcbmsgbuf* msg = new pcbmsgbuf;

    while(1) {
        // mtype = 1 reserved for messages back to oss
        // mtype = 2 reserved for unblock status messages
        msg->mtype = pcbnum + 3; 
        msgreceive(2, msg); // overwrites previous msg
        if (msg->data[PCBNUM] != pcbnum)
            customerrorquit("pcb#" + std::to_string(pcbnum) +
                "received message intended for pcb#" + 
                std::to_string(msg->data[PCBNUM]));
      
        // prepare to return message
        msg->mtype = 1;
        if (rand() % 10 < 1) { // 1/10 chance to terminate
            // terminating after using a random amount of time
            // generate and send appropriate message
            msg->data[TIMESLICE] = rand() % msg->data[TIMESLICE];
            msg->data[STATUS] = TERM;
            msgsend(2, msg);
            // cleanup and terminate
            shmdetach(shclk);
            exit(0);
        } else {
            // not terminating
            if (rand() % 2) {
                // use entire quantuum (no need to change data[TIMESLICE])
                // generate and send approprate message
                msg->data[STATUS] = RUN;
                msgsend(2, msg);
            } else {
                // use some quantuum and get blocked
                // generate and send appropriate message
                msg->data[TIMESLICE] = rand() % msg->data[TIMESLICE];
                msg->data[STATUS] = BLOCK;
                msgsend(2, msg);
                // calculate how long to block and then wait
                float wakeuptime = shclk->nextrand(3e9);
                while (shclk->tofloat() < wakeuptime);
                // generate and send unblock message on reserved mtype = 2
                msg->mtype = 2;
                msg->data[STATUS] = UBLOCK;
                msgsend(2, msg);
                // reset mtype
                msg->mtype = pcbnum+3;
            }
        }
    }
}
