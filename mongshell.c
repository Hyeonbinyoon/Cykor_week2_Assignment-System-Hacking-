#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include "tokenizer.h"
#include "parser.h"
#include "executer.h"
void show_prompt() {
    char hostname[1024];
    char cwd[1024];
    char* home;
    struct passwd* pw;

    pw = getpwuid(geteuid());
    if (pw == NULL) {
        perror("getpwuid 실패");
        return;
    }

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname 실패");
        return;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd 실패");
        return;
    }

    home = getenv("HOME");
    if (home != NULL && strstr(cwd, home) == cwd) {
        printf("%s@%s:~%s$ ", pw->pw_name, hostname, cwd + strlen(home));
    } else {
        printf("%s@%s:%s$ ", pw->pw_name, hostname, cwd);
    }
}

int main() {
    char command[1024];

    while (1) {
        show_prompt();

        if (fgets(command, sizeof(command), stdin) == NULL) {
            printf("\n");
            break;
        }

        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "exit") == 0) {
            break;
        }

        token_count = 0;
        tokenize(command);
        

        TokenStream stream = {
            .tokens = tokens,
            .count = token_count,
            .pos = 0
        };

        Command *cmd = parse_command(&stream);
        if (cmd) {
            
            execute_command(cmd);        // 실제 명령 실행
            free_command(cmd);
        }
    }

    return 0;
}
