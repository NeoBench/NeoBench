#include "shell/shell.hpp"

extern "C"
{
#include <proto/dos.h>
}

int cmd_wait(int argc, char** argv)
{
    if (argc < 2)
        return 1;

    int secs = atoi(argv[1]);

    Delay(secs * 50);

    return 0;
}
