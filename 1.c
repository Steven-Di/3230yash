#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#define MAX_FIELDS   30
#define MAX_LINE_LENGTH 1024
#define MAX_PIPES    4
#define MAX_CMDS     (MAX_PIPES + 1)

static volatile sig_atomic_t got_sigint = 0;   /* 原子标志 */
static void sigint_handler(int sig) { (void)sig; got_sigint = 1; }

typedef struct {
    char *argv[MAX_FIELDS + 1];   /* 以 NULL 结尾 */
    int  argc;
} cmd_t;

/* 先按 | 分段，再按空格分 token；返回命令个数，-1 表示语法错误 */
static int parse_line(char *line, cmd_t cmds[])
{
    int cmd_cnt = 0;
    char *saveptr1 = NULL, *saveptr2 = NULL;

    /* 按 | 拆分 */
    for (char *seg = strtok_r(line, "|", &saveptr1);
         seg;
         seg = strtok_r(NULL, "|", &saveptr1))
    {
        if (cmd_cnt >= MAX_CMDS) {
            fprintf(stderr, "3230yash: too many commands in pipe\n");
            return -1;
        }

        /* 去掉前后空格 */
        while (*seg == ' ' || *seg == '\t') ++seg;
        char *tail = seg + strlen(seg) - 1;
        while (tail > seg && (*tail == ' ' || *tail == '\t')) *tail-- = '\0';

        if (*seg == '\0') {          /* 空命令，如 cat | | wc */
            fprintf(stderr, "3230yash: should not have two consecutive | without in-between command\n");
            return -1;
        }

        /* 按空格拆 argv */
        int argc = 0;
        for (char *tok = strtok_r(seg, " \t", &saveptr2);
             tok;
             tok = strtok_r(NULL, " \t", &saveptr2))
        {
            if (argc >= MAX_FIELDS) {
                fprintf(stderr, "3230yash: too many arguments\n");
                return -1;
            }
            cmds[cmd_cnt].argv[argc++] = tok;
        }
        cmds[cmd_cnt].argv[argc] = NULL;
        cmds[cmd_cnt].argc = argc;
        ++cmd_cnt;
    }

    /* 边界检查：| 开头或结尾 */
    if (line[0] == '|' || line[strlen(line)-1] == '|') {
        fprintf(stderr, "3230yash: Incorrect pipe sequence\n");
        return -1;
    }

    return cmd_cnt;
}

int main(void)
{
    /* ---------- 1. 注册 handler ---------- */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                 /* 不设置 SA_RESTART！ */
    sigaction(SIGINT, &sa, NULL);

    char line[MAX_LINE_LENGTH];
    cmd_t cmds[MAX_CMDS];

    printf("## 3230yash >> ");
    fflush(stdout); 
    while (1) {
        // read line and handle Ctrl-C
        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (got_sigint) {               /* 是 Ctrl-C 导致的 */
                got_sigint = 0;             /* 清标志 */
                clearerr(stdin);            /* 清 stdin 错误状态 */
                printf("\n## 3230yash >> ");
                fflush(stdout);
                continue;                   /* 立即重新提示 */
            }
            break;                          /* 真 EOF (Ctrl-D) */
        }

        // parse line
        line[strcspn(line, "\n")] = '\0';
        int cmd_cnt = parse_line(line, cmds);

        // start executing commands
        for (int i = 0; i < cmd_cnt; ++i) {
            if (strcmp(cmds[i].argv[0], "exit") == 0){
                //execute builtin exit
                if (cmds[0].argv[1]) {          /* 后面还有参数 */
                    printf("3230yash: \"exit\" with other arguments!!!\n");
                } else {                        /* 干净退出 */
                    printf("3230yash: Terminated\n");
                    exit(0);
                }
            } else if (strcmp(cmds[i].argv[0], "watch") == 0){
                //execute builtin watch
            } else {
                //other commands
            }
        }
        
        /* single command

        pid_t pid = fork();
        if (pid == 0) {
            execvp(cmds[0].argv[0], cmds[0].argv);
            fprintf(stderr, "3230yash: '%s': %s\n", cmds[0].argv[0], strerror(errno));
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        } else {
            perror("fork failed");
        }
        */

        printf("## 3230yash >> ");
        fflush(stdout); 
    }
    return 0;
}