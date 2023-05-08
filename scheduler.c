#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <fcntl.h>

#define MAX_PROGRAM_NAME_LEN 20
#define SHM_SIZE 1024

int main(int argc, char *argv[])
{
    // variaveis memoria compartilhada
    int segment, *shm_start_time, *shm_duration_time;
    char *shm_program_name;

    // variaveis semafoto
    sem_t *semid;

    // conectando na memoria compartilhada ja criada
    segment = shmget(2000, SHM_SIZE, 0666);
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

    // conectando no semaforo ja criado (mode e value serão ignorados já que interpreter.c já criou o semaforo antes e definiu eles)
    semid = sem_open("/smphr" /* semaphore name since it is identified by the name */, O_CREAT /* creation flags */, 0666 /* permissions */, 1 /* initial semaphore value */);

    sem_wait(semid);

    if (*shm_start_time == -1 && *shm_duration_time == -1)
    { /* round robin */
        printf("*ROUND ROBIN*\n");
        printf("program name: %s\n", shm_program_name);
    }
    else
    { /* real time */
        printf("*REAL TIME*\n");
        printf("program name: %s\n", shm_program_name);
        printf("start time: %d\n", *shm_start_time);
        printf("duration time: %d\n", *shm_duration_time);
    }

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
