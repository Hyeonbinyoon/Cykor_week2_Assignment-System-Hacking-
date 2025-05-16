#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define MAX_TOKEN_LEN 128
#define MAX_TOKENS 256


// 각 토큰의 종류를 구분하기 위한 열거형
typedef enum {
    T_WORD, T_OPERATOR, T_STRING, T_VARIABLE, T_COMMAND_SUB, T_COMMENT,
    T_HEREDOC, T_PATTERN, T_PAREN, T_EOF
} TokenType;


// 상태머신의 상태 정의
typedef enum {
    NORMAL, IN_ESCAPE, IN_SQUOTE, IN_DQUOTE, IN_COMMENT,
    IN_VAR_EXPAND, IN_CMD_SUBST, IN_HEREDOC
} State;


typedef struct {
    TokenType type;              // 토큰의 종류
    char value[MAX_TOKEN_LEN];   // 토큰의 문자열
} Token;


extern const char *token_type_names[];  // 이름 매핑을 위한 배열
extern Token tokens[MAX_TOKENS];  // 전체 토큰들을 저장하는 배열
extern int token_count;           // 현재까지 저장된 토큰의 수


void add_token(TokenType type, const char *start, int len);  // 새로운 토큰 추가
int is_operator_char(char c);                                // 연산자 판별
int is_pattern_char(char c);                                 // 패턴 문자 판별
void tokenize(const char *input);                            // 토큰화 함수
void print_tokens();                                         // 토큰 출력 함수


#endif // TOKENIZER_H
