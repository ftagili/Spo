#include "../ast/ast.h"
#include "../../build/parser.tab.h"
#include <stdio.h>

extern int parse_error;
extern int yyparse(void);
extern int yydebug;

int main() {
  printf("Parser started. Enter input (Ctrl+D to exit):\n");
  yydebug = 1; /* enable bison debug traces */
  yyparse();
  return parse_error;
}
