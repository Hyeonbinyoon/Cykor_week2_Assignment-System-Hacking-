#include "tokenizer.h"
#include <stdbool.h>
#include "parser.h"

Command *parse_simple(TokenStream *stream);
Command *parse_command(TokenStream *stream);
Command *parse_pipeline(TokenStream *stream);
Command *parse_sequence(TokenStream *stream);
Command *parse_sequence_until(TokenStream *stream, const char *end_token);
Command *parse_logical(TokenStream *stream);

Token *next_token(TokenStream *stream) {
    if (stream->pos >= stream->count)
        return NULL;
    return &stream->tokens[stream->pos++];
}

Token *peek_token(TokenStream *stream) {
    if (stream->pos >= stream->count)
        return NULL;
    return &stream->tokens[stream->pos];
}

bool match_token(TokenStream *stream, const char *value) {
    Token *tok = peek_token(stream);
    if (tok && strcmp(tok->value, value) == 0) {
        next_token(stream);
        return true;
    }
    return false;
}

int split_tokens(Token *tokens, int count, const char *sep) {
    for (int i = 0; i < count; i++) {
        if (strcmp(tokens[i].value, sep) == 0)
            return i;
    }
    return -1;  // 못 찾으면 -1
}

void free_redirects(Redirect *redir) {
    while (redir) {
        Redirect *next = redir->next;
        free(redir->op);
        if (redir->file) free(redir->file);
        free(redir);
        redir = next;
    }
}

void free_command(Command *cmd) {
    if (!cmd) return;

    free_command(cmd->left);
    free_command(cmd->right);
    free_command(cmd->condition);
    free_command(cmd->then_block);
    free_command(cmd->else_block);

    for (int i = 0; cmd->args[i]; i++)
        free(cmd->args[i]);

    free_redirects(cmd->redirects);

    if (cmd->heredoc_body)
        free(cmd->heredoc_body);

    free(cmd);
}

int needs_filename(const char *op) {
    const char *p = op;

    // FD redirect (예: 2>, 3<, 123>&1, 5<&-)
    while (isdigit(*p)) p++;

    if (*p == '>' || *p == '<') {
        if (*(p + 1) == '&') {
            return 0;  // FD 이동 (예: 2>&1) → 파일명 불필요
        }
        return 1;      // FD + > 또는 < (예: 2> file) → 파일 필요
    }

    // 표준 리다이렉션 기호
    if (strcmp(op, ">") == 0 || strcmp(op, "<") == 0 ||
        strcmp(op, ">>") == 0 || strcmp(op, "&>") == 0 ||
        strcmp(op, "&>>") == 0 || strcmp(op, "<>") == 0) {
        return 1;      // 파일 필요
    }

    return 0;          // 그 외 → 파일 불필요
}

void parse_redirects(Command *cmd, TokenStream *stream) {
    Token *tok = next_token(stream);  // operator 소비
    if (!tok) return;

    char *op = strdup(tok->value);    // 연산자 복사
    char *file = NULL;

    if (needs_filename(op)) {
        Token *target = next_token(stream);
        if (!target || target->type != T_WORD) {
            fprintf(stderr, "Error: expected filename after '%s'\n", op);
            free(op);
            return;
        }
        file = strdup(target->value);
    }

    Redirect *redir = malloc(sizeof(Redirect));
    redir->op = op;
    redir->file = file;
    redir->next = NULL;

    if (!cmd->redirects) {
        cmd->redirects = redir;
    } else {
        Redirect *cur = cmd->redirects;
        while (cur->next)
            cur = cur->next;
        cur->next = redir;
    }
}

Command *parse_background_cmd(Command *left, TokenStream *stream) {
    Token *tok = peek_token(stream);
    if (!tok || tok->type != T_OPERATOR || strcmp(tok->value, "&") != 0)
        return left;

    next_token(stream); // '&' 소비

    Token *next = peek_token(stream);
    if (next && (next->type == T_OPERATOR && strcmp(next->value, ";") == 0)) {
        fprintf(stderr, "Error: '&' must not be followed by ';'\n");
        return NULL;
    }

    if (next && next->type != T_EOF) {
        fprintf(stderr, "Error: '&' must be followed by EOF\n");
        return NULL;
    }

    Command *cmd = malloc(sizeof(Command));
    memset(cmd, 0, sizeof(Command));
    cmd->type = CMD_BACKGROUND;
    cmd->left = left;
    return cmd;
}




