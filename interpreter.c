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

int main(void)
{
    // variaveis file I/O
    FILE *file_ptr;
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time, duration_time; /* escalonamento round robin */
    int c;

    // variaveis memoria compartilhada
    char *shm_program_name;
    int segment, *shm_start_time, *shm_duration_time;

    // variaveis semaforo
    sem_t *semid;

    // criando segmento de memoria compartilhada
    segment = shmget(2000, SHM_SIZE, IPC_CREAT | 0666);
    if (segment == -1)
    {
        printf("erro no shmget\n");
        exit(1);
    }

    // attach de variaveis na memoria compartilhada
    shm_program_name = (char *)shmat(segment, 0, 0); /* attach no endereco inicial */
    if (shm_program_name == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_start_time = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN); /* attach no endereco com o offset de program name */
    if (shm_start_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_duration_time = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int)); /* attach no endereco com offset de program name + start time */
    if (shm_duration_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }

    // criando semaforo
    semid = sem_open("/smphr" /* semaphore name since it is identified by the name */, O_CREAT /* creation flags */, 0666 /* permissions */, 1 /* initial semaphore value */);

    // abrindo arquivo para leitura
    file_ptr = fopen("exec.txt", "r");

    // lendo linhas do arquivo
    while ((c = fgetc(file_ptr)) != EOF)
    {
        if (c == 'R')
        {
            if (fscanf(file_ptr, "un %s", program_name) == 1)
            {
                if (fscanf(file_ptr, " I=%d D=%d", &start_time, &duration_time) == 2)
                { /* real time */
                    strcpy(shm_program_name, program_name);
                    *shm_start_time = start_time;
                    *shm_duration_time = duration_time;
                }
                else /* round robin */
                {
                    strcpy(shm_program_name, program_name);
                    *shm_start_time = start_time = -1;
                    *shm_duration_time = duration_time = -1;
                    sem_post(semid);
                }
            }
            else
            {
                printf("formatacao invalida\n");
                exit(1);
            }
            // intervalo de 1 segundo entre processos lidos
            // sleep(1);
        }
        fclose(file_ptr);
    }
    return 0;
}