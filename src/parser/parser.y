%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ast/ast.h"

void yyerror(const char *s);
int yylex(void);
%}

/* Семантические типы */
%union {
    char* str;
    ASTNode* node;
}

/* Токены */
%token <str> IDENTIFIER
%token <str> IDENT_ARRAY
%token <str> BOOL_LITERAL STRING_LITERAL CHAR_LITERAL HEX_LITERAL BITS_LITERAL DEC_LITERAL
%token IF ELSE WHILE DO BREAK RETURN
%token EXTERN CLASS PUBLIC PRIVATE
%token TEMPLATE
%token <str> BUILTIN_TYPE
%token <str> ELLIPSIS
%token <str> PLUS MINUS STAR SLASH PERCENT
%token <str> LT GT LE GE EQEQ NEQ
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA LBRACKET RBRACKET ASSIGN
%token <str> PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%token COLON DOT NEW AMPERSAND

/* Типы нетерминалов */
%type <node> source sourceItemList sourceItem funcDef funcSignature argList argDefList argDef
%type <node> typeRef statementBlock statementList statement varDecl varList varItemList optAssign
%type <node> ifStmt optElse whileStmt doWhileStmt breakStmt returnStmt exprStmt expr argExprList exprList literal
%type <node> importSpec optImportSpec classDef optBase member memberList field fieldList optModifier optTypeRef optTemplate

%start source

%debug

/* Приоритеты (ниже -> ниже приоритет) */
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%left  EQEQ NEQ LT GT LE GE
%left  PLUS MINUS
%left  STAR SLASH PERCENT
%right UPLUS UMINUS
%left  DOT

%%

source
    : sourceItemList
      { $$ = ast_create_node("source"); ast_add_child($$, $1); ast_set_root($$); }
    ;

sourceItemList
    : /* пусто */
      { $$ = ast_create_node("items"); }
    | sourceItemList sourceItem
      { $$ = $1; ast_add_child($$, $2); }
    ;

sourceItem
    : funcDef
      { $$ = $1; }
    | classDef
      { $$ = $1; }
    ;

funcDef
    : optImportSpec funcSignature statementBlock
      { $$ = ast_create_node("funcDef");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); ast_add_child($$, $3); }
    | optImportSpec funcSignature SEMICOLON
      { $$ = ast_create_node("funcDecl");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    ;

optImportSpec
    : /* пусто */
      { $$ = NULL; }
    | importSpec
      { $$ = $1; }
    ;

importSpec
    : EXTERN LPAREN STRING_LITERAL RPAREN
      { $$ = ast_create_node("import");
        ASTNode* dll = ast_create_leaf_token("dll", $3); free($3);
        ast_add_child($$, dll); }
    | EXTERN LPAREN STRING_LITERAL COMMA STRING_LITERAL RPAREN
      { $$ = ast_create_node("import");
        ASTNode* dll = ast_create_leaf_token("dll", $3); free($3);
        ASTNode* entry = ast_create_leaf_token("entry", $5); free($5);
        ast_add_child($$, dll); ast_add_child($$, entry); }
    ;

funcSignature
    : typeRef IDENTIFIER LPAREN argList RPAREN
      { $$ = ast_create_node("signature");
        ast_add_child($$, $1);
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        ast_add_child($$, $4);
      }
    ;

argList
    : /* пусто */
      { $$ = ast_create_node("args"); }
    | argDefList
      { $$ = ast_create_node("args"); ast_add_child($$, $1); }
    | argDefList COMMA ELLIPSIS
      { $$ = ast_create_node("args"); ast_add_child($$, $1);
        ASTNode* va = ast_create_leaf_token("varargs", $3); free($3); ast_add_child($$, va); }
    | ELLIPSIS
      { $$ = ast_create_node("args"); ASTNode* va = ast_create_leaf_token("varargs", $1); free($1); ast_add_child($$, va); }
    ;

argDefList
    : argDef
      { $$ = ast_create_node("arglist"); ast_add_child($$, $1); }
    | argDefList COMMA argDef
      { $$ = $1; ast_add_child($$, $3); }
    ;

argDef
    : typeRef IDENTIFIER
      { $$ = ast_create_node("arg");
        ast_add_child($$, $1);
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
      }
    ;

typeRef
    : BUILTIN_TYPE
      { $$ = ast_create_leaf_token("type", $1); free($1); }
    | IDENTIFIER
      { $$ = ast_create_leaf_token("typeRef", $1); free($1); }
    /* identifier with [] lexed as a single token by the scanner (IDENT_ARRAY) */
    | IDENT_ARRAY
      { $$ = ast_create_node("array");
        /* create child typeRef node from the identifier name */
        ASTNode* t = ast_create_leaf_token("typeRef", $1); free($1);
        ast_add_child($$, t);
      }
    /* identifier followed by [] (space-separated tokens) e.g. 'T [ ]' or 'T [ ]' */
    | IDENTIFIER LBRACKET RBRACKET
      { $$ = ast_create_node("array");
        ASTNode* t = ast_create_leaf_token("typeRef", $1); free($1);
        ast_add_child($$, t);
      }
    | IDENTIFIER LT typeRef GT
      { $$ = ast_create_node("genType");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id);
        ast_add_child($$, $3);
      }
    /* pointer type: e.g. void* or MyType* */
    | typeRef STAR
      { $$ = ast_create_node("ptr"); ast_add_child($$, $1);
        ASTNode* p = ast_create_leaf_token("ptrsym", $2); free($2); ast_add_child($$, p); }
    | typeRef LBRACKET RBRACKET
      { $$ = ast_create_node("array"); ast_add_child($$, $1); }
    ;

