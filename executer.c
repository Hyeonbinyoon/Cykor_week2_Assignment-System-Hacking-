#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include "parser.h"
#include "tokenizer.h"
#include "executer.h"
#include <ctype.h>
#include <fcntl.h>

void execute_command(Command *cmd);
int execute_and_get_status(Command *cmd);

void apply_redirection(Redirect *redir) {
    for (; redir; redir = redir->next) {
        const char *op = redir->op;
        const char *file = redir->file;
        int target_fd = STDOUT_FILENO;  // 기본은 STDOUT
        int fd;

        // 1. 앞에 FD가 붙어있으면 추출 (예: 2>, 3>>)
        const char *p = op;
        while (isdigit(*p)) p++;
        if (p != op) {
            char fd_buf[8] = {0};
            strncpy(fd_buf, op, p - op);
            target_fd = atoi(fd_buf);  // 예: "2>" → 2
        }

        // 2. 리다이렉션 종류 확인
        if (strcmp(p, ">") == 0) {
            fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open >"); continue; }
            dup2(fd, target_fd);
            close(fd);
        } else if (strcmp(p, ">>") == 0) {
            fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror("open >>"); continue; }
            dup2(fd, target_fd);
            close(fd);
        } else if (strcmp(p, "<") == 0) {
            fd = open(file, O_RDONLY);
            if (fd < 0) { perror("open <"); continue; }
            dup2(fd, target_fd);
            close(fd);
        } else if (strcmp(p, "<>") == 0) {
            fd = open(file, O_RDWR | O_CREAT, 0644);
            if (fd < 0) { perror("open <>"); continue; }
            dup2(fd, target_fd);
            close(fd);
        } else if (strcmp(p, ">&") == 0) {
            // file은 fd 문자열이어야 함 (예: 2>&1)
            int dest_fd = atoi(file);
            dup2(dest_fd, target_fd);
        } else if (strcmp(p, "<&-") == 0 || strcmp(p, ">&-") == 0) {
            // FD 닫기 (예: 2>&-)
            close(target_fd);
        } else if (strcmp(p, "&>") == 0) {
            // STDOUT, STDERR 모두 같은 파일로
            fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open &>"); continue; }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        } else if (strcmp(p, "&>>") == 0) {
            fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror("open &>>"); continue; }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        } else {
            fprintf(stderr, "[!] Unsupported redirection: %s\n", op);
        }
    }
}



void execute_pipeline(Command *cmd) {
    int pipefd[2];
    int in_fd = 0;  // stdin
    pid_t pid;

    while (cmd && cmd->type == CMD_PIPE) {
        pipe(pipefd);

        if ((pid = fork()) == 0) {
            dup2(in_fd, 0);            // 이전 파이프의 읽기 쪽을 stdin으로
            dup2(pipefd[1], 1);        // pipe 출력 -> stdout
            close(pipefd[0]);          // 읽기 끝 닫기
            execute_command(cmd->left);
            exit(0);
        } else {
            waitpid(pid, NULL, 0);
            close(pipefd[1]);          // 쓰기 끝 닫기
            in_fd = pipefd[0];         // 다음 명령어는 이걸 stdin으로 씀
            cmd = cmd->right;
        }
    }

    if (cmd) {
        if ((pid = fork()) == 0) {
            dup2(in_fd, 0);
            execute_command(cmd);
            exit(0);
        } else {
            waitpid(pid, NULL, 0);
        }
    }
}


