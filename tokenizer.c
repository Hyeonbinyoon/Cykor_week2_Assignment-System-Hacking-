#include "tokenizer.h"

#define STATE_STACK_MAX 128
State state_stack[STATE_STACK_MAX];
int state_stack_top = 0;                         // 상태 저장을 위한 스택

void push_state(State s) {
    if (state_stack_top < STATE_STACK_MAX)
        state_stack[state_stack_top++] = s;         
}                                                // 상태를 push하는 함수

State pop_state() {
    if (state_stack_top > 0)
        return state_stack[--state_stack_top];
    return NORMAL;
}                                                // 상태를 pop하는 함수

const char *token_type_names[] = {
    "WORD", "OPERATOR", "STRING", "VARIABLE", "COMMAND_SUB", "COMMENT",
    "HEREDOC", "PATTERN", "PAREN", "EOF"
};

Token tokens[MAX_TOKENS];
int token_count = 0;

void add_token(TokenType type, const char *start, int len) {
    if (token_count >= MAX_TOKENS) return;
    Token *t = &tokens[token_count++];
    int copy_len = len < MAX_TOKEN_LEN - 1 ? len : MAX_TOKEN_LEN - 1;  // 오버플로우 방지
    strncpy(t->value, start, copy_len);
    t->value[copy_len] = '\0';               // 토큰 문자열 복사
    t->type = type;                         // 토큰 종류 저장
}

// 연산자 토큰 길이를 반환하는 함수
int is_operator_token(const char *p) {
    if (strncmp(p, "&&", 2) == 0) return 2;
    if (strncmp(p, "||", 2) == 0) return 2;
    if (strncmp(p, "&>>", 3) == 0) return 3;
    if (strncmp(p, "&>", 2) == 0) return 2;
    if (strncmp(p, ">>", 2) == 0) return 2;
    if (strncmp(p, "<<", 2) == 0) return 2;
    if (strncmp(p, "<>", 2) == 0) return 2;
    // 나머지 FD 리다이렉션이나 단일 기호 처리 ...


    // FD + 리다이렉션 (예: 2>, 123>&1, 5<&-) 처리 구간간
    const char *q = p;
    while (isdigit(*q)) q++;  // FD 숫자 부분 스캔 (예: 2, 123)
    if (*q == '>' || *q == '<') {
        q++;
        if (*q == '&') {
            q++;  // 리다이렉션 기호 > 또는 <
            while (isdigit(*q)) q++; // 뒤쪽 FD 숫자 스캔 (예: 1, 5)
            if (*q == '-') q++;  // - 기호 (FD 닫기) 확인
        }
        return q - p;  // 전체 길이 반환 (예: 2>&1 → 4, 123>&- → 7)
    }

    if (*p == '>' || *p == '<' || *p == '&' || *p == '|' || *p == ';') return 1;

    return 0; // 연산자 아님
}


int is_pattern_char(char c) {
    return c == '*' || c == '?' || c == '[';
}

