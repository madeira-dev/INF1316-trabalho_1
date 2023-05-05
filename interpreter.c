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
    // variaveis file I/O
    FILE *file_ptr;
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time;    /* escalonamento robinho */
    int duration_time; /* escalonamento robinho */
    int c;             /* pra iterar pelos char do arquivo */

    // variaveis memoria compartilhada
    char *shm_program_name;
    int segment, *shm_start_time, *shm_duration_time;

    // criando segmento de memoria compartilhada
    segment = shmget(14000, SHM_SIZE, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (segment == -1)
    {
        printf("erro no shmget\n");
        exit(1);
    }

    // dando attach das variaveis na memoria compartilhada
    shm_program_name = (char *)shmat(segment, NULL, 0);
    if (shm_program_name == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_start_time = (int *)shm_program_name + 1; /* posicao seguinte de program_name */
    if (shm_start_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_duration_time = (int *)shm_start_time + 1; /* posicao seguinte de start time*/
    if (shm_duration_time == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }

    // abrindo em read mode
    file_ptr = fopen("exec.txt", "r");

    // lendo linhas do arquivo
    while ((c = fgetc(file_ptr)) != EOF)
    {
        if (c == 'R')
        {
            if (fscanf(file_ptr, "un %s", program_name) == 1)
            {
                if (fscanf(file_ptr, " I=%d D=%d", &start_time, &duration_time) == 2)
                {
                    printf("escalonameno robinho\n");
                    printf("%s %d %d\n", program_name, start_time, duration_time);

                    // passando valores encontrados no arquivo para variaveis da memoria comparilhada
                    strcpy(shm_program_name, program_name);
                    *shm_start_time = start_time;
                    *shm_duration_time = duration_time;
                }
                else
                {
                    printf("escalonamento real time\n");
                    printf("%s\n", program_name);

                    // passando nome encontrado no arquivo pra variavel na memoria compartilhada
                    strcpy(shm_program_name, program_name);
                }
            }
            else
            {
                printf("formatacao invalida\n");
                exit(1);
            }
        }
        // sleep(2);
    }
    fclose(file_ptr);

    // dettach de variaveis da memoria compartilhada
    if (shmdt(shm_program_name) == -1)
    {
        printf("erro no shmdt program name\n");
        exit(3);
    }
    if (shmdt(shm_start_time) == -1)
    {
        printf("erro no shmdt start time\n");
        exit(3);
    }
    if (shmdt(shm_duration_time) == -1)
    {
        printf("erro no shmdt duration time\n");
        exit(3);
    }

    if (shmctl(segment, IPC_RMID, 0) == -1)
    {
        printf("erro no shmctl\n");
        exit(4);
    }

    return 0;
}
