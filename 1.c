#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>    // open, O_RDONLY
#include <sys/stat.h> // 可选，但 fcntl.h 已足够

#define MAX_FIELDS 30
#define MAX_LINE_LENGTH 1024
#define MAX_PIPES 4
#define MAX_CMDS (MAX_PIPES + 1)

static volatile sig_atomic_t got_sigint = 0; /* 原子标志 */
static void sigint_handler(int sig)
{
    (void)sig;
    got_sigint = 1;
}

// 命令结构体
typedef struct
{
    char *argv[MAX_FIELDS + 1]; /* 以 NULL 结尾 */
    int argc;
} cmd_t;

// 解析输入行，填充 cmds 数组，返回命令数量，失败返回 -1
static int parse_line(char *line, cmd_t cmds[])
{
    int cmd_cnt = 0;
    char *saveptr1 = NULL, *saveptr2 = NULL;

    /* 边界检查：| 开头或结尾 */
    if (line[0] == '|' || line[strlen(line) - 1] == '|')
    {
        fprintf(stderr, "3230yash: Incorrect pipe sequence\n");
        return -1;
    }

    /* 按 | 拆分 */
    for (char *seg = strtok_r(line, "|", &saveptr1);
         seg;
         seg = strtok_r(NULL, "|", &saveptr1))
    {
        if (cmd_cnt >= MAX_CMDS)
        {
            fprintf(stderr, "3230yash: too many commands in pipe\n");
            return -1;
        }

        /* 去掉前后空格 */
        while (*seg == ' ' || *seg == '\t')
            ++seg;
        char *tail = seg + strlen(seg) - 1;
        while (tail > seg && (*tail == ' ' || *tail == '\t'))
            *tail-- = '\0';

        if (*seg == '\0')
        { /* 空命令，如 cat | | wc */
            fprintf(stderr, "3230yash: should not have two consecutive | without in-between command\n");
            return -1;
        }

        /* 按空格拆 argv */
        int argc = 0;
        for (char *tok = strtok_r(seg, " \t", &saveptr2);
             tok;
             tok = strtok_r(NULL, " \t", &saveptr2))
        {
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

// 主函数
int main(void)
{
    // 设置 SIGINT 处理函数
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* 不设置 SA_RESTART！ */
    sigaction(SIGINT, &sa, NULL);

    // 存储指令
    char line[MAX_LINE_LENGTH];
    cmd_t cmds[MAX_CMDS];

    printf("## 3230yash >> ");
    fflush(stdout);

    // 主循环
    while (1)
    {
        // 读行，处理ctrl-c
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            if (got_sigint)
            {                    /* 是 Ctrl-C 导致的 */
                got_sigint = 0;  /* 清标志 */
                clearerr(stdin); /* 清 stdin 错误状态 */
                printf("\n## 3230yash >> ");
                fflush(stdout);
                continue; /* 立即重新提示 */
            }
            break; /* 真 EOF (Ctrl-D) */
        }

        // 解析行
        line[strcspn(line, "\n")] = '\0';
        int cmd_cnt = parse_line(line, cmds);

        // 区分一行或多行
        if (cmd_cnt > 1)
        {
            if (strcmp(cmds[0].argv[0], "watch") == 0)
            {
                fprintf(stderr, "3230yash: Cannot watch a pipe sequence\n");
            }
            else
            {

                // 多条命令，处理 pipe
                int pipefd[MAX_PIPES][2]; /* 最多 4 条管道 */
                pid_t pid[MAX_CMDS];

                /* 1. 提前创建所有管道 */
                for (int i = 0; i < cmd_cnt - 1; ++i)
                {
                    if (pipe(pipefd[i]) < 0)
                    {
                        perror("pipe");
                        exit(1);
                    }
                }

                /* 2. 逐个 fork + exec */
                for (int i = 0; i < cmd_cnt; ++i)
                {
                    pid[i] = fork();
                    if (pid[i] == 0)
                    { /* 子进程 */
                        /* 恢复默认 SIGINT */
                        signal(SIGINT, SIG_DFL);

                        /* 重定向 stdin（除了第一个） */
                        if (i > 0)
                        {
                            dup2(pipefd[i - 1][0], STDIN_FILENO);
                        }
                        /* 重定向 stdout（除了最后一个） */
                        if (i < cmd_cnt - 1)
                        {
                            dup2(pipefd[i][1], STDOUT_FILENO);
                        }
                        /* 关闭所有管道 fd */
                        for (int j = 0; j < cmd_cnt - 1; ++j)
                        {
                            close(pipefd[j][0]);
                            close(pipefd[j][1]);
                        }
                        /* 执行命令 */
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

                /* 3. 父进程关闭所有管道 fd */
                for (int i = 0; i < cmd_cnt - 1; ++i)
                {
                    close(pipefd[i][0]);
                    close(pipefd[i][1]);
                }

                /* 4. 等待所有子进程，打印信号名 */
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
                        printf("%s: %s: %d\n", cmds[0].argv[0], strsignal(sig), sig);
                    }
                }
            }
        }
        else if (cmd_cnt == 1)
        {

            // 单条命令,处理内置命令，否则正常执行
            if (strcmp(cmds[0].argv[0], "exit") == 0)
            {
                // 内置 exit 命令
                if (cmds[0].argv[1])
                { /* 后面还有参数 */
                    printf("3230yash: \"exit\" with other arguments!!!\n");
                }
                else
                { /* 干净退出 */
                    printf("3230yash: Terminated\n");
                    exit(0);
                }
            }
            else if (strcmp(cmds[0].argv[0], "watch") == 0)
            {

                // 内置 watch 命令
                if (cmds[0].argc < 2)
                {
                    printf("3230yash: \"watch\" cannot be a standalone command\n");
                    printf("##3230yash >> ");
                    continue;
                }

                /* 1. 先打印表头（题目要求） */
                printf("STATE  CPUID UTIME STIME VSIZE   MINFLT MAJFLT\n");

                /* 2. fork 子进程去跑目标命令 */
                pid_t wpid = fork();
                if (wpid == 0)
                {
                    signal(SIGINT, SIG_DFL);
                    execvp(cmds[0].argv[1], cmds[0].argv + 1);
                    _exit(127); /* exec 失败也不打印，留给父进程 */
                }
                else if (wpid < 0)
                {
                    perror("fork");
                    continue;
                }

                /* 3. 每 500 ms 采样 /proc/pid/stat 直到子进程结束 */
                char stat_path[32];
                snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)wpid);

                while (1)
                {
                    int status;
                    if (waitpid(wpid, &status, WNOHANG) == wpid)
                        break; /* 子进程已结束 */

                    int fd = open(stat_path, O_RDONLY);
                    if (fd < 0)
                        break; /* 进程已消失 */
                    char buf[512] = {0};
                    read(fd, buf, sizeof(buf) - 1);
                    close(fd);

                    char state;
                    int cpu;
                    unsigned int utime, stime, vsize, minflt, majflt;

                    sscanf(buf, "%*d %*s %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %u %u %*d %*d %u %*u %*u %*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %u %u",
                           &state, &cpu, &utime, &stime, &vsize, &minflt, &majflt);

                    /* 题目格式：STATE CPUID UTIME STIME VSIZE MINFLT MAJFLT */
                    printf("%c %d %.2f %.2f %u %d %d\n",
                           state, cpu,
                           (double)utime / 100.0, /* 滴答→秒 */
                           (double)stime / 100.0,
                           vsize, minflt, majflt);

                    usleep(500000); /* 500 ms */
                }

                /* 4. 子进程已结束，再采一次“结束后”快照（题目要求） */
                int status;
                waitpid(wpid, &status, 0); /* 收尸 */
                if (WIFSIGNALED(status))
                {
                    int sig = WTERMSIG(status);
                    printf("%s:%s:%d\n", cmds[0].argv[1],
                           sig == SIGINT ? "interrupt" : "killed", sig);
                }
            }
            else
            {
                // 单条正常命令，直接执行
                pid_t pid = fork();
                if (pid == 0)
                {
                    signal(SIGINT, SIG_DFL);
                    execvp(cmds[0].argv[0], cmds[0].argv);
                    fprintf(stderr, "3230yash: '%s': %s\n", cmds[0].argv[0], strerror(errno));
                    _exit(127);
                }
                else if (pid > 0)
                {
                    int status;
                    pid_t w;
                    do
                    {
                        w = waitpid(pid, &status, 0);
                    } while (w == -1 && errno == EINTR);

                    if (w == -1)
                    {
                        perror("waitpid");
                    }
                    else if (WIFSIGNALED(status))
                    {
                        int sig = WTERMSIG(status);
                        printf("%s: %s: %d\n", cmds[0].argv[0], strsignal(sig), sig);
                    }
                }
                else
                {
                    perror("fork failed");
                }
            }
        }

        printf("## 3230yash >> ");
        fflush(stdout);
    }
    return 0;
}