statementBlock
    : LBRACE statementList RBRACE
      { $$ = ast_create_node("block"); ast_add_child($$, $2); }
    ;

statementList
    : /* пусто */
      { $$ = ast_create_node("stmts"); }
    | statementList statement
      { $$ = $1; ast_add_child($$, $2); }
    ;

statement
    : varDecl
      { $$ = $1; }
    | ifStmt
      { $$ = $1; }
    | whileStmt
      { $$ = $1; }
    | doWhileStmt
      { $$ = $1; }
    | breakStmt
      { $$ = $1; }
    | returnStmt
      { $$ = $1; }
    | exprStmt
      { $$ = $1; }
    | statementBlock
      { $$ = $1; }
    ;

varDecl
    : typeRef varList SEMICOLON
      { $$ = ast_create_node("vardecl"); ast_add_child($$, $1); ast_add_child($$, $2); }
    ;

varList
    : varItemList
      { $$ = $1; }
    ;

varItemList
    : IDENTIFIER optAssign
      { $$ = ast_create_node("vars");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); ast_add_child($$, $2);
      }
    | varItemList COMMA IDENTIFIER optAssign
      { $$ = $1;
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, id); ast_add_child($$, $4);
      }
    ;

optAssign
    : /* пусто */
      { $$ = ast_create_node("noinit"); }
    | ASSIGN expr
      { $$ = ast_create_node("assign"); ast_add_child($$, $2); }
    ;

ifStmt
    : IF LPAREN expr RPAREN statement optElse
      { $$ = ast_create_node("if"); ast_add_child($$, $3); ast_add_child($$, $5); ast_add_child($$, $6); }
    ;

optElse
    : /* пусто */
      { $$ = ast_create_node("noelse"); }
    | ELSE statement
      { $$ = ast_create_node("else"); ast_add_child($$, $2); }
    ;

whileStmt
    : WHILE LPAREN expr RPAREN statement
      { $$ = ast_create_node("while"); ast_add_child($$, $3); ast_add_child($$, $5); }
    ;

doWhileStmt
    : DO statementBlock WHILE LPAREN expr RPAREN SEMICOLON
      { $$ = ast_create_node("doWhile"); ast_add_child($$, $2); ast_add_child($$, $5); }
    ;

breakStmt
    : BREAK SEMICOLON
      { $$ = ast_create_node("break"); }
    ;

returnStmt
    : RETURN expr SEMICOLON
      { $$ = ast_create_node("return"); ast_add_child($$, $2); }
    | RETURN SEMICOLON
      { $$ = ast_create_node("return"); }
    ;

exprStmt
    : expr SEMICOLON
      { $$ = ast_create_node("exprstmt"); ast_add_child($$, $1); }
    ;

