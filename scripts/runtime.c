#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* =========================
   Базовый ввод / вывод
   ========================= */

void writeByte(int c) {
    putchar(c);
}

void printInt(int64_t n) {
    printf("%ld", n);
}

/* =========================
   Vec2i
   ========================= */

typedef struct {
    int64_t x;
    int64_t y;
} Vec2i;

void Vec2i__init(Vec2i *self, int64_t x, int64_t y) {
    self->x = x;
    self->y = y;
}

Vec2i *makeVec2i(int64_t x, int64_t y) {
    Vec2i *v = malloc(sizeof(Vec2i));
    Vec2i__init(v, x, y);
    return v;
}

/* Используется в sum(List<Vec2i>) */
Vec2i *sum__makeVec2i(int64_t x, int64_t y) {
    return makeVec2i(x, y);
}

/* =========================
   List (односвязный список)
   ========================= */

typedef struct ListNode {
    void *value;
    struct ListNode *next;
} ListNode;

typedef struct {
    ListNode *head;
    ListNode *tail;
} List;

void List__init(List *self) {
    self->head = NULL;
    self->tail = NULL;
}

void List__add(List *self, void *value) {
    ListNode *n = malloc(sizeof(ListNode));
    n->value = value;
    n->next = NULL;

    if (self->tail) {
        self->tail->next = n;
        self->tail = n;
    } else {
        self->head = self->tail = n;
    }
}

/* =========================
   printValue
   ========================= */

/* int */

void printValue_int__printInt(int64_t n) {
    printInt(n);
}

void printValue_int__writeByte(int c) {
    writeByte(c);
}

/* Vec2i */

void printValue_Vec2i__printInt(int64_t n) {
    printInt(n);
}

void printValue_Vec2i__writeByte(int c) {
    writeByte(c);
}

void printValue_Vec2i(Vec2i *v) {
    writeByte('(');
    printInt(v->x);
    writeByte(',');
    writeByte(' ');
    printInt(v->y);
    writeByte(')');
}

/* =========================
   List печать
   ========================= */

void List__printValues(List *self) {
    writeByte('[');

    ListNode *cur = self->head;
    int first = 1;

    while (cur) {
        if (!first) {
            writeByte(',');
            writeByte(' ');
        }
        first = 0;

        Vec2i *v = (Vec2i *)cur->value;
        printValue_Vec2i(v);

        cur = cur->next;
    }

    writeByte(']');
    writeByte('\n');
}
