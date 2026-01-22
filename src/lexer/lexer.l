%{
#include "../ast/ast.h"
#include "../../build/parser.tab.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
        if (c >= 32 && c < 127) {
          fputc(c, stderr);
        } else {
          fprintf(stderr, "\\x%02x", c);
        }
    }
  }
  fputs("'\n", stderr);
}
%}

%option noyywrap
%option nounput
%option noinput

%%
/* =========================
   Комментарии (САМЫЕ ПЕРВЫЕ!)
   ========================= */

"/*"([^*]|\*+[^*/])*\*+"/"  { /* skip multi-line comment */ }
"//"[^\n]*                  { /* skip single-line comment */ }

/* =========================
   Ключевые слова
   ========================= */

"if"            return IF;
"else"          return ELSE;
"while"         return WHILE;
"do"            return DO;
"break"         return BREAK;
"return"        return RETURN;
"extern"        return EXTERN;
"class"         return CLASS;
"public"        return PUBLIC;
"private"       return PRIVATE;
"new"           return NEW;
"template"      return TEMPLATE;

/* =========================
   Встроенные типы
   ========================= */

"bool"|"byte"|"int"|"uint"|"long"|"ulong"|"char"|"string"|"void" {
    yylval.str = dup_yytext(yytext);
    return BUILTIN_TYPE;
}

/* =========================
   Литералы
   ========================= */

"true"|"false" {
    yylval.str = dup_yytext(yytext);
    return BOOL_LITERAL;
}

0[xX][0-9A-Fa-f]+ {
    yylval.str = dup_yytext(yytext);
    return HEX_LITERAL;
}

0[bB][01]+ {
    yylval.str = dup_yytext(yytext);
    return BITS_LITERAL;
}

[0-9]+ {
    yylval.str = dup_yytext(yytext);
    return DEC_LITERAL;
}

\"([^\\"]|\\.)*\" {
    yylval.str = dup_yytext(yytext);
    return STRING_LITERAL;
}

/* === CHAR literal -> INT === */
'\\n'  { yylval.int_val = '\n'; return CHAR_LITERAL; }
'\\t'  { yylval.int_val = '\t'; return CHAR_LITERAL; }
'\\r'  { yylval.int_val = '\r'; return CHAR_LITERAL; }
'\\\\' { yylval.int_val = '\\'; return CHAR_LITERAL; }
'[^\\']' {
    yylval.int_val = yytext[1];
    return CHAR_LITERAL;
}

/* =========================
   Идентификаторы
   ========================= */

[a-zA-Z_][a-zA-Z_0-9]* {
    yylval.str = dup_yytext(yytext);
    return IDENTIFIER;
}

/* =========================
   Операторы
   ========================= */

"==" { return EQEQ; }
"!=" { return NEQ; }
"<=" { return LE; }
">=" { return GE; }
"<"  { return LT; }
">"  { return GT; }

"+=" { return PLUS_ASSIGN; }
"-=" { return MINUS_ASSIGN; }
"*=" { return STAR_ASSIGN; }
"/=" { return SLASH_ASSIGN; }
"%=" { return PERCENT_ASSIGN; }

"+"  { return PLUS; }
"-"  { return MINUS; }
"*"  { return STAR; }
"/"  { return SLASH; }
"%"  { return PERCENT; }

"="  { return ASSIGN; }
"&"  { return AMPERSAND; }

/* =========================
   Разделители
   ========================= */

"{" { return LBRACE; }
"}" { return RBRACE; }
"(" { return LPAREN; }
")" { return RPAREN; }
";" { return SEMICOLON; }
"," { return COMMA; }
"[" { return LBRACKET; }
"]" { return RBRACKET; }
"." { return DOT; }
":" { return COLON; }

/* =========================
   Пробелы
   ========================= */

[[:space:]]+ { }

/* =========================
   Ошибки
   ========================= */

. { print_escaped(yytext); }

%%
