#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

// 주요 함수 선언
void execute_command(Command *cmd);
int execute_and_get_status(Command *cmd);
void apply_redirection(Redirect *redir);
void execute_pipeline(Command *cmd);

#endif // EXECUTOR_H
