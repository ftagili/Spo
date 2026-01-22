#include "../ast/ast.h"
#include "codegen.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Parser generated in build directory */
#include "../../build/parser.tab.h"

extern FILE *yyin;
extern int yyparse(void);
/* expose bison debug flag (available when %debug is used in grammar) */
extern int yydebug;

/* Skip UTF-8 BOM if present at the start of the file. Leaves the file
   position after the BOM (or rewound to start if no BOM). */
static void skip_utf8_bom(FILE *f) {
  if (!f) return;
  unsigned char bom[3];
  size_t n = fread(bom, 1, 3, f);
  if (n != 3 || bom[0] != 0xEF || bom[1] != 0xBB || bom[2] != 0xBF) {
    /* No BOM: rewind to start */
    fseek(f, 0, SEEK_SET);
  }
}

int main(int argc, char **argv) {
  const char *input_file = NULL;
  const char *output_file = NULL;

  /* Parse arguments: support both <input> <output> and <input> -o <output> */
  if (argc == 3) {
    /* Simple format: input output */
    input_file = argv[1];
    output_file = argv[2];
  } else if (argc == 4 && strcmp(argv[2], "-o") == 0) {
    /* Flag format: input -o output */
    input_file = argv[1];
    output_file = argv[3];
  } else {
    fprintf(stderr, "usage: %s <input-file> <output-file>\n", argv[0]);
    fprintf(stderr, "   or: %s <input-file> -o <output-file>\n", argv[0]);
    return 1;
  }

  /* Open input file */
  errno = 0;
  FILE *input_f = fopen(input_file, "rb");
  if (!input_f) {
    fprintf(stderr, "Error: cannot open input file '%s': %s\n", input_file,
            strerror(errno));
    return 1;
  }

  /* Parse input file */
  /* Be tolerant of optional UTF-8 BOM at the start of input files */
  skip_utf8_bom(input_f);
  yyin = input_f;
  /* Enable parser debug when requested via environment (helps debugging) */
  if (getenv("PARSER_DEBUG") != NULL) {
    yydebug = 1;
  }
  int parse_result = yyparse();
  fclose(input_f);

  if (parse_result != 0) {
    fprintf(stderr, "Error: syntax errors in '%s'\n", input_file);
    return 1;
  }

  /* Get AST root */
  ASTNode *root = ast_get_root();
  if (!root) {
    fprintf(stderr, "Error: no AST root produced for '%s'\n", input_file);
    return 1;
  }

  /* Open output file */
  errno = 0;
  FILE *output_f = fopen(output_file, "wb");
  if (!output_f) {
    fprintf(stderr, "Error: cannot open output file '%s': %s\n", output_file,
            strerror(errno));
    return 1;
  }

  /* Generate code */
  int codegen_result = codegen_s390x_from_ast(output_f, root);
  fclose(output_f);

  if (!codegen_result) {
    fprintf(stderr, "Error: code generation failed\n");
    return 1;
  }

  return 0;
}

