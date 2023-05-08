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

int main(void)
{
    FILE *file_ptr;
    char program_name[MAX_PROGRAM_NAME_LEN], exec_mode[] = "1", interpreter_pid[8];
    int start_time, duration_time; /* escalonamento round robin */
    int c, scheduler_pid, pid, status;
    sprintf(interpreter_pid, "%d", getpid()); // passando pra string pra enviar pro scheduler por arg
    char *args[] = {"scheduler", exec_mode, interpreter_pid, NULL};

    // variaveis memoria compartilhada
    char *shm_program_name;
    int segment, *shm_start_time, *shm_duration_time;

    // criando segmento de memoria compartilhada
    segment = shmget(2000, SHM_SIZE, IPC_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (segment == -1)
    {
        printf("erro no shmget\n");
        exit(1);
    }

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

    // criando processo filho para poder executar o scheduler e descobrir o pid dele
    pid = fork();
    if (pid == -1)
    {
        printf("erro no fork()\n");
        exit(1);
    }
    if (pid == 0)
    {
        execv("scheduler", args);

        // atualizando valor do exec_mode pra não entrar mais no if inicial do scheduler
        strcpy(exec_mode, "2");
        exit(1);
    }
    else
    {
        scheduler_pid = pid;

        // esperando scheduler acabar
        wait(&status);
        printf("voltei pro interpreter\n");
        kill(scheduler_pid, SIGCONT);

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

                        strcpy(shm_program_name, program_name);
                        *shm_start_time = start_time;
                        *shm_duration_time = duration_time;

                        // printf("program name, start time e duration time na memoria ja: %s %d %d\n", shm_program_name, *shm_start_time, *shm_duration_time);

                        // avisa(?) pro escalonador pegar variaveis na memoria
                        // mandar USRSIG1 (esse avisa que é round robin)
                    }
                    else
                    {

                        strcpy(shm_program_name, program_name);

                        // avisa(?) pro escalonador pegar variavel na memoria
                        // mandar SIGUSR2 (esse avisa q é real time)
                    }
                }
                else
                {
                    printf("formatacao invalida\n");
                    exit(1);
                }
            }

            // intervalo de 1 segundo entre processos lidos
            // sleep(1);
        }
        fclose(file_ptr);
    }
    return 0;
}