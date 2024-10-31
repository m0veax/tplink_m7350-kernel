/*
 * Copyright (c) 2009-2012 The Linux Foundation. All rights reserved
 *
 * Software was previously licensed under BSD license by Qualcomm Atheros, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

char **arg;
pid_t child_pid;

void start_hostapd(void)
{
    child_pid = fork();
    if ( child_pid == 0 ) {
        //printf("Starting hostapd (%d)\n", getpid());
        execv("./hostapd", arg);
        _exit( 0 );
    } else {
        wait(NULL);
    }
}

void kill_hostapd(void)
{
    //printf("Kill hostapd (%d)\n", child_pid);
    if(child_pid > 1) {
        kill(child_pid, SIGTERM);
    }
}

void sig_exit(int sig_num)
{
    kill_hostapd();
    _exit(0);
}

void sig_from_hostapd(int sig_num)
{
    signal(SIGUSR1, sig_from_hostapd);
    sleep(1);
    kill_hostapd();
}

int main(int argc, char *argv[]) {
    arg = argv;
    signal(SIGTERM, sig_exit);
    signal(SIGUSR1, sig_from_hostapd);
    while(1) {
        start_hostapd();
        sleep(1);
    }
    return 0;
}
