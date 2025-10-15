#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char  *line = NULL;   /* getline 会自动 malloc */
    size_t len  = 0;
    ssize_t nread;

    /* 用于保存所有“命令”的缓冲区，可不断扩容 */
    size_t cmd_cap = 1024;
    size_t cmd_len = 0;
    char  *cmd_buf = malloc(cmd_cap);
    if (!cmd_buf) { perror("malloc"); return 1; }

    for (;;) {
        printf("bash$ ");          /* 打印提示符 */
        fflush(stdout);

        nread = getline(&line, &len, stdin);
        if (nread == -1) {         /* Ctrl-D 或其他 EOF */
            putchar('\n');
            break;
        }

        /* 把刚读到的这一行追加到 cmd_buf */
        while (cmd_len + (size_t)nread + 1 > cmd_cap) {
            cmd_cap <<= 1;
            cmd_buf = realloc(cmd_buf, cmd_cap);
            if (!cmd_buf) { perror("realloc"); return 1; }
        }
        memcpy(cmd_buf + cmd_len, line, nread);
        cmd_len += nread;
        cmd_buf[cmd_len] = '\0';   /* 总是保持 NUL 结尾 */

        /* 这里什么都不做，直接进入下一轮循环 */
    }

    /* 如果以后想查看存下来的内容，可打印： */
    /* printf("\n--- 所有输入内容 ---\n%s", cmd_buf); */

    free(line);
    free(cmd_buf);
    return 0;
}