#include "shell.h"
#include <stdio.h>
#include <string.h>

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
