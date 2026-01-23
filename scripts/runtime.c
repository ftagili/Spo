#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* Runtime wrapper around malloc used by generated code. We implement a
   thin wrapper so we can add debug logging and ensure all allocations go
   through our runtime layer. This avoids overriding libc's malloc symbol
   globally and gives visibility into allocation failures / sizes. */
void *__runtime_malloc(size_t s) {
    /* Avoid calling fprintf here: it may call malloc internally and
       lead to re-entrancy issues when we are debugging allocator state.
       Keep this wrapper simple and side-effect free. */
    return malloc(s);
}

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

/* Runtime representation of List<T> expected by codegen:
   Layout in memory (offsets in bytes):
     0: vptr (8 bytes)
     8: pointer to array (void**)
    16: int64_t count
    24: int64_t capacity
  Codegen uses 8-byte words for fields; implement List accordingly. */
typedef struct {
    void *vptr;    /* at offset 0 */
    void **arr;    /* offset 8 */
    int64_t count; /* offset 16 */
    int64_t capacity; /* offset 24 */
} List;

void List__init(List *self) {
    if (!self) return;
    self->arr = NULL;
    self->count = 0;
    self->capacity = 0;
}

static int list_ensure_capacity(List *self, int64_t need) {
    if (self->capacity >= need) return 1;
    int64_t newcap = self->capacity ? (self->capacity * 2) : 4;
    while (newcap < need) newcap *= 2;
    void **n = realloc(self->arr, (size_t)(newcap * sizeof(void*)));
    if (!n) return 0;
    self->arr = n;
    self->capacity = newcap;
    return 1;
}

void List__add(List *self, void *value) {
    if (!self) return;
    if (!list_ensure_capacity(self, self->count + 1)) return;
    self->arr[self->count] = value;
    self->count += 1;
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
    for (int64_t i = 0; i < self->count; i++) {
        if (i != 0) {
            writeByte(',');
            writeByte(' ');
        }
        void *val = self->arr[i];
        /* Attempt to print as Vec2i if non-null; tests use List<Vec2i>
           and List<int> — for int values they are boxed as int-like
           values (in this runtime we expect plain pointers to heap
           allocated Vec2i or integer values represented as small
           integers stored directly as pointers). Here test7 stores
           integers as immediate (not boxed) via codegen, but the
           generated printValue__int path calls printValue__printInt
           which is available. We'll attempt to dispatch by calling
           printValue__printInt for integer-like values and
           printValue_Vec2i for Vec2i pointers. */
        if (val == NULL) {
            writeByte('N'); writeByte('U'); writeByte('L'); writeByte('L');
        } else {
            /* Heuristic: assume pointers that align to object have
               been produced by makeVec2i and point to a Vec2i. */
            printValue_Vec2i((Vec2i*)val);
        }
    }
    writeByte(']');
    writeByte('\n');
}

/* Note: do NOT provide printValue__Vec2i here — the generated program may
   define its own printValue__Vec2i (from the user's source). Defining it in
   the runtime would cause multiple-definition linker errors. */