void parse_heredoc(Command *cmd, TokenStream *stream) {
    Token *op = next_token(stream);  // << 연산자 소비
    if (!op || strcmp(op->value, "<<") != 0) {
        fprintf(stderr, "Error: expected '<<'\n");
        return;
    }

    Token *delim_token = next_token(stream);  // delimiter 읽기 (예: EOF)
    if (!delim_token || delim_token->type != T_WORD) {
        fprintf(stderr, "Error: expected heredoc delimiter after '<<'\n");
        return;
    }

    const char *delimiter = delim_token->value;

    // 본문 수집
    char buffer[4096] = {0};  // 최대 4KB 본문
    size_t offset = 0;

    while (true) {
        char line[512];
        if (!fgets(line, sizeof(line), stdin)) {
            fprintf(stderr, "Error: unexpected EOF while reading heredoc\n");
            return;  // [보완] 원래 break로 빠져나와서 아래 코드 진행 위험 → return으로 안전 종료
        }

        // 줄 끝 '\n' 제거
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        // 종료 식별자 확인
        if (strcmp(line, delimiter) == 0)
            break;

        size_t total_needed = offset + len + 1 + 1;  // [보완] 기존 +2 계산이 불명확 → line + '\n' + '\0' 명확히 분리
        if (total_needed >= sizeof(buffer)) {
            fprintf(stderr, "Error: heredoc body too large\n");
            return;  // [보완] break 대신 return으로 안전 종료
        }

        //  [보완] strcat 대신 memcpy + offset으로 직접 추가 → strcat 반복 사용은 느림(O(n²)) → memcpy로 개선
        memcpy(buffer + offset, line, len);
        offset += len;

        buffer[offset++] = '\n';  // 개행 추가
        buffer[offset] = '\0';    // 널 종료 유지
    }

    cmd->heredoc_body = strdup(buffer);
}

int is_redirect_operator(const char *op) {
    if (strcmp(op, ">") == 0 || strcmp(op, "<") == 0 ||
        strcmp(op, ">>") == 0 || strcmp(op, "&>") == 0 ||
        strcmp(op, "&>>") == 0 || strcmp(op, "<>") == 0)
        return 1;

    // FD 리다이렉션 패턴 (예: 2>, 123>&1)
    const char *p = op;
    while (isdigit(*p)) p++;
    if (*p == '>' || *p == '<')
        return 1;
    return 0;
}

Command *parse_logical(TokenStream *stream) {
    Command *left = parse_pipeline(stream);
    if (!left) return NULL;

    while (1) {
        Token *tok = peek_token(stream);
        if (!tok || tok->type != T_OPERATOR) break;

        CommandType type;
        if (strcmp(tok->value, "&&") == 0) {
            type = CMD_AND;
        } else if (strcmp(tok->value, "||") == 0) {
            type = CMD_OR;
        } else {
            break;
        }

        next_token(stream);  // && 또는 || 소비

        Command *right = parse_logical(stream);  // 재귀적으로 처리!
        if (!right) {
            fprintf(stderr, "Error: expected command after '%s'\n", tok->value);
            free_command(left);
            return NULL;
        }

        Command *cmd = malloc(sizeof(Command));
        memset(cmd, 0, sizeof(Command));
        cmd->type = type;
        cmd->left = left;
        cmd->right = right;

        left = cmd;
    }

    return left;
}

Command *parse_pipeline(TokenStream *stream) {
    Command *left = parse_simple(stream);
    if (!left) return NULL;

    while (true) {
        Token *tok = peek_token(stream);
        if (!tok || tok->type != T_OPERATOR || strcmp(tok->value, "|") != 0)
            break;

        next_token(stream);  // '|' 소비
        Command *right = parse_simple(stream);
        if (!right) {
            fprintf(stderr, "Error: expected command after '|'\n");
            free_command(left);
            return NULL;
        }

        Command *pipe = malloc(sizeof(Command));
        memset(pipe, 0, sizeof(Command));
        pipe->type = CMD_PIPE;
        pipe->left = left;
        pipe->right = right;

        left = pipe;  // ⭐ 핵심: 트리를 왼쪽으로 누적 연결
    }

    return left;
}


