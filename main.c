#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>

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
    int segment, *shm_start_time, *shm_duration_time, *shm_access_var, *parent_end;

    // variaveis dos processos
    int scheduler_pid, status;

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
    shm_access_var = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int) + sizeof(int));
    if (shm_access_var == (void *)-1)
    {
        printf("erro no shmat de shm_access_var\n");
        exit(1);
    }
    parent_end = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int) + sizeof(int) + sizeof(int));
    if (parent_end == (void *)-1)
    {
        printf("erro no shmat parent end\n");
        exit(1);
    }

    // abrindo arquivo para leitura
    file_ptr = fopen("exec.txt", "r");

    scheduler_pid = fork();
    if (scheduler_pid == -1)
    {
        printf("erro no fork()\n");
        exit(1);
    }

    if (scheduler_pid != 0) // interpreter (parent)
    {
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
                    else
                    { /* round robin */
                        strcpy(shm_program_name, program_name);
                        *shm_start_time = start_time = -1;
                        *shm_duration_time = duration_time = -1;
                    }
                    *shm_access_var = 1;
                }

                else
                {
                    printf("formatacao invalida\n");
                    exit(1);
                }

                // intervalo de 1 segundo entre processos lidos
                sleep(1);
            }
        }

        fclose(file_ptr);
        *parent_end = 1; // informa que processo pai acabou de ler todos os programas a escalonar
    }

    else // scheduler (child)
    {
        usleep(10); // garantindo que processo pai inicia primeiro

        while (1)
        {
            if (*shm_access_var == 1) // verificando se processo pai concedeu acesso para a memoria compartilhada
            {
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

                // remove acesso para a memoria compartilhada
                *shm_access_var = 0;
            }
            if (*parent_end == 1) // verifica se pai terminou de ler todos os programas a serem escalonados
                break;
        }

        if (shmdt(shm_program_name) == -1)
        {
            printf("erro no detach");
            exit(1);
        }
        if (shmctl(segment, IPC_RMID, 0) == -1)
        {
            printf("erro no shmctl");
            exit(1);
        }

        exit(0);
    }

    wait(&status); // aguardando processo filho terminar

    return 0;
}
