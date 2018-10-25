#ifndef DEVICEIO_FRAMEWORK_SHELL_H_
#define DEVICEIO_FRAMEWORK_shell_H_

class Shell {
public:
   static bool exec(const char* cmd, char* result);
};

#endif //__DEVICEIO_SHELL_H__
