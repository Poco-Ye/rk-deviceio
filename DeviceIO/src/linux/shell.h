#ifndef DEVICEIO_FRAMEWORK_SHELL_H_
#define DEVICEIO_FRAMEWORK_shell_H_

#define MSG_BUFF_LEN (10 * 1024) //max size for wifi list

class Shell {
public:
    static bool exec(const char* cmd, char* result);
    static int get_pid(const char *Name);
};

#endif //__DEVICEIO_SHELL_H__
