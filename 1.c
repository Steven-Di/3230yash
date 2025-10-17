/**********************************************************
* Student Name and No.: Di Kaitian 3036291784
* Development platform: WSL2 Ubuntu connected to VSCode
* Remark: Complete all features as required
************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#define MAX_FIELDS 30
#define MAX_LINE_LENGTH 1024
#define MAX_PIPES 4
#define MAX_CMDS (MAX_PIPES + 1)

// parent process SIGINT handler
static volatile sig_atomic_t got_sigint = 0;
static void sigint_handler(int sig)
{
    (void)sig;
    got_sigint = 1;
}

// command structure, storing argv and argc (one struct for one line's commands)
typedef struct
{
    char *argv[MAX_FIELDS + 1];
    int argc;
} cmd_t;

// parse functing: parse every line into cmds array, return command count, or -1 on error
static int parse_line(char *line, cmd_t cmds[])
{
    int cmd_cnt = 0;                         // command count
    char *saveptr1 = NULL, *saveptr2 = NULL; // for strtok_r

    // check invalid pipe sequence
    if (line[0] == '|' || line[strlen(line) - 1] == '|')
    {
        fprintf(stderr, "3230yash: Incorrect pipe sequence\n");
        return -1;
    }

    // outer separate loop, split by '|' to split commands. after this the cmds array is filled
    for (char *seg = strtok_r(line, "|", &saveptr1);
         seg;
         seg = strtok_r(NULL, "|", &saveptr1))
    {
        // exceed max commands
        if (cmd_cnt >= MAX_CMDS)
        {
            fprintf(stderr, "3230yash: too many commands in pipe\n");
            return -1;
        }

        // delete leading/trailing spaces/tabs
        while (*seg == ' ' || *seg == '\t')
            ++seg;
        char *tail = seg + strlen(seg) - 1;
        while (tail > seg && (*tail == ' ' || *tail == '\t'))
            *tail-- = '\0';

        if (*seg == '\0')
        {
            // empty command between pipes
            fprintf(stderr, "3230yash: should not have two consecutive | without in-between command\n");
            return -1;
        }

        // inner separate loop, split by spaces/tabs to fill argv
        int argc = 0;
        for (char *tok = strtok_r(seg, " \t", &saveptr2);
             tok;
             tok = strtok_r(NULL, " \t", &saveptr2))
        {
            // exceed max fields
            if (argc >= MAX_FIELDS)
            {
                fprintf(stderr, "3230yash: too many arguments\n");
                return -1;
            }
            cmds[cmd_cnt].argv[argc++] = tok;
        }
        cmds[cmd_cnt].argv[argc] = NULL;
        cmds[cmd_cnt].argc = argc;
        ++cmd_cnt;
    }

    return cmd_cnt;
}

// main function
int main(void)
{
    // set up SIGINT handler for parent process, react to Ctrl-C
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // storage for line and commands
    char line[MAX_LINE_LENGTH];
    cmd_t cmds[MAX_CMDS];

    // first prompt
    printf("## 3230yash >> ");
    fflush(stdout);

    // main loop
    while (1)
    {
        // read line and handle Ctrl-C
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            if (got_sigint)
            {
                // this is Ctrl-C, not EOF
                got_sigint = 0;  // reset flag
                clearerr(stdin); // clear EOF flag
                printf("\n## 3230yash >> ");
                fflush(stdout);
                continue; // continue to next loop iteration
            }
            break; // EOF encountered, exit shell
        }

        // parse line into commands
        // after this, cmds array is filled. proceed to execute
        line[strcspn(line, "\n")] = '\0';
        int cmd_cnt = parse_line(line, cmds);

        // execute commands part

        // check command count
        if (cmd_cnt > 1)
        {
            // multiple commands using pipe, check for built-in "watch" command, which is invalid with pipes
            if (strcmp(cmds[0].argv[0], "watch") == 0)
            {
                fprintf(stderr, "3230yash: Cannot watch a pipe sequence\n");
            }
            else
            {

                // handle multiple commands with pipes
                
                // this part creates pipes, forks and execs each command, connects pipes accordingly.
                // parent process waits for all child processes to finish and reports if any terminated by signal

                // create pipes and fork+exec each command
                int pipefd[MAX_PIPES][2];
                pid_t pid[MAX_CMDS];

                // create pipes
                for (int i = 0; i < cmd_cnt - 1; ++i)
                {
                    if (pipe(pipefd[i]) < 0)
                    {
                        perror("pipe");
                        exit(1);
                    }
                }

                // fork and exec each command
                for (int i = 0; i < cmd_cnt; ++i)
                {
                    pid[i] = fork();
                    if (pid[i] == 0)
                    {
                        // child process, set default SIGINT handler for ctrl-C
                        signal(SIGINT, SIG_DFL);

                        // duplicate fds for stdin and stdout
                        if (i > 0)
                        {
                            dup2(pipefd[i - 1][0], STDIN_FILENO);
                        }

                        if (i < cmd_cnt - 1)
                        {
                            dup2(pipefd[i][1], STDOUT_FILENO);
                        }
                        // close all pipe fds in child
                        for (int j = 0; j < cmd_cnt - 1; ++j)
                        {
                            close(pipefd[j][0]);
                            close(pipefd[j][1]);
                        }
                        // execute command
                        execvp(cmds[i].argv[0], cmds[i].argv);
                        fprintf(stderr, "3230yash: Fail to execute '%s': %s\n", cmds[i].argv[0], strerror(errno));
                        exit(1);
                    }
                    else if (pid[i] < 0)
                    {
                        perror("fork");
                        exit(1);
                    }
                }

                // parent process, close all pipe fds
                for (int i = 0; i < cmd_cnt - 1; ++i)
                {
                    close(pipefd[i][0]);
                    close(pipefd[i][1]);
                }

                // wait for all child processes and report if terminated by signal
                for (int i = 0; i < cmd_cnt; ++i)
                {
                    int status;
                    pid_t w;
                    do
                    {
                        w = waitpid(pid[i], &status, 0);
                    } while (w == -1 && errno == EINTR);

                    if (w == -1)
                    {
                        perror("waitpid");
                    }
                    else if (WIFSIGNALED(status))
                    {
                        int sig = WTERMSIG(status);
                        printf("%s: %s\n", cmds[0].argv[0], strsignal(sig));
                    }
                }
            }
        }
        else if (cmd_cnt == 1)
        {

            // single command, check for built-in commands "exit" and "watch"
            if (strcmp(cmds[0].argv[0], "exit") == 0)
            {
                // built-in exit command

                // this command just exits the shell, with error if arguments are given
                if (cmds[0].argv[1])
                {
                    // exit with arguments is invalid
                    printf("3230yash: \"exit\" with other arguments!!!\n");
                }
                else
                {
                    // clean exit
                    printf("3230yash: Terminated\n");
                    exit(0);
                }
            }
            else if (strcmp(cmds[0].argv[0], "watch") == 0)
            {
                // built-in watch command

                // this command executes another command and monitors its /proc/[pid]/stat info every 500 ms.
                // child process runs the command, parent process gets the stats from /proc/[pid]/stat
                // and prints the relevant fields in a formatted manner before, during, and after execution.
                if (cmds[0].argc < 2)
                {
                    // invalid usage
                    printf("3230yash: \"watch\" cannot be a standalone command\n");
                    printf("##3230yash >> ");
                    fflush(stdout);
                    continue;
                }

                pid_t wpid = fork();
                if (wpid == 0)
                {
                    // child process: execute the command to be watched
                    signal(SIGINT, SIG_DFL);
                    execvp(cmds[0].argv[1], cmds[0].argv + 1);
                    fprintf(stderr, "3230yash: '%s': %s\n", cmds[0].argv[1], strerror(errno));
                    _exit(127);
                }
                else if (wpid < 0)
                {
                    perror("fork");
                    continue;
                }

                // parent process: monitor the child process

                // print header line
                printf("STATE CPUID UTIME STIME VSIZE   MINFLT MAJFLT\n");

                // prepare stat path for /proc/[pid]/stat
                char stat_path[32];
                snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)wpid);

                // loop to read and print stats every 500 ms
                while (1)
                {
                    // read /proc/[pid]/stat
                    int st;
                    int fd = open(stat_path, O_RDONLY);
                    char buf[512] = {0};
                    read(fd, buf, sizeof(buf) - 1);
                    close(fd);

                    // parse needed fields from stat
                    char state;
                    int cpu, minflt, majflt;
                    unsigned int utime, stime, vsize;
                    if (sscanf(buf, "%*d %*s %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %u %u %*d %*d %u %*u %*u %*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %u %u",
                               &state, &cpu, &utime, &stime, &vsize, &minflt, &majflt) != 7)
                        continue; // parse error, try again

                    // print the stats line
                    printf("%c %d %.2f %.2f %u %u %u\n",
                           state, cpu,
                           (double)utime / 100.0,
                           (double)stime / 100.0,
                           vsize, minflt, majflt);

                    if (waitpid(wpid, &st, WNOHANG) == wpid)
                        break; // child process has terminated

                    if (fd < 0)
                        break; // child process has terminated

                    usleep(500000); // sleep for 500 ms
                }

                // final wait to get exit status, similar codes as above
                int status;
                waitpid(wpid, &status, 0);

                int fd = open(stat_path, O_RDONLY);
                if (fd >= 0)
                {
                    char buf[512] = {0};
                    read(fd, buf, sizeof(buf) - 1);
                    close(fd);
                    char state;
                    int cpu, minflt, majflt;
                    unsigned int utime, stime, vsize;
                    if (sscanf(buf, "%*d %*s %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %u %u %*d %*d %u %*u %*u %*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %u %u",
                               &state, &cpu, &utime, &stime, &vsize, &minflt, &majflt) == 7)
                    {
                        // print final stats line
                        printf("%c %d %.2f %.2f %u\n%u %u\n",
                               state, cpu,
                               (double)utime / 100.0,
                               (double)stime / 100.0,
                               vsize, minflt, majflt);
                    }
                }

                // report if terminated by signal
                if (WIFSIGNALED(status))
                {
                    int sig = WTERMSIG(status);
                    printf("%s: %s\n", cmds[0].argv[1], strsignal(sig));
                }
            }

            else
            {
                // single normal command, fork and exec to execute

                // the parent process waits for the child process to finish and reports if terminated by signal
                // while the child process sets default SIGINT handler for ctrl-C before exec and execs the command.
                pid_t pid = fork();
                if (pid == 0)
                {
                    // child process, set default SIGINT handler for ctrl-C, then exec
                    signal(SIGINT, SIG_DFL);
                    execvp(cmds[0].argv[0], cmds[0].argv);
                    fprintf(stderr, "3230yash: '%s': %s\n", cmds[0].argv[0], strerror(errno));
                    _exit(127);
                }
                else if (pid > 0)
                {
                    // parent process, wait for child and report if terminated by signal
                    int status;
                    pid_t w;

                    // wait for the child process

                    // note: there will be a problem if only using wait(&status) one time here, because
                    // if a SIGINT is received during wait, it will be interrupted and return -1
                    do
                    {
                        w = waitpid(pid, &status, 0);
                    } while (w == -1 && errno == EINTR);

                    // check waitpid result
                    if (w == -1)
                    {
                        // error
                        perror("waitpid");
                    }
                    else if (WIFSIGNALED(status))
                    {
                        // child terminated by signal
                        int sig = WTERMSIG(status);
                        printf("%s: %s\n", cmds[0].argv[0], strsignal(sig));
                    }
                }
                else
                {
                    // fork failed
                    perror("fork failed");
                }
            }
        }

        // print prompt for next command
        printf("## 3230yash >> ");
        fflush(stdout);
    }
    return 0;
}