Command *parse_simple(TokenStream *stream) {
    Command *cmd = malloc(sizeof(Command));
    memset(cmd, 0, sizeof(Command));
    cmd->type = CMD_SIMPLE;

    int argc = 0;
    Token *tok;

    while ((tok = peek_token(stream)) != NULL) {
        if (tok->type == T_WORD || tok->type == T_VARIABLE || tok->type == T_STRING) {
            tok = next_token(stream);
            cmd->args[argc++] = strdup(tok->value);
        } else if (tok->type == T_OPERATOR && is_redirect_operator(tok->value)) {
            parse_redirects(cmd, stream);
        } else {
            break;
        }
    }

    cmd->args[argc] = NULL;
    return cmd;
}

Command *parse_until(TokenStream *stream, const char *end_token) {
    Command *left = parse_logical(stream);
    if (!left) return NULL;

    while (1) {
        Token *tok = peek_token(stream);
        if (!tok || tok->type != T_OPERATOR || strcmp(tok->value, ";") != 0)
            break;

        next_token(stream); // consume ';'
        tok = peek_token(stream);
        if (!tok || (tok->type == T_WORD && strcmp(tok->value, end_token) == 0))
            break;

        Command *right = parse_logical(stream);
        if (!right) break;

        Command *seq = malloc(sizeof(Command));
        memset(seq, 0, sizeof(Command));
        seq->type = CMD_SEQUENCE;
        seq->left = left;
        seq->right = right;
        left = seq;
    }

    return left;
}


Command *parse_sequence(TokenStream *stream) {
    Command *left = parse_logical(stream);
    if (!left) return NULL;

    while (1) {
        Token *tok = peek_token(stream);
        if (!tok) break;

        if (tok->type == T_OPERATOR && strcmp(tok->value, "&") == 0) {
            next_token(stream); // '&' 소비

            Command *bg = calloc(1, sizeof(Command));
            bg->type = CMD_BACKGROUND;
            bg->left = left;

            Token *next = peek_token(stream);

            // 다음에 또 명령어가 있다면 시퀀스로 이어붙이기
            if (next && next->type != T_EOF) {
                Command *right = parse_sequence(stream);
                if (!right) return bg;

                Command *seq = calloc(1, sizeof(Command));
                seq->type = CMD_SEQUENCE;
                seq->left = bg;
                seq->right = right;
                return seq;
            } else {
                return bg;
            }
        }

        if (tok->type == T_OPERATOR && strcmp(tok->value, ";") == 0) {
            next_token(stream);
            Token *next = peek_token(stream);
            if (!next || next->type == T_EOF) break;

            Command *right = parse_logical(stream);
            if (!right) break;

            Command *seq = calloc(1, sizeof(Command));
            seq->type = CMD_SEQUENCE;
            seq->left = left;
            seq->right = right;
            left = seq;
        } else {
            break;
        }
    }

    return left;
}



Command *parse_sequence_until(TokenStream *stream, const char *end_token) {
    Command *left = parse_pipeline(stream);
    if (!left) return NULL;

    Token *tok = peek_token(stream);
    while (tok && tok->type == T_OPERATOR && strcmp(tok->value, ";") == 0) {
        next_token(stream);
        tok = peek_token(stream);

        if (tok && strcmp(tok->value, end_token) == 0)
            break;

        Command *right = parse_pipeline(stream);
        if (!right) break;

        Command *seq = malloc(sizeof(Command));
        memset(seq, 0, sizeof(Command));
        seq->type = CMD_SEQUENCE;
        seq->left = left;
        seq->right = right;

        left = seq;
        tok = peek_token(stream);
    }
    return left;
}