/* --- выражения --- */
expr
    /* присваивание */
    : IDENTIFIER ASSIGN expr
      { $$ = ast_create_node("assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); }

    /* assign to indexed lvalue: a[expr] = expr */
    | IDENTIFIER LBRACKET expr RBRACKET ASSIGN expr
      { $$ = ast_create_node("assign_index");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); ast_add_child($$, $6); }

    /* составные присваивания */
    | IDENTIFIER PLUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }

    /* allow assignment where LHS is any expression (e.g., a[i], obj.field) */
    | expr ASSIGN expr
      { $$ = ast_create_node("assign");
        ast_add_child($$, $1); ast_add_child($$, $3); }
    /* compound assigns for general lvalues */
    | expr PLUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ast_add_child($$, $1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr MINUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ast_add_child($$, $1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr STAR_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ast_add_child($$, $1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr SLASH_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ast_add_child($$, $1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr PERCENT_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ast_add_child($$, $1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER MINUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER STAR_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER SLASH_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER PERCENT_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }

    /* арифметика */
    | expr STAR    expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr SLASH   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr PERCENT expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }

    | expr PLUS    expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr MINUS   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }

    /* сравнения */
    | expr LT   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr GT   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr LE   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr GE   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr EQEQ expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr NEQ  expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }

    /* унарные + и - */
    | MINUS expr %prec UMINUS
      { $$ = ast_create_node("unop");
        ASTNode* op=ast_create_leaf_token("op", $1); free($1);
        ast_add_child($$, op); ast_add_child($$, $2); }
    | PLUS  expr %prec UPLUS
      { $$ = ast_create_node("unop");
        ASTNode* op=ast_create_leaf_token("op", $1); free($1);
        ast_add_child($$, op); ast_add_child($$, $2); }
    /* адрес переменной &var */
    | AMPERSAND IDENTIFIER
      { $$ = ast_create_node("address");
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id); }

    /* new ClassName(...) */
    | NEW IDENTIFIER LPAREN argExprList RPAREN
      { $$ = ast_create_node("new");
        ASTNode* id=ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        ast_add_child($$, $4);
      }
    /* new ClassName<Type>(...) */
    | NEW IDENTIFIER LT typeRef GT LPAREN argExprList RPAREN
      { $$ = ast_create_node("new");
        ASTNode* id=ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        /* attach generic type parameter */
        ast_add_child($$, $4);
        ast_add_child($$, $7);
      }
    /* new Type[expr] - array allocation form */
    | NEW IDENTIFIER LBRACKET expr RBRACKET
      { $$ = ast_create_node("new");
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        ast_add_child($$, $4);
      }
    /* new ClassName<Type>[expr] - generic array allocation */
    | NEW IDENTIFIER LT typeRef GT LBRACKET expr RBRACKET
      { $$ = ast_create_node("new");
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        ast_add_child($$, $4);
        ast_add_child($$, $7);
      }
    | NEW IDENTIFIER
      { $$ = ast_create_node("new");
        ASTNode* id=ast_create_leaf_token("id", $2); free($2);
        ASTNode* args=ast_create_node("args");
        ast_add_child($$, id);
        ast_add_child($$, args);
      }

    /* доступ к членам: obj.field */
    | expr DOT IDENTIFIER
      { $$ = ast_create_node("fieldAccess");
        ASTNode* id=ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, $1);
        ast_add_child($$, id);
      }

    /* вызов метода: obj.method(args) */
    | expr DOT IDENTIFIER LPAREN argExprList RPAREN
      { $$ = ast_create_node("methodCall");
        ASTNode* id=ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, $1);
        ast_add_child($$, id);
        ast_add_child($$, $5);
      }

    /* индекс после точки (если вдруг понадобится): obj.arr[idx] */
    | expr DOT IDENTIFIER LBRACKET expr RBRACKET
      { $$ = ast_create_node("memberIndex");
        ASTNode* id=ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, $1);
        ast_add_child($$, id);
        ast_add_child($$, $5);
      }

    /* прочее */
    | LPAREN expr RPAREN
      { $$ = $2; }
    | IDENTIFIER
      { $$ = ast_create_leaf_token("id", $1); free($1); }
    | literal
      { $$ = $1; }
    | IDENTIFIER LPAREN argExprList RPAREN
      { $$ = ast_create_node("call");
        ASTNode* id=ast_create_leaf_token("id",$1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); }
    | IDENTIFIER LBRACKET expr RBRACKET
      { $$ = ast_create_node("index");
        ASTNode* id=ast_create_leaf_token("id",$1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); }
    ;

