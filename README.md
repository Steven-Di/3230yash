# 3230yash - 简易Shell实现

## 项目简介

`3230yash` 是一个基于C语言编写的简易Unix Shell程序，实现了命令解析、管道执行、进程监控等核心功能。该项目作为HKU COMP3230课程的作业实现，展示了Linux系统编程中进程控制、信号处理和文件操作等关键技术。

---

## 功能特性

### 核心功能
| 功能 | 描述 |
|------|------|
| **命令执行** | 支持标准Unix命令的执行，使用`fork()`+`execvp()`实现 |
| **管道支持** | 支持多命令管道（最多4个管道，5个命令），实现进程间通信 |
| **内置命令** | `exit` - 退出Shell；`watch` - 实时监控进程状态 |
| **信号处理** | 自定义SIGINT处理，支持Ctrl-C中断但不终止Shell |
| **错误处理** | 完善的管道语法检查与错误提示 |

### 内置命令详解

#### `exit`
- 无参数时正常退出Shell
- 带参数时提示错误信息

#### `watch <command>`
- 每500ms读取`/proc/[pid]/stat`监控子进程
- 输出字段：进程状态、CPU ID、用户态时间、内核态时间、虚拟内存大小、缺页次数
- 支持检测信号终止并报告

---

## 编译与运行

### 环境要求
- Linux/WSL环境
- GCC编译器
- POSIX兼容系统

### 编译
```bash
gcc -o 3230yash 3230yash.c -Wall -Wextra
```

### 运行
```bash
./3230yash
```

---

## 使用示例

```bash
## 3230yash >> ls -la                    # 执行单条命令
## 3230yash >> cat file.txt | grep keyword | wc -l   # 管道操作
## 3230yash >> watch ./long_running_program          # 监控进程
## 3230yash >> exit                      # 退出Shell
```

---

## 技术实现

### 架构设计
```
┌─────────────────────────────────────┐
│           Main Loop                 │
│  - 信号处理设置 (SIGINT)             │
│  - 命令行读取与解析                   │
│  - 执行策略分发                      │
└─────────────────────────────────────┘
              │
    ┌─────────┴─────────┐
    ▼                   ▼
┌─────────┐       ┌─────────────┐
│ 单命令执行 │       │  管道命令执行  │
│ fork+exec│       │ 多进程+管道通信 │
└─────────┘       └─────────────┘
```

### 关键数据结构
```c
typedef struct {
    char *argv[MAX_FIELDS + 1];  // 命令参数数组
    int argc;                     // 参数计数
} cmd_t;
```

### 信号处理机制
- **父进程**：捕获SIGINT设置标志位，中断当前输入但不退出
- **子进程**：恢复默认SIGINT处理（SIG_DFL），允许Ctrl-C终止当前命令

---

## 错误处理

| 错误类型 | 处理方式 |
|---------|---------|
| 连续管道符 `||` | 提示"should not have two consecutive \|" |
| 管道首尾为 `\|` | 提示"Incorrect pipe sequence" |
| 管道命令过多 | 提示"Too many commands in pipe"（>5个） |
| 参数过多 | 提示"Too many arguments"（>30个） |
| `watch`用于管道 | 提示"Cannot watch a pipe sequence" |
| `watch`无参数 | 提示"watch cannot be a standalone command" |

---

## 项目信息

- **作者**: Di Kaitian (3036291784)
- **开发平台**: WSL2 Ubuntu + VSCode
- **Git仓库**: https://github.com/Steven-Di/3230yash.git
- **课程**: HKU COMP3230 Principles of Operating Systems

---

## 代码规范

- 符合POSIX标准
- 使用`strtok_r`保证线程安全
- 所有系统调用均检查返回值
- 资源泄漏防护（管道FD及时关闭）

---