Command *parse_if(TokenStream *stream) {
    Token *tok = next_token(stream); // consume 'if'
    if (!tok || strcmp(tok->value, "if") != 0) {
        fprintf(stderr, "Error: expected 'if'\n");
        return NULL;
    }

    Command *cmd = calloc(1, sizeof(Command));
    cmd->type = CMD_IF;

    // condition 파싱을 then 이전까지만
    cmd->condition = parse_until(stream, "then");

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "then") != 0) {
        fprintf(stderr, "Error: expected 'then'\n");
        free_command(cmd);
        return NULL;
    }

    cmd->then_block = parse_sequence_until(stream, "else");

    tok = peek_token(stream);
    if (tok && strcmp(tok->value, "else") == 0) {
        next_token(stream);
        cmd->else_block = parse_sequence_until(stream, "fi");
    }

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "fi") != 0) {
        fprintf(stderr, "Error: expected 'fi'\n");
        free_command(cmd);
        return NULL;
    }

    return cmd;
}


Command *parse_for(TokenStream *stream) {
    Token *tok = next_token(stream);
    if (!tok || strcmp(tok->value, "for") != 0) {
        fprintf(stderr, "Error: expected 'for'\n");
        return NULL;
    }

    Command *cmd = malloc(sizeof(Command));
    memset(cmd, 0, sizeof(Command));
    cmd->type = CMD_FOR;

    tok = next_token(stream);
    if (!tok || tok->type != T_WORD) {
        fprintf(stderr, "Error: expected variable after 'for'\n");
        free(cmd);
        return NULL;
    }
    cmd->args[0] = strdup(tok->value);

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "in") != 0) {
        fprintf(stderr, "Error: expected 'in' after variable\n");
        free(cmd->args[0]);
        free(cmd);
        return NULL;
    }

    int argc = 1;
    while ((tok = peek_token(stream)) && tok->type == T_WORD) {
        tok = next_token(stream);
        cmd->args[argc++] = strdup(tok->value);
    }
    cmd->args[argc] = NULL;

    tok = peek_token(stream);
    if (tok && tok->type == T_OPERATOR && strcmp(tok->value, ";") == 0)
        next_token(stream);

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "do") != 0) {
        fprintf(stderr, "Error: expected 'do'\n");
        for (int i = 0; i < argc; i++) free(cmd->args[i]);
        free(cmd);
        return NULL;
    }

    cmd->then_block = parse_sequence_until(stream, "done");

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "done") != 0) {
        fprintf(stderr, "Error: expected 'done'\n");
        for (int i = 0; i < argc; i++) free(cmd->args[i]);
        free_command(cmd->then_block);
        free(cmd);
        return NULL;
    }

    return cmd;
}

Command *parse_while(TokenStream *stream) {
    Token *tok = next_token(stream);
    if (!tok || strcmp(tok->value, "while") != 0) {
        fprintf(stderr, "Error: expected 'while'\n");
        return NULL;
    }

    Command *cmd = malloc(sizeof(Command));
    memset(cmd, 0, sizeof(Command));
    cmd->type = CMD_WHILE;

    cmd->condition = parse_command(stream);
    if (!cmd->condition) {
        fprintf(stderr, "Error: expected condition after 'while'\n");
        free(cmd);
        return NULL;
    }

    tok = peek_token(stream);
    if (tok && tok->type == T_OPERATOR && strcmp(tok->value, ";") == 0)
        next_token(stream);

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "do") != 0) {
        fprintf(stderr, "Error: expected 'do'\n");
        free_command(cmd->condition);
        free(cmd);
        return NULL;
    }

    cmd->then_block = parse_sequence_until(stream, "done");

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "done") != 0) {
        fprintf(stderr, "Error: expected 'done'\n");
        free_command(cmd->condition);
        free_command(cmd->then_block);
        free(cmd);
        return NULL;
    }

    return cmd;
}

Command *parse_group(TokenStream *stream) {
    Token *tok = next_token(stream);
    if (!tok || strcmp(tok->value, "{") != 0) {
        fprintf(stderr, "Error: expected '{'\n");
        return NULL;
    }

    Command *cmd = malloc(sizeof(Command));
    memset(cmd, 0, sizeof(Command));
    cmd->type = CMD_GROUP;

    cmd->left = parse_sequence_until(stream, "}");

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, "}") != 0) {
        fprintf(stderr, "Error: expected '}'\n");
        free_command(cmd);
        return NULL;
    }

    return cmd;
}