argExprList
    : /* пусто */
      { $$ = ast_create_node("args"); }
    | exprList
      { $$ = ast_create_node("args"); ast_add_child($$, $1); }
    ;

exprList
    : expr
      { $$ = ast_create_node("list"); ast_add_child($$, $1); }
    | exprList COMMA expr
      { $$ = $1; ast_add_child($$, $3); }
    ;

literal
    : BOOL_LITERAL
      { $$ = ast_create_leaf_token("bool", $1); free($1); }
    | STRING_LITERAL
      { $$ = ast_create_leaf_token("string", $1); free($1); }
    | CHAR_LITERAL
      { $$ = ast_create_leaf_token("char", $1); free($1); }
    | HEX_LITERAL
      { $$ = ast_create_leaf_token("hex", $1); free($1); }
    | BITS_LITERAL
      { $$ = ast_create_leaf_token("bits", $1); free($1); }
    | DEC_LITERAL
      { $$ = ast_create_leaf_token("dec", $1); free($1); }
    ;

/* --------- КЛАССЫ / НАСЛЕДОВАНИЕ --------- */

/* Add optional template declaration before class */
optTemplate
    : /* пусто */
      { $$ = NULL; }
    | TEMPLATE LT IDENTIFIER GT
      { $$ = ast_create_node("template");
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, id);
      }
    ;

classDef
    : optTemplate CLASS IDENTIFIER optBase LBRACE memberList RBRACE
      { $$ = ast_create_node("class");
        if ($1) ast_add_child($$, $1); /* template param, if any */
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, id);
        if ($4) ast_add_child($$, $4);
        ast_add_child($$, $6);
      }
    ;

/* optBase: ": BaseClass" или пусто */
optBase
    : /* пусто */
      { $$ = NULL; }
    | COLON IDENTIFIER
      { $$ = ast_create_node("extends");
        ASTNode* base = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, base);
      }
    ;

memberList
    : /* пусто */
      { $$ = ast_create_node("members"); }
    | memberList member
      { $$ = $1; ast_add_child($$, $2); }
    ;

member
    : optModifier funcDef
      { $$ = ast_create_node("member");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    | optModifier typeRef IDENTIFIER LPAREN argList RPAREN statement
      { $$ = ast_create_node("member");
        if ($1) ast_add_child($$, $1);
        /* build signature */
        ASTNode* sig = ast_create_node("signature");
        ast_add_child(sig, $2);
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child(sig, id);
        ast_add_child(sig, $5);
        /* create funcDef node and attach body */
        ASTNode* f = ast_create_node("funcDef");
        ast_add_child(f, sig);
        ast_add_child(f, $7);
        ast_add_child($$, f);
      }
    | optModifier field
      { $$ = ast_create_node("member");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    ;

optModifier
    : /* пусто */
      { $$ = NULL; }
    | PUBLIC
      { $$ = ast_create_leaf_token("modifier", "public"); }
    | PRIVATE
      { $$ = ast_create_leaf_token("modifier", "private"); }
    ;

field
    : typeRef fieldList SEMICOLON
      { $$ = ast_create_node("field");
        ast_add_child($$, $1);
        ast_add_child($$, $2); }
    ;

optTypeRef
    : /* пусто */
      { $$ = NULL; }
    | typeRef
      { $$ = $1; }
    ;

fieldList
    : IDENTIFIER
      { $$ = ast_create_node("fieldlist");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); }
    | fieldList COMMA IDENTIFIER
      { $$ = $1;
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, id); }
    ;

%%

int parse_error = 0;

/* Expose lexer state for better error messages */
extern int yylineno;
extern char *yytext;

void yyerror(const char *s) {
  if (yytext) {
    fprintf(stderr, "Error: %s at line %d near '%s'\n", s, yylineno, yytext);
  } else {
    fprintf(stderr, "Error: %s at line %d\n", s, yylineno);
  }
  parse_error = 1;
}
