#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>

#define MAX_PROGRAM_NAME_LEN 20
#define SHM_SIZE 2048

typedef struct _queue_node
{
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time;
    int duration_time;
    struct _queue_node *next;
} queue_node;

typedef struct _queue
{
    struct _queue_node *front;
    struct _queue_node *rear;
} queue;

void init_queue(queue *q);
int is_queue_empty(queue *q);
void enqueue(queue *q, const char *program_name, int start_time, int duration_time);
void dequeue(queue *q);
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
    int segment, *shm_start_time, *shm_duration_time, *interpreter_end, *test;
    sem_t *access_smphr;

    // variaveis dos processos
    int scheduler_pid, status;

    // criando segmento de memoria compartilhada
    segment = shmget(90000, SHM_SIZE, IPC_CREAT | 0666);
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
    access_smphr = (sem_t *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int) + sizeof(int) + sizeof(int));
    if (access_smphr == (void *)-1)
    {
        printf("erro no shmat de access_smphr\n");
        exit(1);
    }
    test = (int *)((char *)shmat(segment, 0, 0) + (char)MAX_PROGRAM_NAME_LEN + sizeof(int) + sizeof(int) + sizeof(double) + sizeof(int));

    // initializing semaphore
    access_smphr = sem_open("/qqqqqqq", O_CREAT, 0666, 1);
    if (access_smphr == SEM_FAILED)
    {
        printf("erro no sem_open\n");
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
        *test = 0;
        // lendo linhas do arquivo
        while ((c = fgetc(file_ptr)) != EOF)
        {
            sem_wait(access_smphr); // esperando processo filho conceder acesso para a memoria compartilhada
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
                    sem_post(access_smphr); // concedendo acesso para a memoria compartilhada
                }

                else
                {
                    printf("formatacao invalida\n");
                    exit(1);
                }

                if (c == EOF)
                    break;

                // intervalo de 1 segundo entre processos lidos
                sleep(1);
            }
        }
        fclose(file_ptr);
        *test = 1; // informa que processo pai acabou de ler todos os programas a escalonar
    }

    else // scheduler (child)
    {
        while (1)
        {
            sem_wait(access_smphr); // esperando processo pai conceder acesso para a memoria compartilhada
            if (*test == 1)
            {
                sem_post(access_smphr); // concedendo acesso para a memoria compartilhada
                break;
            }

            if (*shm_start_time == -1 && *shm_duration_time == -1)
            { /* round robin */
                printf("*ROUND ROBIN*\n");
                printf("program name: %s\n", shm_program_name);
            }
            else
            { /* real time */
                // verificar se a soma do tempo de inicio com o tempo de duracao eh maior que 60 segundos
                // verificar se tempo de inicio e de duracao eh conflitante com algum tempo de inicio e duracao de processo existente

                if (*shm_start_time + *shm_duration_time > 60)
                {
                    printf("tempo total maior que 60 segundos\n");
                    exit(1); /* algum jeito correto de tratar sem ser saindo do programa? */
                }

                printf("*REAL TIME*\n");
                printf("program name: %s\n", shm_program_name);
                printf("start time: %d\n", *shm_start_time);
                printf("duration time: %d\n", *shm_duration_time);
            }

            sem_post(access_smphr); // concedendo acesso para a memoria compartilhada
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

    sem_unlink("/qqqqqqq");

    return 0;
}

void init_queue(queue *q) { q->front = NULL, q->rear = NULL; }

int is_queue_empty(queue *q) { return (q->front == NULL); }

void enqueue(queue *q, const char *program_name, int start_time, int duration_time)
{
    queue_node *new_node = (queue_node *)malloc(sizeof(queue_node));
    strcpy(new_node->program_name, program_name);
    new_node->start_time = start_time;
    new_node->duration_time = duration_time;
    new_node->next = NULL;

    if (is_queue_empty(q))
    {
        q->front = new_node;
        q->rear = new_node;
    }
    else
    {
        q->rear->next = new_node;
        q->rear = new_node;
    }
}

void dequeue(queue *q)
{
    // remove front node from the queue
    queue_node *tmp_node = q->front;
    char tmp_program_name[20];
    strcpy(tmp_program_name, tmp_node->program_name);
    q->front = q->front->next;
    free(tmp_node);

    // if queue becomes empty, set rear pointer to NULL
    if (q->front == NULL)
        q->rear = NULL;
}

void print_queue(queue *q)
{
    // atualizar funcao com tipo de programa (real time ou round robin) para evitar printar os tempos -1 -1
    if (is_queue_empty(q))
        return;

    queue_node *current = q->front;
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
