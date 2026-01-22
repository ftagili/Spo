%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ast/ast.h"

void yyerror(const char *s);
int yylex(void);
%}

%union {
    char* str;
    ASTNode* node;
}

/* Токены */
%token <str> IDENTIFIER
%token <str> BOOL_LITERAL STRING_LITERAL CHAR_LITERAL HEX_LITERAL BITS_LITERAL DEC_LITERAL
%token IF ELSE WHILE DO BREAK RETURN
%token EXTERN CLASS PUBLIC PRIVATE
%token TEMPLATE
%token <str> BUILTIN_TYPE
%token <str> PLUS MINUS STAR SLASH PERCENT
%token <str> LT GT LE GE EQEQ NEQ
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA LBRACKET RBRACKET ASSIGN
%token <str> PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%token COLON DOT NEW AMPERSAND

%type <node> source sourceItemList sourceItem
%type <node> funcDef funcSignature argList argDefList argDef
%type <node> typeRef statementBlock statementList statement
%type <node> varDecl varList varItemList optAssign
%type <node> ifStmt optElse whileStmt doWhileStmt breakStmt returnStmt
%type <node> exprStmt expr literal
%type <node> argExprList exprList
%type <node> classDef optBase member memberList field fieldList optModifier optTemplate
%type <node> lvalue

%start source

%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%left EQEQ NEQ LT GT LE GE
%left PLUS MINUS
%left STAR SLASH PERCENT
%right UPLUS UMINUS
%left DOT

%%

/* -------- SOURCE -------- */

source
    : sourceItemList
      { $$ = ast_create_node("source"); ast_add_child($$, $1); ast_set_root($$); }
    ;

sourceItemList
    : /* empty */ { $$ = ast_create_node("items"); }
    | sourceItemList sourceItem
      { $$ = $1; ast_add_child($$, $2); }
    ;

sourceItem
    : funcDef
    | classDef
    ;

/* -------- FUNCTIONS -------- */

funcDef
    : funcSignature statementBlock
      { $$ = ast_create_node("funcDef"); ast_add_child($$, $1); ast_add_child($$, $2); }
    | funcSignature SEMICOLON
      { $$ = ast_create_node("funcDecl"); ast_add_child($$, $1); }
    ;

funcSignature
    : typeRef IDENTIFIER LPAREN argList RPAREN
      {
        $$ = ast_create_node("signature");
        ast_add_child($$, $1);
        ast_add_child($$, ast_create_leaf_token("id", $2)); free($2);
        ast_add_child($$, $4);
      }
    ;

argList
    : /* empty */ { $$ = ast_create_node("args"); }
    | argDefList { $$ = ast_create_node("args"); ast_add_child($$, $1); }
    ;

argDefList
    : argDef { $$ = ast_create_node("arglist"); ast_add_child($$, $1); }
    | argDefList COMMA argDef { $$ = $1; ast_add_child($$, $3); }
    ;

argDef
    : typeRef IDENTIFIER
      {
        $$ = ast_create_node("arg");
        ast_add_child($$, $1);
        ast_add_child($$, ast_create_leaf_token("id", $2)); free($2);
      }
    ;

/* -------- TYPES -------- */

typeRef
    : BUILTIN_TYPE { $$ = ast_create_leaf_token("type", $1); free($1); }
    | IDENTIFIER   { $$ = ast_create_leaf_token("typeRef", $1); free($1); }
    | IDENTIFIER LT typeRef GT
      {
        $$ = ast_create_node("genType");
        ast_add_child($$, ast_create_leaf_token("id", $1)); free($1);
        ast_add_child($$, $3);
      }
    | typeRef LBRACKET RBRACKET
      { $$ = ast_create_node("array"); ast_add_child($$, $1); }
    ;

/* -------- STATEMENTS -------- */

statementBlock
    : LBRACE statementList RBRACE
      { $$ = ast_create_node("block"); ast_add_child($$, $2); }
    ;

statementList
    : /* empty */ { $$ = ast_create_node("stmts"); }
    | statementList statement { $$ = $1; ast_add_child($$, $2); }
    ;

statement
    : varDecl
    | ifStmt
    | whileStmt
    | doWhileStmt
    | breakStmt
    | returnStmt
    | exprStmt
    | statementBlock
    ;

/* -------- VARIABLES -------- */

varDecl
    : typeRef varList SEMICOLON
      { $$ = ast_create_node("vardecl"); ast_add_child($$, $1); ast_add_child($$, $2); }
    ;

varList
    : varItemList
    ;

varItemList
    : IDENTIFIER optAssign
      {
        $$ = ast_create_node("vars");
        ast_add_child($$, ast_create_leaf_token("id", $1)); free($1);
        ast_add_child($$, $2);
      }
    | varItemList COMMA IDENTIFIER optAssign
      {
        $$ = $1;
        ast_add_child($$, ast_create_leaf_token("id", $3)); free($3);
        ast_add_child($$, $4);
      }
    ;

optAssign
    : /* empty */ { $$ = ast_create_node("noinit"); }
    | ASSIGN expr { $$ = ast_create_node("assign"); ast_add_child($$, $2); }
    ;

