#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

bool Shell::exec(const char *cmdline, char *recv_buff) {
    printf("consule_run: %s\n",cmdline);

    FILE *stream = NULL;
    char buff[1024];

    memset(recv_buff, 0, sizeof(recv_buff));
    if((stream = popen(cmdline,"r"))!=NULL){
        while(fgets(buff,1024,stream)){
            strcat(recv_buff,buff);
        }
    }
    pclose(stream);
    return true;
}

int Shell::get_pid(const char *Name) {
    int len;
    char name[20] = {0};
    len = strlen(Name);
    strncpy(name,Name,len);
    name[len] ='\0';
    char cmdresult[256] = {0};
    char cmd[20] = {0};
    FILE *pFile = NULL;
    int  pid = 0;

    sprintf(cmd, "pidof %s", name);
    pFile = popen(cmd, "r");
    if (pFile != NULL)  {
        while (fgets(cmdresult, sizeof(cmdresult), pFile)) {
            pid = atoi(cmdresult);
            break;
        }
    }
    pclose(pFile);
    return pid;
}
