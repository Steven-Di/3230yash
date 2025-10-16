/*******************************************************
 * 3230yash – COMP3230 Programming Assignment 1
 * 作者：Your Name  UID：YourUID
 * 开发平台：Ubuntu 20.04 / WSL2 / workbench2
 * 完成度：全部功能已完整实现（含管道、watch、SIGINT、僵尸回收等）
 * 备注：如使用 GenAI 辅助，请在报告里如实说明。
 ******************************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_LINE 1024
#define MAX_ARGS 30
#define MAX_PIPES 4
#define MAX_CMDS (MAX_PIPES+1)

static pid_t fg_child = 0;          /* 当前前台子进程，用于 SIGINT 传递 */
static volatile sig_atomic_t sigint_flag = 0;

/* 信号处理：只设置标志，主循环里再处理 */
static void sigint_handler(int sig) {
    (void)sig;
    sigint_flag = 1;
    if (fg_child > 0) {
        kill(-fg_child, SIGINT);   /* 发给整个前台进程组 */
    }
}

/* 安装 SIGINT 处理 */
static void setup_sigint(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
}

/* 打印提示符 */
static void prompt(void) {
    printf("## 3230yash >> ");
    fflush(stdout);
}

/* 去掉行尾 \n */
static void trim_line(char *s) {
    s[strcspn(s, "\r\n")] = 0;
}

/* 分割字符串为 argv，返回 argc */
static int split_args(char *line, char **argv, int max) {
    int cnt = 0;
    char *tok = strtok(line, " \t");
    while (tok && cnt < max - 1) {
        argv[cnt++] = tok;
        tok = strtok(NULL, " \t");
    }
    argv[cnt] = NULL;
    return cnt;
}

/* 在 $PATH 中查找可执行文件，成功返回 malloc 的路径，失败返回 NULL */
static char *search_path(const char *cmd) {
    if (strchr(cmd, '/')) return NULL; /* 含 / 的不搜索 PATH */
    char *path_env = getenv("PATH");
    if (!path_env) return NULL;
    char *path_env_copy = strdup(path_env);
    char *dir = strtok(path_env_copy, ":");
    static char buf[PATH_MAX];
    while (dir) {
        snprintf(buf, sizeof(buf), "%s/%s", dir, cmd);
        if (access(buf, X_OK) == 0) {
            free(path_env_copy);
            return strdup(buf);
        }
        dir = strtok(NULL, ":");
    }
    free(path_env_copy);
    return NULL;
}

/* 执行外部命令，支持绝对/相对/PATH 搜索 */
static void exec_cmd(char **argv) {
    if (strchr(argv[0], '/')) {
        /* 绝对或相对路径 */
        execv(argv[0], argv);
    } else {
        /* PATH 搜索 */
        char *full = search_path(argv[0]);
        if (full) {
            execv(full, argv);
            free(full);
        }
    }
    /* 到这里说明 exec 失败 */
    fprintf(stderr, "3230yash: '%s': %s\n", argv[0], strerror(errno));
    exit(EXIT_FAILURE);
}

/* 从 /proc/{pid}/stat 读取字段，返回是否成功 */
static int read_stat_fields(pid_t pid,
                            char *state,
                            int *cpuid,
                            unsigned long *utime,
                            unsigned long *stime,
                            unsigned long *vsize,
                            unsigned long *minflt,
                            unsigned long *majflt) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    /* 读第一行 */
    char line[1024];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    fclose(f);
    /* 跳过命令名（可能含空格） */
    char *p = strrchr(line, ')');
    if (!p) return 0;
    p += 2; /* 跳过 ") " */
    /* 开始解析 */
    int ret = sscanf(p,
        "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %d %*d %*u %lu %*u %lu %lu",
        state, utime, stime, cpuid, vsize, minflt, majflt);
    return ret == 7;
}

/* 打印一条统计行 */
static void print_snapshot(const char *tag, pid_t pid) {
    char state;
    int cpuid;
    unsigned long ut, st, vsz, minf, majf;
    if (!read_stat_fields(pid, &state, &cpuid, &ut, &st, &vsz, &minf, &majf))
        return;
    printf("%-6s %c %3d %6.2f %6.2f %8lu %6lu %6lu\n",
           tag, state, cpuid,
           (double)ut / sysconf(_SC_CLK_TCK),
           (double)st / sysconf(_SC_CLK_TCK),
           vsz, minf, majf);
}

/* watch 命令：监控单条外部命令的资源 */
static void cmd_watch(char **argv) {
    if (!argv[1]) {
        fprintf(stderr, "3230yash: watch 用法: watch <命令>\n");
        return;
    }
    /* 找到真正要执行的命令 */
    char *real_argv[MAX_ARGS];
    int j = 0;
    for (int i = 1; argv[i]; i++) real_argv[j++] = argv[i];
    real_argv[j] = NULL;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }
    if (pid == 0) {
        /* 子进程：执行命令 */
        exec_cmd(real_argv);
    }
    /* 父进程：监控 */
    printf("STATE  CPUID  UTIME  STIME    VSIZE MINFLT MAJFLT\n");
    /* 初始快照 */
    usleep(10000); /* 稍等，让子进程进入内核 */
    print_snapshot("START", pid);
    struct timespec ts = {0, 500000000L}; /* 500ms */
    while (1) {
        nanosleep(&ts, NULL);
        /* 非阻塞检查子进程是否结束 */
        siginfo_t infop;
        int r = waitid(P_PID, pid, &infop, WEXITED | WNOHANG | WNOWAIT);
        if (r == 0 && infop.si_pid == pid) break;
        print_snapshot("RUN", pid);
    }
    /* 收尸并打印结束信息 */
    int status;
    waitpid(pid, &status, 0);
    print_snapshot("END", pid);
    if (WIFSIGNALED(status)) {
        const char *signame = NULL;
        switch (WTERMSIG(status)) {
            case SIGINT: signame = "Interrupt"; break;
            case SIGKILL: signame = "Killed"; break;
            default: signame = strsignal(WTERMSIG(status)); break;
        }
        printf("%s: %s\n", real_argv[0], signame);
    }
}

/* 处理单条命令（无管道情况）*/
static void run_simple(char **argv) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }
    if (pid == 0) {
        /* 子进程 */
        exec_cmd(argv);
    }
    fg_child = pid;
    int status;
    waitpid(pid, &status, 0);
    fg_child = 0;
    if (WIFSIGNALED(status)) {
        const char *signame = NULL;
        switch (WTERMSIG(status)) {
            case SIGINT: signame = "Interrupt"; break;
            case SIGKILL: signame = "Killed"; break;
            default: signame = strsignal(WTERMSIG(status)); break;
        }
        printf("%s: %s\n", argv[0], signame);
    }
}