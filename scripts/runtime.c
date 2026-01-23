#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* Placeholder vtable symbols for classes that may be referenced by
    generated code but not defined as classes in the source AST. These
    ensure the linker finds symbols like List_vtable or Vec2i_vtable and
    that object vptrs initialized to these symbols are non-NULL. */
void *List_vtable = NULL;
void *Vec2i_vtable = NULL;

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
   Совместимость с test6
   ========================= */

/* test6 ожидает print_int */
void print_int(int64_t n) {
    printInt(n);
}

/* test6 ожидает print_int__print_int */
void print_int__print_int(int64_t n) {
    printInt(n);
}

/* =========================
   Vec2i
   ========================= */

typedef struct {
    int64_t x;
    int64_t y;
} Vec2i;

/*
 * codegen может вызывать init с NULL self
 */
void Vec2i__init(Vec2i *self, int64_t x, int64_t y) {
    if (!self) return;
    self->x = x;
    self->y = y;
}

Vec2i *makeVec2i(int64_t x, int64_t y) {
    Vec2i *v = malloc(sizeof(Vec2i));
    if (!v) return NULL;
    Vec2i__init(v, x, y);
    return v;
}

/* используется в sum(List<Vec2i>) */
Vec2i *sum__makeVec2i(int64_t x, int64_t y) {
    return makeVec2i(x, y);
}

/* =========================
   List
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
    if (!self) return;
    self->head = NULL;
    self->tail = NULL;
}

void List__add(List *self, void *value) {
    if (!self) return;

    ListNode *n = malloc(sizeof(ListNode));
    if (!n) return;

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

/*
 * Backwards-compatible generic wrappers.
 * Some generated code may refer to non-suffixed names like
 * `printValue__printInt` / `printValue__writeByte`. Provide
 * thin wrappers that forward to the concrete implementations
 * already present above.
 */
void printValue__printInt(int64_t n) {
    printValue_int__printInt(n);
}

void printValue__writeByte(int c) {
    printValue_int__writeByte(c);
}

/* Vec2i */

void printValue_Vec2i__printInt(int64_t n) {
    printInt(n);
}

void printValue_Vec2i__writeByte(int c) {
    writeByte(c);
}

void printValue_Vec2i(Vec2i *v) {
    if (!v) return;

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
    if (!self) {
        writeByte('\n');
        return;
    }

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
        if (v) {
            printValue_Vec2i(v);
        }

        cur = cur->next;
    }

    writeByte(']');
    writeByte('\n');
}
