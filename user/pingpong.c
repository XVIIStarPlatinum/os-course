#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/ANSI-color-codes.h"
#include "user/user.h"

#define FROM_CHILD_MSG "ping\0"
#define FROM_PARENT_MSG "pong\0"

enum EXIT_STATES {
    NORMAL = 0,
    FAIL = 1
};

enum STD_STREAMS {
    STDOUT = 1,
    STDERR = 2
};

enum ERROR_MSG {
    PIPE = "pipe_err",
    FORK = "fork_err"
};

int 
main(void)
{
    int pipe_desc[2];
    char buf[6];

    int pipe_code = pipe(pipe_desc);

    if(pipe_code < 0)
    {
        fprintf(STDERR, PIPE);
        exit(FAIL);
    }

    pid_t pid = fork();

    if(pid > 0) 
    {
        // Родительский процесс
        int bytes_written = write(pipe_desc[1], FROM_CHILD_MSG, sizeof(buf));
        if(bytes_written < sizeof(buf))
        {
            fprintf(STDERR, "error(pid %d): Not enough bytes were written in child process.\n", getpid());
            exit(FAIL);
        }
        wait(0); // Серебряная пуля, остановка всех родительских процессов до завершения всех дочерних процессов
        
        int bytes_read = read(pipe_desc[0], buf, sizeof(buf));
        if(bytes_read < sizeof(buf))
        {
            fprintf(STDERR, "error(pid %d): Not enough bytes were read in child process.\n", getpid());
            exit(FAIL);
        }
        fprintf(STDOUT, "%s%d: got %s %s\n", REDB, getpid(), buf, CRESET);
    } 
    else if(pid == 0) 
    {
        // Дочерний процесс
        read(pipe_desc[0], buf, sizeof(buf));
        {
            fprintf(STDERR, "error(pid %d): Not enough bytes were read in child process.\n", getpid());
            exit(FAIL);
        }
        fprintf(STDOUT, "%s%d: got %s %s\n", GRNB, getpid(), buf, CRESET);

        int bytes_written = write(pipe_desc[1], FROM_PARENT_MSG, sizeof(buf));
        if(bytes_written < sizeof(buf))
        {
            fprintf(STDERR, "error(pid %d): Not enough bytes were written in child process.\n", getpid());
            exit(FAIL);
        }
    } 
    else 
    {
        // Ошибка при форк
        fprintf(STDERR, FORK);
        exit(FAIL);
    }

    exit(NORMAL);
}