void tokenize(const char *input) {
    const char *p = input;       // 순회를 위한 포인터
    const char *start = input;   // 시작 포인터
    State state = NORMAL;        // 현재 상태 초기화
    int depth = 0;               // 중첩 괄호 depth 관리

    while (*p) {
        if (token_count >= MAX_TOKENS) { p++; continue; }  // MAX_TOKENS 초과 방어

        switch (state) {
            case NORMAL:
            if (isspace(*p) || *p == '\r') { p++; start = p; continue; }
            if (*p == '#') { state = IN_COMMENT; start = p; p++; continue; }
            if (*p == '\\' && *(p + 1) == ';') { add_token(T_WORD, p, 2); p += 2; start = p; continue; }
            if (*p == '{' && *(p + 1) == '}') { add_token(T_WORD, p, 2); p += 2; start = p; continue; }
            if (*p == '\\') { push_state(state); state = IN_ESCAPE; p++; continue; }
            if (*p == '\'') { state = IN_SQUOTE; start = ++p; continue; }
            if (*p == '\"') { push_state(state); state = IN_DQUOTE; start = ++p; continue; }
            if (*p == '$' && *(p + 1) == '{') { push_state(state); state = IN_VAR_EXPAND; start = p; p += 2; continue; }
            if (*p == '$' && *(p + 1) == '(') { push_state(state); state = IN_CMD_SUBST; start = p; p += 2; depth = 1; continue; }

            int op_len = is_operator_token(p);  // ← 변경: is_operator_token() 호출 추가
            if (op_len > 0) {
                if (op_len == 2 && strncmp(p, "<<", 2) == 0) {  // ← heredoc 구분 처리 추가
                    add_token(T_HEREDOC, p, op_len);
                    p += op_len;
                    state = IN_HEREDOC;
                    start = p;
                    continue;
                }
                add_token(T_OPERATOR, p, op_len);
                p += op_len;
                start = p;
                continue;
            }

            if (*p == '(' || *p == ')') { add_token(T_PAREN, p, 1); p++; start = p; continue; }

            if (*p == '[') {
                if (p == input || isspace(*(p - 1))) {
                    add_token(T_WORD, p, 1);
                } else {
                    add_token(T_PATTERN, p, 1);
                }
                p++;
                start = p;
                continue;
            }

            if (is_pattern_char(*p)) { add_token(T_PATTERN, p, 1); p++; start = p; continue; }

            if (*p == '-') {
                start = p++;
                while (*p && !isspace(*p) && *p != '\r') {
                    int next_op_len = is_operator_token(p);
                    if (next_op_len > 0) break;
                    p++;
                }
                add_token(T_WORD, start, p - start);
                continue;
            }

            start = p;
            while (*p) {
                if (isspace(*p) || *p == '\r') break;
                int next_op_len = is_operator_token(p);
                if (next_op_len > 0) break;
                if (*p == '(' || *p == ')') break;

                if (*p == '$' && (isalpha(*(p + 1)) || *(p + 1) == '_')) {
                    if (p > start) add_token(T_WORD, start, p - start);
                    start = p;
                    p++;
                    while (isalnum(*p) || *p == '_') p++;
                    add_token(T_VARIABLE, start, p - start);
                    start = p;
                    continue;
                }
                p++;
            }
            if (p > start) add_token(T_WORD, start, p - start);
            break;

            case IN_ESCAPE:
                if (*p) p++;  
                state = pop_state();
                break;

            case IN_SQUOTE:
                if (*p == '\'') { add_token(T_STRING, start, p - start); p++; state = NORMAL; start = p; }
                else p++;
                break;

            case IN_DQUOTE:
                if (*p == '\"') { 
                    if (p > start) add_token(T_STRING, start, p - start); 
                    p++; state = pop_state(); start = p; }
                else if (*p == '\\') { push_state(state); state = IN_ESCAPE; p++; }
                else if (*p == '$' && *(p + 1) == '{') { 
                    if (p > start) add_token(T_STRING, start, p - start);
                    push_state(state); state = IN_VAR_EXPAND; start = p; p += 2; }
                else if (*p == '$' && *(p + 1) == '(') { 
                    if (p > start) add_token(T_STRING, start, p - start);
                    push_state(state); state = IN_CMD_SUBST; start = p; p += 2; depth = 1; }
                else if (*p == '$' && (isalpha(*(p + 1)) || *(p + 1) == '_')) { 
                    if (p > start) add_token(T_STRING, start, p - start);
                    push_state(state);  state = IN_VAR_EXPAND;  start = p; p++; }
                else {
                    p++;  // 개선: DQUOTE 안에서는 is_operator_char 체크하지 않고 그냥 진행
                }
                break;

            case IN_COMMENT:
                while (*p && *p != '\n' && *p != '\r') p++;  // \r\n 호환 추가
                add_token(T_COMMENT, start, p - start);
                state = NORMAL;
                start = p;
                break;

            case IN_VAR_EXPAND:
            if (*(start + 1) == '{') {
                while (*p && *p != '}') p++;
                if (*p == '}') p++;
            } else {
                while (isalnum(*p) || *p == '_') p++;
            }
            add_token(T_VARIABLE, start, p - start);
            state = pop_state();
            start = p;
            break;
        
        case IN_CMD_SUBST:
            while (*p && depth > 0) {
                if (*p == '$' && *(p + 1) == '(') { depth++; p += 2; continue; }
                if (*p == ')') { depth--; p++; continue; }
                p++;
            }
            if (depth == 0) {
                add_token(T_COMMAND_SUB, start, p - start);
                state = pop_state();
                start = p;
            } else {
                fprintf(stderr, "Error: unterminated state detected (%d)\n", state);
                exit(EXIT_FAILURE);
            }
            break;
        
            case IN_HEREDOC:
            while (*p) {
                if (*p == '\n' || *p == '\r') {
                    p++;
                    const char *line_start = p;
                    const char *line_end = p;
                    while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;

                    int line_len = line_end - line_start;
                    if (line_len == 3 && strncmp(line_start, "EOF", 3) == 0) {
                        add_token(T_HEREDOC, start, line_start - start - 1);
                        p = line_end;
                        state = NORMAL;
                        break;
                    }
                    p = line_end;
                } else {
                    p++;
                }
            }

            if (state == IN_HEREDOC) {
                fprintf(stderr, "Error: unterminated HEREDOC detected\n");
                exit(EXIT_FAILURE);
            }
            break;
        
        }
    }

    if (state != NORMAL) {
        fprintf(stderr, "Warning: unterminated state detected (%d)\n", state);
        exit(EXIT_FAILURE);
    }

    add_token(T_EOF, "", 0);
}

void print_tokens() {
    for (int i = 0; i < token_count; i++) {
        printf("[%s] '%s'\n", token_type_names[tokens[i].type], tokens[i].value);
    }
}

