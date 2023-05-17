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
    struct _queue_node *next;
} queue_node;

void init_queue(queue *q);
int is_queue_empty(queue *q);
void enqueue(queue *q, const char *program_name, int start_time, int duration_time);
queue_node *dequeue(queue *q);
void print_queue(queue *q);
void free_queue(queue_node *q);

int main(void)
{
    // variaveis file I/O
    FILE *file_ptr;
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time, duration_time; /* escalonamento round robin */
    int c;

    // variaveis memoria compartilhada
    char *shm_program_name;
    int segment, *shm_start_time, *shm_duration_time, *interpreter_end, *shm_access_var;

    // variaveis dos processos
    int scheduler_pid, status;

    // criando segmento de memoria compartilhada
    segment = shmget(1000, SHM_SIZE, IPC_CREAT | 0666);
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

        usleep(10); /* garantindo que processo pai comece primeiro */

        // pegando todos os processos e colocando eles na fila de prontos
        while (1)
        {
            if (*shm_access_var == 1) // verificando se processo pai concedeu acesso para a memoria compartilhada
            {
                // a ideia eh que pra todo processo novo lido ocorra um fork() pra ele pra que seja possivel
                // enviar sinais
                if (*shm_start_time == -1 && *shm_duration_time == -1)
                { /* round robin */
                    local_start_time = *shm_start_time;
                    local_duration_time = *shm_duration_time;
                    strcpy(local_program_name, shm_program_name);

                    enqueue(processes_queue, local_program_name, local_start_time, local_duration_time);
                    print_queue(processes_queue);
                }
                else
                { /* real time */
                    if (*shm_start_time + *shm_duration_time > 60)
                    {
                        printf("programa %s nao pode ser escalonado.\ntempo total maior que a duracao maxima de 60 segundos\n", shm_program_name);
                    }

                    else
                    {
                        local_start_time = *shm_start_time;
                        local_duration_time = *shm_duration_time;
                        strcpy(local_program_name, shm_program_name);

                        enqueue(processes_queue, local_program_name, local_start_time, local_duration_time);
                        print_queue(processes_queue);
                    }
                }
                // remove acesso para a memoria compartilhada
                *shm_access_var = 0;
            }
            if (*interpreter_end == 1) // verifica se pai terminou de ler todos os programas a serem escalonados
                break;
        }

        // apos pegar todos os processos, escalona-los
        while (1)
        {
            if (is_queue_empty(processes_queue))
            {
                puts("fila vazia");
                exit(0);
            }

            // get first process in queue
            queue_node *process = dequeue(processes_queue); // pega o primeiro processo na fila

            if (process->start_time != -1 && process->duration_time != -1) // verifica se eh round robin
            {
                int start_time, new_process_pid;

                // pegando tempo de inicio de execucao
                gettimeofday(&current_time, NULL);
                start_time = current_time.tv_sec;

                // loop para executar processo por 1 segundo
                while (1)
                {
                    new_process_pid = fork();
                    if (new_process_pid == 0) // filho
                    {
                        // filho comeca a executar o programa ate receber sigstop do pai (scheduler)
                        execl(process->program_name, process->program_name, NULL);
                    }
                    else // pai (scheduler)
                    {
                        gettimeofday(&current_time, NULL);         // atualizando tempo atual
                        if (current_time.tv_sec - start_time == 1) // verificando se passou 1 segundo
                        {
                            /* se tiver passado 1 segundo, manda SIGSTOP para o processo e coloca ele no final da fila */
                            kill(new_process_pid, SIGSTOP);                                                               // sending sigstop to child process
                            enqueue(processes_queue, process->program_name, process->start_time, process->duration_time); // manda pro final da fila de pronto
                            break;
                        }
                    }
                }
            }

            else
            {
                // real time
            }
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

void enqueue(queue *q, const char *program_name, int start_time, int duration_time)
{
    queue_node *new_node = (queue_node *)malloc(sizeof(queue_node));
    strcpy(new_node->program_name, program_name);
    new_node->start_time = start_time;
    new_node->duration_time = duration_time;
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

void free_queue(queue_node *q)
{
    queue_node *current = q;

    while (current != NULL)
    {
        queue_node *next = current->next;
        free(current);
        current = next;
    }
}
