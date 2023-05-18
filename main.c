#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>

#define MAX_PROGRAM_NAME_LEN 20
#define SHM_SIZE 1024

typedef struct _queue // fila de prontos
{
    struct _queue_node *head;
    struct _queue_node *tail;
} queue;

typedef struct _queue_node // processos
{
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time;
    int duration_time;
    int pid;
    struct _queue_node *next;
} queue_node;

void init_queue(queue *q);
int is_queue_empty(queue *q);
void enqueue(queue *q, const char *program_name, int start_time, int duration_time, int pid);
queue_node *dequeue(queue *q);
void print_queue(queue *q);
void free_queue(queue *q);

int main(void)
{
    // variaveis file I/O
    FILE *file_ptr;
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time, duration_time; /* escalonamento real time */
    int c, i /* variavel temporaria so pra testar escalonador */;

    // variaveis memoria compartilhada
    char *shm_program_name;
    int segment, *shm_start_time, *shm_duration_time, *interpreter_end, *shm_access_var;

    // variaveis dos processos
    int scheduler_pid, status;

    // criando segmento de memoria compartilhada
    segment = shmget(12000, SHM_SIZE, IPC_CREAT | 0666);
    if (segment == -1)
    {
        printf("erro no shmget\n");
        exit(1);
    }

    // attach de variaveis na memoria compartilhada
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
    interpreter_end = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int) + sizeof(int));
    if (interpreter_end == (void *)-1)
    {
        printf("erro no shmat\n");
        exit(1);
    }
    shm_access_var = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int) + sizeof(int) + sizeof(int));
    if (shm_access_var == (void *)-1)
    {
        printf("erro no shmat de shm_access_var\n");
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
                        if (start_time + duration_time > 60)
                        {
                            printf("start time: %d\nduration time: %d", *shm_start_time, *shm_duration_time);
                            printf("programa %s nao pode ser escalonado.\ntempo total maior que a duracao maxima de 60 segundos\n", shm_program_name);
                        }
                        else
                        {
                            strcpy(shm_program_name, program_name);
                            *shm_start_time = start_time;
                            *shm_duration_time = duration_time;
                        }
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
        *interpreter_end = 1; // informa que processo pai acabou de ler todos os programas a escalonar
    }

    else // scheduler (child)
    {
        queue *processes_queue;
        int local_start_time, local_duration_time, is_program_executing = 0;
        char local_program_name[MAX_PROGRAM_NAME_LEN];
        struct timeval current_time;

        processes_queue = (queue *)malloc(sizeof(queue));
        init_queue(processes_queue);

        usleep(10); /* garantindo que processo pai inicia primeiro */

        // pegando todos os processos e colocando eles na fila de prontos
        while (1)
        {
            if (*shm_access_var == 1) // verificando se processo pai concedeu acesso para a memoria compartilhada
            {
                if (*shm_start_time == -1 && *shm_duration_time == -1)
                { /* round robin */
                    local_start_time = *shm_start_time;
                    local_duration_time = *shm_duration_time;
                    strcpy(local_program_name, shm_program_name);

                    enqueue(processes_queue, local_program_name, local_start_time, local_duration_time, 0);
                    print_queue(processes_queue);
                }
                else
                { /* real time */
                    local_start_time = *shm_start_time + 60;
                    local_duration_time = *shm_duration_time;
                    strcpy(local_program_name, shm_program_name);

                    enqueue(processes_queue, local_program_name, local_start_time, local_duration_time, 0);
                    print_queue(processes_queue);
                }
                // remove acesso para a memoria compartilhada
                *shm_access_var = 0;
            }
            if (*interpreter_end == 1) // verifica se pai terminou de ler todos os programas a serem escalonados
                break;
        }

        printf("\n");
        // // apos pegar todos os processos, escalona-los
        while (i < 5)
        {
            if (is_queue_empty(processes_queue))
            {
                puts("fila vazia\n");
                exit(0);
            }

            // pega primeiro processo da fila
            queue_node *process = dequeue(processes_queue); // pega o primeiro processo na fila

            if (process->start_time == -1 && process->duration_time == -1) // round robin
            {
                int start_time_rr;

                // pegando tempo de inicio de execucao
                gettimeofday(&current_time, NULL);
                start_time_rr = current_time.tv_sec;

                printf("iniciando o programa %s (round robin)\n\n", process->program_name);
                print_queue(processes_queue);
                if (process->pid == 0) // primeira vez que esta executando
                {
                    // criando processo filho para executar o programa
                    process->pid = fork();
                    if (process->pid == 0) // filho
                    {
                        /* na teoria o programa demora muito mais que 1 segundo
                        entao nao teria problema do filho acabar de executar
                        antes do pai contabilizar um segundo de execucao */
                        is_program_executing = 1;
                        printf("execl %s agora\n\n", process->program_name);
                        execl(process->program_name, process->program_name, NULL); // programa executando ate receber SIGSTOP
                        pause();
                    }
                    else // pai (scheduler)
                    {
                        while (1)
                        {
                            gettimeofday(&current_time, NULL);            // atualizando tempo atual
                            if (current_time.tv_sec - start_time_rr == 1) // verificando se passou 1 segundo
                            {
                                /* se tiver passado 1 segundo, manda SIGSTOP para o processo e coloca ele no final da fila */
                                printf("SIGSTOP %s agora\n\n", process->program_name);
                                kill(process->pid, SIGSTOP); // envia SIGSTOP pro processo em execucao
                                is_program_executing = 0;
                                enqueue(processes_queue, process->program_name, process->start_time, process->duration_time, process->pid); // manda pro final da fila de pronto
                                print_queue(processes_queue);
                                break;
                            }
                        }
                    }
                }
                else // processo que ja foi executado e retornou ao primeiro lugar da fila de prontos
                {
                    is_program_executing = 1;
                    printf("SIGCONT agora\n\n");
                    kill(process->pid, SIGCONT); // faz o processo retornar a execucao

                    while (1)
                    {
                        gettimeofday(&current_time, NULL);            // atualizando tempo atual
                        if (current_time.tv_sec - start_time_rr == 1) // verificando se passou 1 segundo
                        {
                            /* se tiver passado 1 segundo, manda SIGSTOP para o processo e coloca ele no final da fila */
                            printf("SIGSTOP do processo repetido agora\n\n");
                            kill(process->pid, SIGSTOP); // envia SIGSTOP para o processo em execucao
                            is_program_executing = 0;
                            enqueue(processes_queue, process->program_name, process->start_time, process->duration_time, process->pid); // manda pro final da fila de pronto
                            print_queue(processes_queue);
                            break;
                        }
                    }
                }
            }

            else // real time
            {
                printf("entrou real time\n");
                int start_time_rt;
                gettimeofday(&current_time, NULL);
                start_time_rt = current_time.tv_sec;
                while (1)
                {
                    gettimeofday(&current_time, NULL);                              // atualizando a variavel de tempo atual
                    if (start_time_rt + process->start_time == current_time.tv_sec) // testando se atingiu o tempo de inicio
                    {
                        start_time_rt = current_time.tv_sec;
                        if (process->pid == 0) // primeira vez que esta executando
                        {
                            // criando processo filho para executar o programa
                            process->pid = fork();
                            if (process->pid == 0) // filho
                            {
                                /* na teoria o programa demora muito mais que 1 segundo
                                entao nao teria problema do filho acabar de executar
                                antes do pai contabilizar um segundo de execucao */
                                is_program_executing = 1;
                                printf("execl %s agora\n\n", process->program_name);
                                execl(process->program_name, process->program_name, NULL); // programa executando ate receber SIGSTOP
                                pause();
                            }
                            else // pai (scheduler)
                            {
                                while (1)
                                {
                                    gettimeofday(&current_time, NULL);                                 // atualizando tempo atual
                                    if (start_time_rt + process->duration_time == current_time.tv_sec) // verificando se passou o tempo de duracao do processo
                                    {
                                        /* se tiver passado o tempo de duracao, manda SIGSTOP para o processo e coloca ele no final da fila */
                                        printf("SIGSTOP %s agora\n\n", process->program_name);
                                        kill(process->pid, SIGSTOP); // envia SIGSTOP pro processo em execucao
                                        is_program_executing = 0;
                                        enqueue(processes_queue, process->program_name, process->start_time, process->duration_time, process->pid); // manda pro final da fila de pronto
                                        print_queue(processes_queue);
                                        break;
                                    }
                                }
                            }
                        }
                        else // processo que ja foi executado e retornou ao primeiro lugar da fila de prontos
                        {
                            is_program_executing = 1;
                            printf("SIGCONT agora\n\n");
                            start_time_rt = current_time.tv_sec;
                            kill(process->pid, SIGCONT); // faz o processo retornar a execucao

                            while (1)
                            {
                                gettimeofday(&current_time, NULL);                                 // atualizando tempo atual
                                if (start_time_rt + process->duration_time == current_time.tv_sec) // verificando se passou 1 segundo
                                {
                                    /* se tiver passado 1 segundo, manda SIGSTOP para o processo e coloca ele no final da fila */
                                    printf("SIGSTOP do processo repetido agora\n\n");
                                    kill(process->pid, SIGSTOP); // envia SIGSTOP para o processo em execucao
                                    is_program_executing = 0;
                                    enqueue(processes_queue, process->program_name, process->start_time, process->duration_time, process->pid); // manda pro final da fila de pronto
                                    print_queue(processes_queue);
                                    break;
                                }
                            }
                        }
                    }
                }
                // sleep pelo tempo que resta ate completar 1 minuto ja que os processos sao periodicos (1 por minuto)
                // sleep(60 - (start_time_rt + process->duration_time));
            }
            i++;
        }

        free_queue(processes_queue);

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

void init_queue(queue *q) { q->head = NULL, q->tail = NULL; }

int is_queue_empty(queue *q) { return (q->head == NULL); }

void enqueue(queue *q, const char *program_name, int start_time, int duration_time, int pid)
{
    queue_node *new_node = (queue_node *)malloc(sizeof(queue_node));
    strcpy(new_node->program_name, program_name);
    new_node->start_time = start_time;
    new_node->duration_time = duration_time;
    new_node->pid = pid;
    new_node->next = NULL;

    if (is_queue_empty(q))
    {
        q->head = new_node;
        q->tail = new_node;
    }
    else
    {
        q->tail->next = new_node;
        q->tail = new_node;
    }
}

queue_node *dequeue(queue *q)
{
    if (is_queue_empty(q))
        return NULL;

    queue_node *removed_node = q->head;
    q->head = q->head->next;

    if (q->head == NULL)
        q->tail = NULL;

    return removed_node;
}

void print_queue(queue *q)
{
    if (is_queue_empty(q))
        return;

    queue_node *current = q->head;
    printf("queue: ");

    while (current != NULL)
    {
        printf("%s ", current->program_name);

        (current->start_time != -1) ? printf("%d ", current->start_time) : 0;
        (current->duration_time != -1) ? printf("%d ", current->duration_time) : 0;
        printf("| ");
        current = current->next;
    }
    printf("\n");
}

void free_queue(queue *q)
{
    queue_node *current = q->head;
    queue_node *next;

    while (current != NULL)
    {
        next = current->next;
        free(current);
        current = next;
    }
    q->head = NULL;
    q->tail = NULL;
}