int execute_and_get_status(Command *cmd) {
    if (!cmd) return 1;

    if (cmd->type == CMD_SIMPLE) {
        if (!cmd->args[0]) return 1;

        //내부 명령어 cd, pwd 수행
        if (strcmp(cmd->args[0], "cd") == 0) {
    const char *path = cmd->args[1];
    
    if (!path) {
        // case: cd
        path = getenv("HOME");
    } else if (strcmp(path, "~") == 0) {
        // case: cd ~
        path = getenv("HOME");
    } else if (strcmp(path, "-") == 0) {
        // case: cd -
        path = getenv("OLDPWD");
        if (path)
            printf("%s\n", path); // cd -는 이동 경로 출력해야 함
    } else if (path[0] == '~') {
        // case: cd ~/something
        const char *home = getenv("HOME");
        if (home) {
            static char expanded[1024];
            snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1); // '~' 이후 경로 붙이기
            path = expanded;
        }
    }

    if (chdir(path) != 0) {
        perror("cd");
        return 1;
    }

    // OLDPWD 업데이트
    setenv("OLDPWD", getenv("PWD"), 1);
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)))
        setenv("PWD", cwd, 1);

    return 0;
}

    if (strcmp(cmd->args[0], "pwd") == 0) {
    int use_physical = 0; // -P 옵션 여부

    // 옵션 파싱
    if (cmd->args[1]) {
        if (strcmp(cmd->args[1], "-P") == 0) {
            use_physical = 1;
        } else if (strcmp(cmd->args[1], "-L") != 0) {
            fprintf(stderr, "pwd: invalid option -- '%s'\n", cmd->args[1]);
            return 1;
        }
    }

    if (use_physical) {
        char real[1024];
        if (getcwd(real, sizeof(real)))
            printf("%s\n", real);
        else
            perror("pwd");
    } else {
        // 논리 경로: PWD 환경변수 사용
        const char *pwd = getenv("PWD");
        if (pwd && *pwd)
            printf("%s\n", pwd);
        else {
            // Fallback
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)))
                printf("%s\n", cwd);
            else
                perror("pwd");
        }
    }
    return 0;
}

        if (strcmp(cmd->args[0], "true") == 0) return 0;
        if (strcmp(cmd->args[0], "false") == 0) return 1;

        //외부 명령어 수행
        pid_t pid = fork();
        if (pid == 0) {
            if (cmd->redirects) apply_redirection(cmd->redirects);
            execvp(cmd->args[0], cmd->args);
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        } else {
            perror("fork");
            return 1;
        }
    }

    // 다른 타입의 명령(CMD_IF, CMD_WHILE 등)은 execute_command() 재귀 호출로 처리
    execute_command(cmd);
    return 0;
}



void execute_command(Command *cmd) {
    if (!cmd) return;

    switch (cmd->type) {
        case CMD_SIMPLE:
            execute_and_get_status(cmd);
            break;

        case CMD_SEQUENCE:
            execute_command(cmd->left);
            execute_command(cmd->right);
            break;

        case CMD_PIPE: {
        execute_pipeline(cmd);
        break;
        }


        case CMD_AND: {
            int status = execute_and_get_status(cmd->left);
            if (status == 0)
                execute_command(cmd->right);
            break;
        }

        case CMD_OR: {
            int status = execute_and_get_status(cmd->left);
            if (status != 0)
                execute_command(cmd->right);
            break;
        }

        case CMD_BACKGROUND: {
            pid_t pid = fork();
            if (pid == 0) {
                execute_command(cmd->left); exit(0);
            }
            if (cmd->right) execute_command(cmd->right);
            break;
        }

        case CMD_IF: {
            int status = execute_and_get_status(cmd->condition);
            if (status == 0) {
                execute_command(cmd->then_block);
            } else if (cmd->else_block) {
                execute_command(cmd->else_block);
            }
            break;
        }

        case CMD_FOR:
            for (int i = 1; cmd->args[i]; i++) {
                setenv(cmd->args[0], cmd->args[i], 1);
                execute_command(cmd->then_block);
            }
            break;

        case CMD_WHILE:
            while (1) {
                int status = execute_and_get_status(cmd->condition);
                if (status != 0) break;
                execute_command(cmd->then_block);
            }
            break;

        case CMD_GROUP:
        case CMD_SUBSHELL:
            execute_command(cmd->left);
            break;

        default:
            fprintf(stderr, "Unknown command type\n");
            break;
    }
}
