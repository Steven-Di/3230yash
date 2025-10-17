#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

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