Command *parse_subshell(TokenStream *stream) {
    Token *tok = next_token(stream);
    if (!tok || strcmp(tok->value, "(") != 0) {
        fprintf(stderr, "Error: expected '('\n");
        return NULL;
    }

    Command *cmd = malloc(sizeof(Command));
    memset(cmd, 0, sizeof(Command));
    cmd->type = CMD_SUBSHELL;

    cmd->left = parse_sequence_until(stream, ")");

    tok = next_token(stream);
    if (!tok || strcmp(tok->value, ")") != 0) {
        fprintf(stderr, "Error: expected ')'\n");
        free_command(cmd);
        return NULL;
    }

    return cmd;
}

Command *parse_command(TokenStream *stream) {
  // parse_command()
Command *cmd = parse_sequence(stream);
Token *tok = peek_token(stream);
if (tok && tok->type == T_OPERATOR && strcmp(tok->value, "&") == 0) {
    next_token(stream);

    // 다음 토큰 확인
    Token *next = peek_token(stream);
    if (next && next->type != T_EOF && !(next->type == T_OPERATOR && strcmp(next->value, ";") == 0)) {
        fprintf(stderr, "Error: '&' must be followed by EOF or ';'\n");
        return NULL;
    }

    Command *bg = malloc(sizeof(Command));
    memset(bg, 0, sizeof(Command));
    bg->type = CMD_BACKGROUND;
    bg->left = cmd;
    return bg;
}
return cmd;

}





void print_indent(int indent) {
    for (int i = 0; i < indent; i++)
        printf("  ");
}

void print_command_tree(Command *cmd, int indent) {
    if (!cmd) return;
    print_indent(indent);
    printf("Command Type: ");
    switch (cmd->type) {
        case CMD_SIMPLE:
            printf("SIMPLE\n");
            print_indent(indent + 1);
            printf("Args:");
            for (int i = 0; cmd->args[i]; i++)
                printf(" %s", cmd->args[i]);
            printf("\n");
            break;
        case CMD_PIPE:
            printf("PIPE\n");
            print_command_tree(cmd->left, indent + 1);
            print_command_tree(cmd->right, indent + 1);
            break;
        case CMD_SEQUENCE:
            printf("SEQUENCE\n");
            print_command_tree(cmd->left, indent + 1);
            print_command_tree(cmd->right, indent + 1);
            break;
        case CMD_AND:
            printf("AND\n");
            print_command_tree(cmd->left, indent + 1);
            print_command_tree(cmd->right, indent + 1);
            break;
        case CMD_OR:
            printf("OR\n");
            print_command_tree(cmd->left, indent + 1);
            print_command_tree(cmd->right, indent + 1);
            break;
        case CMD_IF:
            printf("IF\n");
            print_indent(indent + 1); printf("Condition:\n");
            print_command_tree(cmd->condition, indent + 2);
            print_indent(indent + 1); printf("Then:\n");
            print_command_tree(cmd->then_block, indent + 2);
            if (cmd->else_block) {
                print_indent(indent + 1); printf("Else:\n");
                print_command_tree(cmd->else_block, indent + 2);
            }
            break;
        case CMD_FOR:
            printf("FOR\n");
            print_indent(indent + 1);
            printf("Var: %s\n", cmd->args[0]);
            print_indent(indent + 1);
            printf("List:");
            for (int i = 1; cmd->args[i]; i++) printf(" %s", cmd->args[i]);
            printf("\n");
            print_indent(indent + 1); printf("Do:\n");
            print_command_tree(cmd->then_block, indent + 2);
            break;
        case CMD_WHILE:
            printf("WHILE\n");
            print_indent(indent + 1); printf("Condition:\n");
            print_command_tree(cmd->condition, indent + 2);
            print_indent(indent + 1); printf("Do:\n");
            print_command_tree(cmd->then_block, indent + 2);
            break;
        case CMD_GROUP:
            printf("GROUP\n");
            print_command_tree(cmd->left, indent + 1);
            break;
        case CMD_SUBSHELL:
            printf("SUBSHELL\n");
            print_command_tree(cmd->left, indent + 1);
            break;
        case CMD_BACKGROUND:
            printf("BACKGROUND\n");
            print_command_tree(cmd->left, indent + 1);
    break;

        default:
            printf("UNKNOWN COMMAND TYPE\n");
            break;
    }
}
