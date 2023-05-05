#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>

#define MAX_PROGRAM_NAME_LEN 20
#define SHM_SIZE 1024

int main(void)
{
    int segment, *shm_start_time, *shm_duration_time;
    char *shm_program_name;

    segment = shmget(12000, SHM_SIZE, IPC_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
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
    *shm_start_time = (int *)shmat(segment, 0, 0);
    if (*shm_start_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    *shm_duration_time = (int *)shmat(segment, 0, 0);
    if (*shm_duration_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }

    return 0;
}
