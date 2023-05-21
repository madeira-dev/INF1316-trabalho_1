/*
    Membros do grupo:
        Gabriel Madeira 2111471
        Juliana Pinheiro 2110516
*/

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

typedef struct _queue // fila de processos
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
    int io_start_time;
    struct _queue_node *next;
} queue_node;

void init_queue(queue *q);
int is_queue_empty(queue *q);
void enqueue(queue *q, const char *program_name, int start_time, int duration_time, int pid, int io_start_time);
queue_node *dequeue(queue *q);
void print_queue(queue *q);
void free_queue(queue *q);
int check_conflicting_times(queue *q, int curr_process_start_time, int curr_process_duration_time);
void sigusr1_handler(int signum);

// variavel global para tratar programas I/O bound
volatile int is_io = 0;

int main(void)
{
    // variaveis file I/O
    FILE *file_ptr;
    char program_name[MAX_PROGRAM_NAME_LEN];
    int start_time, duration_time; /* escalonamento real time */
    int c;

    // variaveis memoria compartilhada
    char *shm_program_name;
    int segment, *shm_start_time, *shm_duration_time, *interpreter_end, *shm_access_var;

    // variaveis dos processos
    int scheduler_pid, status;

    signal(SIGUSR1, sigusr1_handler); // sigusr1 usado para receber sinal de I/O bound
    queue *temp_queue;                // queue temporaria no interpretador para ver se tempos dos processos real time estao conflitando
    temp_queue = (queue *)malloc(sizeof(queue));
    init_queue(temp_queue);

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

    if (scheduler_pid != 0) // interpretador (pai)
    {
        printf("Eviando dados do arquivo exec.txt para o escalonador\n\n");
        // lendo linhas do arquivo
        while ((c = fgetc(file_ptr)) != EOF)
        {
            if (c == 'R')
            {
                if (fscanf(file_ptr, "un %s", program_name) == 1)
                {
                    if (fscanf(file_ptr, " I=%d D=%d", &start_time, &duration_time) == 2)
                    {                                        /* real time */
                        if (start_time + duration_time > 60) // verifica se tempo total eh maior que limite maximo
                            printf("programa %s nao pode ser escalonado.\ntempo total maior que a duracao maxima de 60 segundos\n", shm_program_name);
                        else if (check_conflicting_times(temp_queue, start_time, duration_time)) // verifica se existe conflito entre tempos dos processos
                            printf("tempos do processo %s conflitam com os de outro processo.\nNao pode ser escalonado.\n", program_name);
                        else
                        {
                            strcpy(shm_program_name, program_name);
                            *shm_start_time = start_time;
                            *shm_duration_time = duration_time;
                            enqueue(temp_queue, program_name, start_time, duration_time, 0, 0);
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
        free_queue(temp_queue);
    }

    else // escalonador (filho)
    {
        queue *waiting_queue /* fila para colocar os i/o até chegar o tempo deles */, *real_time_processes_queue /* fila apenas para processos real time */, *round_robin_processes_queue /* fila apenas para processos round robin */;
        int local_start_time, local_duration_time;
        char local_program_name[MAX_PROGRAM_NAME_LEN];
        struct timeval current_time;
        signal(SIGUSR1, sigusr1_handler);

        waiting_queue = (queue *)malloc(sizeof(queue));
        real_time_processes_queue = (queue *)malloc(sizeof(queue));
        round_robin_processes_queue = (queue *)malloc(sizeof(queue));
        init_queue(waiting_queue);
        init_queue(real_time_processes_queue);
        init_queue(round_robin_processes_queue);

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

                    enqueue(round_robin_processes_queue, local_program_name, local_start_time, local_duration_time, 0, 0);
                }
                else
                { /* real time */
                    local_start_time = *shm_start_time + 60;
                    local_duration_time = *shm_duration_time;
                    strcpy(local_program_name, shm_program_name);

                    enqueue(real_time_processes_queue, local_program_name, local_start_time, local_duration_time, 0, 0);
                }
                // remove acesso para a memoria compartilhada
                *shm_access_var = 0;
            }
            if (*interpreter_end == 1) // verifica se pai terminou de ler todos os programas a serem escalonados
                break;
        }

        printf("Fila Round-Robin: ");
        print_queue(round_robin_processes_queue);
        printf("Fila Real Time: ");
        print_queue(real_time_processes_queue);
        printf("\n");

        if (is_queue_empty(round_robin_processes_queue) && is_queue_empty(real_time_processes_queue))
        {
            puts("filas vazia\n");
            exit(0);
        }

        // apos pegar todos os processos, escalonar eles
        int scheduler_start_time, io_time_slice = 3 /* considerando 3 sec de tempo de io para round robin */, time_slice_count = 0 /* variavel contando quantidade de time slices executados de programas roundrobin para finalizar execucao */;
        queue_node *round_robin_process = (queue_node *)malloc(sizeof(queue_node));
        queue_node *real_time_process = (queue_node *)malloc(sizeof(queue_node));
        gettimeofday(&current_time, NULL); // atualizando tempo atual
        scheduler_start_time = current_time.tv_sec;

        while (1) /* loop para escalonar */
        {
            gettimeofday(&current_time, NULL); // atualizando tempo atual

            // verificando se existe io em execucao e se sim, testando se seu tempo ja acabou para retornar a fila de pronto
            if (!is_queue_empty(waiting_queue))
            {
                if (waiting_queue->head->start_time == -1 && waiting_queue->head->duration_time == -1) // verificando se eh round robin
                {
                    // testando para saber se o processo que tinha i/o era real time ou round robin pra mandar pra fila de pronto certa
                    if ((waiting_queue->head->io_start_time + io_time_slice) <= current_time.tv_sec) // verificando se atingiu tempo de i/o
                    {
                        queue_node *tmp_round_robin_process;
                        tmp_round_robin_process = (queue_node *)malloc(sizeof(queue_node));
                        tmp_round_robin_process = dequeue(waiting_queue);

                        kill(tmp_round_robin_process->pid, SIGSTOP);
                        enqueue(round_robin_processes_queue, tmp_round_robin_process->program_name, tmp_round_robin_process->start_time, tmp_round_robin_process->duration_time, tmp_round_robin_process->pid, tmp_round_robin_process->io_start_time);
                        printf("Programa %s (round robin) voltou para a fila de pronto (estava na fila de espera de I/O)\n\n", tmp_round_robin_process->program_name);
                        free(tmp_round_robin_process);
                    }
                }
            }

            if (!is_queue_empty(round_robin_processes_queue))
                if (round_robin_processes_queue->head->start_time == -1 && round_robin_processes_queue->head->duration_time == -1) // round robin
                {
                    int start_time_rr;
                    printf("Fila Round-Robin: ");
                    print_queue(round_robin_processes_queue);
                    round_robin_process = dequeue(round_robin_processes_queue); // pega o primeiro processo na fila

                    // pegando tempo de inicio de execucao
                    gettimeofday(&current_time, NULL);
                    start_time_rr = current_time.tv_sec;

                    printf("Pegando primeiro processo da fila de round robin: %s\n", round_robin_process->program_name);
                    if (round_robin_process->pid == 0) // primeira vez que esta executando
                    {
                        printf("Executando o programa %s (round robin) pela primeira vez\n", round_robin_process->program_name);

                        // criando processo filho para executar o programa
                        round_robin_process->pid = fork();
                        if (round_robin_process->pid == 0) // filho
                        {
                            signal(SIGUSR1, sigusr1_handler);
                            char tmp_parent_pid[24]; // usado apenas para converter o pid do pai para string pra poder passar como argumento (ja que todos devem ser passados como string)
                            sprintf(tmp_parent_pid, "%d", scheduler_pid);

                            execl(round_robin_process->program_name /* file name */, round_robin_process->program_name /* arg0 */, tmp_parent_pid /* arg1 */, NULL); // programa executando ate receber SIGSTOP
                            pause();
                        }
                        else // pai (scheduler)
                        {
                            usleep(1000);   // tempo para variavel  is_io ser atualizada pelo handler caso o processo seja i/o bound
                            if (is_io == 1) // se for I/O bound coloca na fila de espera
                            {
                                printf("Programa %s eh I/O Bound, mandando pra fila de espera ate operacao I/O acabar\n", round_robin_process->program_name);

                                // marcando tempo de inicio que entrou na fila de espera
                                gettimeofday(&current_time, NULL);
                                round_robin_process->io_start_time = current_time.tv_sec;

                                // colocando processo na fila de espera
                                enqueue(waiting_queue, round_robin_process->program_name, round_robin_process->start_time, round_robin_process->duration_time, round_robin_process->pid, round_robin_process->io_start_time);
                                is_io = 0;
                                printf("Fila de espera I/O Bound: ");
                                print_queue(waiting_queue);
                                printf("\n");
                            }

                            else // se for CPU bound apenas espera acabar tempo de execucao e manda de volta para a fila de pronto
                            {
                                while (1)
                                {
                                    gettimeofday(&current_time, NULL);            // atualizando tempo atual
                                    if (current_time.tv_sec - start_time_rr >= 1) // verificando se passou 1 segundo
                                    {
                                        /* se tiver passado 1 segundo, manda SIGSTOP para o processo e coloca ele no final da fila */
                                        printf("Parando a execucao de %s agora\n", round_robin_process->program_name);

                                        // incrementando para chegar ao fim do programa
                                        time_slice_count++;

                                        // envia SIGSTOP pro processo em execucao
                                        kill(round_robin_process->pid, SIGSTOP);

                                        // manda pro final da fila de pronto
                                        enqueue(round_robin_processes_queue, round_robin_process->program_name, round_robin_process->start_time, round_robin_process->duration_time, round_robin_process->pid, round_robin_process->io_start_time);
                                        printf("Fila Round-Robin apos execucao de %s: ", round_robin_process->program_name);
                                        print_queue(round_robin_processes_queue);
                                        printf("\n");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    else // processo que ja foi executado e retornou ao primeiro lugar da fila de prontos
                    {
                        if (round_robin_process->io_start_time != 0) // verificando se eh i/o bound
                        {
                            printf("Executando novamente o programa %s (I/O Bound)\n", round_robin_process->program_name);
                            gettimeofday(&current_time, NULL);
                            round_robin_process->io_start_time = current_time.tv_sec;

                            // colocando processo na fila de espera
                            enqueue(waiting_queue, round_robin_process->program_name, round_robin_process->start_time, round_robin_process->duration_time, round_robin_process->pid, round_robin_process->io_start_time);
                            printf("Fila de espera I/O Bound: ");
                            print_queue(waiting_queue);
                            printf("\n");
                        }

                        else // se nao for io bound continua a execucao padrao
                        {
                            printf("Executando novamente o programa %s (CPU Bound)\n", round_robin_process->program_name);
                            kill(round_robin_process->pid, SIGCONT); // faz o processo retornar a execucao

                            while (1)
                            {
                                gettimeofday(&current_time, NULL);            // atualizando tempo atual
                                if (current_time.tv_sec - start_time_rr >= 1) // verificando se passou 1 segundo
                                {
                                    /* se tiver passado 1 segundo, manda SIGSTOP para o processo e coloca ele no final da fila */
                                    printf("Parando novamente a execucao do programa %s (CPU Bound)\n", round_robin_process->program_name);
                                    time_slice_count++;
                                    kill(round_robin_process->pid, SIGSTOP);                                                                                                                                                                    // envia SIGSTOP para o processo em execucao
                                    enqueue(round_robin_processes_queue, round_robin_process->program_name, round_robin_process->start_time, round_robin_process->duration_time, round_robin_process->pid, round_robin_process->io_start_time); // manda pro final da fila de pronto
                                    printf("Fila Round-Robin apos execucao de %s: ", round_robin_process->program_name);
                                    print_queue(round_robin_processes_queue);
                                    printf("\n");
                                    break;
                                }
                            }
                        }
                    }
                }

            if (!is_queue_empty(real_time_processes_queue))
                if (scheduler_start_time + real_time_processes_queue->head->start_time == current_time.tv_sec) // verifica se atingiu tempo de inicio do processo real time em primeiro na fila
                {
                    int start_time_rt;
                    printf("Fila Real Time: ");
                    print_queue(real_time_processes_queue);
                    start_time_rt = current_time.tv_sec;
                    real_time_process = dequeue(real_time_processes_queue); // pega o primeiro processo na fila

                    printf("Pegando primeiro processo da fila de real time: %s\n", real_time_process->program_name);

                    while (1)
                    {
                        if (real_time_process->pid == 0) // primeira vez que esta executando
                        {
                            printf("Executando o programa %s (Real Time) pela primeira vez\n", real_time_process->program_name);

                            // criando processo filho para executar o programa
                            real_time_process->pid = fork();
                            if (real_time_process->pid == 0) // filho
                            {
                                signal(SIGUSR1, sigusr1_handler);
                                char tmp_parent_pid[24]; // usado apenas para converter o pid do pai para string pra poder passar como argumento (ja que todos devem ser passados como string)
                                sprintf(tmp_parent_pid, "%d", scheduler_pid);

                                execl(real_time_process->program_name, real_time_process->program_name, tmp_parent_pid, NULL); // programa executando ate receber SIGSTOP
                                pause();
                            }
                            else // pai (scheduler)
                            {
                                while (1)
                                {
                                    gettimeofday(&current_time, NULL);                                           // atualizando tempo atual
                                    if (start_time_rt + real_time_process->duration_time <= current_time.tv_sec) // verificando se passou o tempo de duracao do processo
                                    {
                                        /* se tiver passado o tempo de duracao, manda SIGSTOP para o processo e coloca ele no final da fila */
                                        printf("Parando a execucao de %s agora\n", real_time_process->program_name);
                                        kill(real_time_process->pid, SIGSTOP);                                                                                                                                                          // envia SIGSTOP pro processo em execucao
                                        enqueue(real_time_processes_queue, real_time_process->program_name, real_time_process->start_time, real_time_process->duration_time, real_time_process->pid, real_time_process->io_start_time); // manda pro final da fila de pronto
                                        printf("Fila Real Time apos execucao de %s: ", real_time_process->program_name);
                                        print_queue(real_time_processes_queue);
                                        printf("\n");
                                        break;
                                    }
                                }
                            }
                        }

                        else // processo que ja foi executado e retornou ao primeiro lugar da fila de pronto
                        {
                            printf("Executando novamente o programa %s (CPU Bound)\n", real_time_process->program_name);
                            start_time_rt = current_time.tv_sec;   // atualizando tempo de inicio
                            kill(real_time_process->pid, SIGCONT); // faz o processo retornar a execucao

                            while (1)
                            {
                                gettimeofday(&current_time, NULL);                                           // atualizando tempo atual
                                if (start_time_rt + real_time_process->duration_time <= current_time.tv_sec) // verificando se passou 1 segundo
                                {
                                    /* se tiver passado seu tempo de duracao, manda SIGSTOP para o processo e coloca ele no final da fila */
                                    printf("Parando novamente a execucao do programa %s (CPU Bound) agora\n", real_time_process->program_name);
                                    kill(real_time_process->pid, SIGSTOP);                                                                                                                                                          // envia SIGSTOP para o processo em execucao
                                    enqueue(real_time_processes_queue, real_time_process->program_name, real_time_process->start_time, real_time_process->duration_time, real_time_process->pid, real_time_process->io_start_time); // manda pro final da fila de pronto
                                    printf("Fila Real Time apos execucao de %s: ", real_time_process->program_name);
                                    print_queue(real_time_processes_queue);
                                    printf("\n");
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    // atualiza tempo de inicio do scheduler para contar corretamente o tempo para caso exista outro processo real time
                    gettimeofday(&current_time, NULL);
                    scheduler_start_time = current_time.tv_sec;
                }

            if (time_slice_count >= 180)
            {
                printf("Escalonador chegou ao tempo final de execucao\n");
                printf("Finalizando...\n");
                sleep(1);
                printf("Estado final de cada fila:\n");
                printf("Fila Round-Robin: ");
                print_queue(round_robin_processes_queue);
                printf("Fila Real Time: ");
                print_queue(real_time_processes_queue);
                sleep(1);
                break;
            }
        }

        free_queue(waiting_queue);
        free_queue(real_time_processes_queue);
        free_queue(round_robin_processes_queue);

        exit(0);
    }
    wait(&status); // aguardando processo filho terminar

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

    return 0;
}

void init_queue(queue *q) { q->head = NULL, q->tail = NULL; }

int is_queue_empty(queue *q) { return (q->head == NULL); }

void enqueue(queue *q, const char *program_name, int start_time, int duration_time, int pid, int io_start_time)
{
    queue_node *new_node = (queue_node *)malloc(sizeof(queue_node));
    strcpy(new_node->program_name, program_name);
    new_node->start_time = start_time;
    new_node->duration_time = duration_time;
    new_node->pid = pid;
    new_node->io_start_time = io_start_time;
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

int check_conflicting_times(queue *q, int process_start_time, int process_duration_time)
{

    queue_node *current = q->head;

    int process_end_time = process_start_time + process_duration_time;
    int current_end_time;

    while (current != NULL)
    {
        current_end_time = current->start_time + current->duration_time;

        // iniciando ao mesmo tempo
        if (process_start_time == current->start_time)
            return 1;

        // inicia antes e termina depois, entao esta sobrepondo sobre o tempo do outro
        if (process_start_time < current->start_time && process_end_time > current_end_time)
            return 1;
        current = current->next;
    }

    return 0;
}

void sigusr1_handler(int signum) { is_io = 1; }
