%{
#include "../ast/ast.h"
#include "../../build/parser.tab.h"
#include <string.h>
#include <stdlib.h>

static char* dup_yytext(const char* s) {
  size_t n = strlen(s) + 1;
  char* d = (char*)malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

static void print_escaped(const char *s) {
  fputs("Unknown symbol: '", stderr);
  for (const unsigned char *p = (const unsigned char*)s; *p; ++p) {
    unsigned char c = *p;
    switch (c) {
      case '\\': fputs("\\\\", stderr); break;
      case '\n': fputs("\\n", stderr); break;
      case '\r': fputs("\\r", stderr); break;
      case '\t': fputs("\\t", stderr); break;
      default:
        if (c >= 32 && c < 127) fputc(c, stderr);
        else fprintf(stderr, "\\x%02x", c);
    }
  }
  fputs("'\\n", stderr);
}
%}

%option noyywrap
%option nounput
%option noinput
%option yylineno

%%

"if"            { return IF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"do"            { return DO; }
"break"         { return BREAK; }
"return"        { return RETURN; }
"extern"        { return EXTERN; }
"class"         { return CLASS; }
"public"        { return PUBLIC; }
"private"       { return PRIVATE; }
"new"           { return NEW; }
"template"      { return TEMPLATE; }

"bool"|"byte"|"int"|"uint"|"long"|"ulong"|"char"|"string"|"void"  { yylval.str = dup_yytext(yytext); return BUILTIN_TYPE; }

"true"|"false"  { yylval.str = dup_yytext(yytext); return BOOL_LITERAL; }
0[xX][0-9A-Fa-f]+       { yylval.str = dup_yytext(yytext); return HEX_LITERAL; }
0[bB][01]+              { yylval.str = dup_yytext(yytext); return BITS_LITERAL; }
[0-9]+                  { yylval.str = dup_yytext(yytext); return DEC_LITERAL; }
\"([^\\\n]|\\.)*\"  { yylval.str = dup_yytext(yytext); return STRING_LITERAL; }
\'([^\\\n]|\\.)\'                  { yylval.str = dup_yytext(yytext); return CHAR_LITERAL; }

[A-Za-z_][A-Za-z0-9_]*[ \t]*\[\] {
  /* Match patterns like `T[]` (optionally with spaces between identifier and []).
     Return IDENT_ARRAY with the identifier name only to help the parser
     distinguish array type declarations from indexing expressions. */
  char *br = strchr(yytext, '[');
  size_t idlen = br ? (size_t)(br - yytext) : yyleng;
  char *id = (char*)malloc(idlen + 1);
  if (id) {
    memcpy(id, yytext, idlen);
    id[idlen] = '\0';
  }
  yylval.str = id;
  return IDENT_ARRAY;
}

/* Regular identifier rule (after IDENT_ARRAY) */
[A-Za-z_][A-Za-z0-9_]*  { yylval.str = dup_yytext(yytext); return IDENTIFIER; }

"=="            { yylval.str = dup_yytext(yytext); return EQEQ; }
"!="            { yylval.str = dup_yytext(yytext); return NEQ; }
"<="            { yylval.str = dup_yytext(yytext); return LE; }
">="            { yylval.str = dup_yytext(yytext); return GE; }
"<"             { yylval.str = dup_yytext(yytext); return LT; }
">"             { yylval.str = dup_yytext(yytext); return GT; }

"+="            { yylval.str = dup_yytext(yytext); return PLUS_ASSIGN; }
"-="            { yylval.str = dup_yytext(yytext); return MINUS_ASSIGN; }
"*="            { yylval.str = dup_yytext(yytext); return STAR_ASSIGN; }
"/="            { yylval.str = dup_yytext(yytext); return SLASH_ASSIGN; }
"%="            { yylval.str = dup_yytext(yytext); return PERCENT_ASSIGN; }

"+"             { yylval.str = dup_yytext(yytext); return PLUS; }
"-"             { yylval.str = dup_yytext(yytext); return MINUS; }
"*"             { yylval.str = dup_yytext(yytext); return STAR; }
"/"             { yylval.str = dup_yytext(yytext); return SLASH; }
"%"             { yylval.str = dup_yytext(yytext); return PERCENT; }

"="             { return ASSIGN; }
"&"             { return AMPERSAND; }

"{"     { return LBRACE; }
"}"     { return RBRACE; }
"("     { return LPAREN; }
")"     { return RPAREN; }
";"     { return SEMICOLON; }
","     { return COMMA; }
"["     { return LBRACKET; }
"]"     { return RBRACKET; }
"..."   { yylval.str = dup_yytext(yytext); return ELLIPSIS; }
"."     { return DOT; }
":"     { return COLON; }

"/*"([^*]|\*+[^*/])*\*+"/"  { /* multi-line comment */ }
"//"[^\n]*                 { /* single-line comment */ }

[ \t\r\n]+     { /* skip whitespace */ }
.                { print_escaped(yytext); }

%%
