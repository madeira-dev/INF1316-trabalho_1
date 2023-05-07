#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#define MAX_PROGRAM_NAME_LEN 20
#define SHM_SIZE 1024

int main(int argc, char *argv[])
{
    if (argc > 1 && atoi(argv[1]) == 1)
    {
        printf("to no scheduler e o pid eh: %d\n", getpid());
        return 0;
    }

    int segment, *shm_start_time, *shm_duration_time;
    char *shm_program_name;

    segment = shmget(2000, SHM_SIZE, S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (segment == -1)
    {
        printf("erro no shmget\n");
        exit(1);
    }

    // attach de variaveis
    shm_program_name = (char *)shmat(segment, 0, 0);
    if (shm_program_name == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_start_time = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN);
    if (shm_start_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_duration_time = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int));
    if (shm_duration_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }

    printf("codigo completo scheduler\n");
    // printf("program name: %s\n", shm_program_name);
    // printf("start time: %d\n", *shm_start_time);
    // printf("duration time: %d\n", *shm_duration_time);
    if (shmdt(shm_program_name) == -1)
    {
        printf("erro no shmdt() program name\n");
        exit(1);
    }
    /* por que está dando erro quando eu dou detach das outras variaveis???
    só detach de shm_program_name ta dando certo e sem memory leak */

    if (shmctl(segment, IPC_RMID, 0))
    {
        printf("erro no shmctl() de segment\n");
        exit(2);
    }

    return 0;
}
