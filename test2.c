#include <stdio.h>
#include <signal.h>

int main(int argc, char *argv[])
{
    kill(argv[1], SIGUSR1);

    while (1)
        ;
    return 0;
}