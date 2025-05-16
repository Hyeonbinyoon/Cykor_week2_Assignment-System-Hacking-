#ifndef PARSER_H
#define PARSER_H

#include "tokenizer.h"
#include <stdbool.h>

// 명령어 종류 정의
typedef enum {
    CMD_SIMPLE,
    CMD_PIPE,
    CMD_IF,
    CMD_FOR,
    CMD_WHILE,
    CMD_SUBSHELL,
    CMD_GROUP,
    CMD_SEQUENCE,
    CMD_BACKGROUND,
    CMD_OR,
    CMD_AND
} CommandType;

// 리다이렉션 구조체
typedef struct Redirect {
    char *op;
    char *file;
    struct Redirect *next;
} Redirect;

// Command 구조체 정의
typedef struct Command {
    CommandType type;
    struct Command *left;
    struct Command *right;
    struct Command *condition;
    struct Command *then_block;
    struct Command *else_block;
    char *args[20];
    Redirect *redirects;
    char *heredoc_body;
    int background;
} Command;

// 토큰 스트림 구조체
typedef struct {
    Token *tokens;
    int count;
    int pos;
} TokenStream;

// 파싱 관련 함수
Command *parse_command(TokenStream *stream);
Command *parse_if(TokenStream *stream);
Command *parse_for(TokenStream *stream);
Command *parse_while(TokenStream *stream);
Command *parse_group(TokenStream *stream);
Command *parse_subshell(TokenStream *stream);
Command *parse_simple(TokenStream *stream);
Command *parse_logical(TokenStream *stream);
// 파싱 보조 함수
Token *next_token(TokenStream *stream);
Token *peek_token(TokenStream *stream);
bool match_token(TokenStream *stream, const char *value);
int split_tokens(Token *tokens, int count, const char *sep);
int is_redirect_operator(const char *op);
int needs_filename(const char *op);

// 속성 처리 함수
void parse_redirects(Command *cmd, TokenStream *stream);
void parse_heredoc(Command *cmd, TokenStream *stream);
void parse_background(Command *cmd, TokenStream *stream);

// 트리 출력 및 해제
void print_command_tree(Command *cmd, int indent);
void free_command(Command *cmd);
Command *parse_sequence(TokenStream *stream);
#endif // PARSER_H
