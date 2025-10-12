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

static int
custom_read(int fd, char* buf, int count)
{
    int total = 0;
    while (total < count)
    {
        int n = read(fd, buf + total, count - total);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }
    return total;
}

static int
custom_write(int fd, const char* buf, int count)
{
    int total = 0;
    while (total < count) 
    {
        int n = write(fd, buf + total, count - total);
        if (n < 0) {
            return -1;
        }
        total += n;
    }
    return total;
}

int 
main(void)
{
    int pipe_desc[2];
    char buf[6];

    int pipe_code = pipe(pipe_desc);

    if (pipe_code < 0)
    {
        fprintf(STDERR, "pipe_err");
        exit(FAIL);
    }

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(STDERR, "fork_err\n");
        exit(FAIL);
    }

    const int len_child_msg = sizeof(FROM_CHILD_MSG);
    const int len_parent_msg = sizeof(FROM_PARENT_MSG);

    if (pid > 0) 
    {
        // Родительский процесс
        int bytes_written = custom_write(pipe_desc[1], FROM_CHILD_MSG, len_child_msg);
        if (bytes_written < 0)
        {
            fprintf(STDERR, "error(pid %d): write failed in parent.\n", getpid());
            exit(FAIL);
        }
        if (bytes_written != len_child_msg) {
            fprintf(STDERR, "warning(pid %d): parent wrote %d bytes (expected: %d).\n",
                getpid(), bytes_written, len_child_msg);
        }
        wait(0); // Остановка всех родительских процессов до завершения всех дочерних процессов
        
        int bytes_read = custom_read(pipe_desc[0], buf, len_parent_msg);
        if (bytes_read < 0)
        {
            fprintf(STDERR, "error(pid %d): read failed in parent.\n", getpid());
            exit(FAIL);
        }
        if (bytes_read < len_parent_msg)
        {
            fprintf(STDERR, "warning(pid %d): parent expected %d bytes but got %d. Possible EOF.\n",
                getpid(), len_parent_msg, bytes_read);
        }
        fprintf(STDOUT, "%s%d: got %s %s\n", REDB, getpid(), buf, CRESET);
    } 
    else
    {
        // Дочерний процесс
        int bytes_read = custom_read(pipe_desc[0], buf, len_child_msg);
        if (bytes_read < 0) {
            fprintf(STDERR, "error(pid %d): read failed in child.\n", getpid());
            exit(FAIL);
        }
        if (bytes_read < len_child_msg) {
            fprintf(STDERR, "error(pid %d): child expected %d bytes but got %d (EOF?).\n",
                    getpid(), len_child_msg, bytes_read);
            exit(FAIL);
        }

        fprintf(STDOUT, "%s%d: got %s %s\n", GRNB, getpid(), buf, CRESET);

        int written = custom_write(pipe_desc[1], FROM_PARENT_MSG, len_parent_msg);
        if (written < 0) {
            fprintf(STDERR, "error(pid %d): write failed in child.\n", getpid());
            exit(FAIL);
        }
        if (written != len_parent_msg) {
            fprintf(STDERR, "warning(pid %d): child wrote %d bytes (expected %d).\n",
                    getpid(), written, len_parent_msg);
        }
    }
    exit(NORMAL);
}
