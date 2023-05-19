#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

int main(void)
{
    // execute test1 file
    execl("test1", "test1", "1", NULL);

    return 0;
}