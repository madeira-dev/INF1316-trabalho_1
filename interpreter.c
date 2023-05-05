#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    FILE *file_ptr;
    char program_name[20];
    int start_time;    /* escalonamento round robin */
    int duration_time; /* escalonamento round robin */
    int c;             /* pra iterar pelos char do arquivo */

    // abrindo em read mode
    file_ptr = fopen("exec.txt", "r");

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
                }
                else
                {
                    printf("escalonamento real time\n");
                    printf("%s\n", program_name);
                }
            }
            else
            {
                printf("formatacao invalida\n");
                exit(1);
            }
        }
        sleep(2);
    }
    fclose(file_ptr);

    return 0;
}
