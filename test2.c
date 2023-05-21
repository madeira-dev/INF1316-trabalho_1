// round robin io bound

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        int pid = atoi(argv[1]);
        kill(pid, SIGUSR1); // manda sinal falando que eh io bound
    }

    while (1)
        ;

    return 0;
}