/* -------- CONTROL -------- */

ifStmt
    : IF LPAREN expr RPAREN statement optElse
      { $$ = ast_create_node("if"); ast_add_child($$, $3); ast_add_child($$, $5); ast_add_child($$, $6); }
    ;

optElse
    : /* empty */ { $$ = ast_create_node("noelse"); }
    | ELSE statement { $$ = ast_create_node("else"); ast_add_child($$, $2); }
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
    : BREAK SEMICOLON { $$ = ast_create_node("break"); }
    ;

returnStmt
    : RETURN expr SEMICOLON { $$ = ast_create_node("return"); ast_add_child($$, $2); }
    | RETURN SEMICOLON { $$ = ast_create_node("return"); }
    ;

exprStmt
    : expr SEMICOLON { $$ = ast_create_node("exprstmt"); ast_add_child($$, $1); }
    ;

/* -------- EXPRESSIONS -------- */

lvalue
    : IDENTIFIER
      { $$ = ast_create_leaf_token("id", $1); free($1); }
    | expr DOT IDENTIFIER
      {
        $$ = ast_create_node("fieldAccess");
        ast_add_child($$, $1);
        ast_add_child($$, ast_create_leaf_token("id", $3)); free($3);
      }
    | expr LBRACKET argExprList RBRACKET
      {
        $$ = ast_create_node("index");
        ast_add_child($$, $1);
        ast_add_child($$, $3);
      }
    ;

expr
    : lvalue ASSIGN expr
      { $$ = ast_create_node("assign"); ast_add_child($$, $1); ast_add_child($$, $3); }

    | lvalue PLUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign"); ast_add_child($$, $1);
        ast_add_child($$, ast_create_leaf_token("op", $2)); free($2);
        ast_add_child($$, $3); }

    | expr PLUS expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ast_add_child($$, ast_create_leaf_token("op", $2)); free($2);
        ast_add_child($$, $3); }

    | MINUS expr %prec UMINUS
      { $$ = ast_create_node("unop"); ast_add_child($$, ast_create_leaf_token("op", $1)); free($1); ast_add_child($$, $2); }

    | LPAREN expr RPAREN { $$ = $2; }
    | literal
    | lvalue
    ;

/* -------- LITERALS -------- */

literal
    : BOOL_LITERAL   { $$ = ast_create_leaf_token("bool", $1); free($1); }
    | STRING_LITERAL { $$ = ast_create_leaf_token("string", $1); free($1); }
    | CHAR_LITERAL   { $$ = ast_create_leaf_token("char", $1); free($1); }
    | HEX_LITERAL    { $$ = ast_create_leaf_token("hex", $1); free($1); }
    | BITS_LITERAL   { $$ = ast_create_leaf_token("bits", $1); free($1); }
    | DEC_LITERAL    { $$ = ast_create_leaf_token("dec", $1); free($1); }
    ;

/* -------- CLASSES -------- */

optTemplate
    : /* empty */ { $$ = NULL; }
    | TEMPLATE LT IDENTIFIER GT
      {
        $$ = ast_create_node("template");
        ast_add_child($$, ast_create_leaf_token("id", $3)); free($3);
      }
    ;

classDef
    : optTemplate CLASS IDENTIFIER optBase LBRACE memberList RBRACE
      {
        $$ = ast_create_node("class");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, ast_create_leaf_token("id", $3)); free($3);
        if ($4) ast_add_child($$, $4);
        ast_add_child($$, $6);
      }
    ;

optBase
    : /* empty */ { $$ = NULL; }
    | COLON IDENTIFIER
      {
        $$ = ast_create_node("extends");
        ast_add_child($$, ast_create_leaf_token("id", $2)); free($2);
      }
    ;

memberList
    : /* empty */ { $$ = ast_create_node("members"); }
    | memberList member { $$ = $1; ast_add_child($$, $2); }
    ;

member
    : optModifier funcDef
      { $$ = ast_create_node("member"); if ($1) ast_add_child($$, $1); ast_add_child($$, $2); }
    | optModifier field
      { $$ = ast_create_node("member"); if ($1) ast_add_child($$, $1); ast_add_child($$, $2); }
    ;

optModifier
    : /* empty */ { $$ = NULL; }
    | PUBLIC  { $$ = ast_create_leaf_token("modifier", "public"); }
    | PRIVATE { $$ = ast_create_leaf_token("modifier", "private"); }
    ;

field
    : typeRef fieldList SEMICOLON
      { $$ = ast_create_node("field"); ast_add_child($$, $1); ast_add_child($$, $2); }
    ;

fieldList
    : IDENTIFIER
      {
        $$ = ast_create_node("fieldlist");
        ast_add_child($$, ast_create_leaf_token("id", $1)); free($1);
      }
    | fieldList COMMA IDENTIFIER
      {
        $$ = $1;
        ast_add_child($$, ast_create_leaf_token("id", $3)); free($3);
      }
    ;

%%

int parse_error = 0;

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
    parse_error = 1;
}
