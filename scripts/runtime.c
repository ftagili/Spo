#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// Small runtime helpers used by generated code.

// printInt: print a 64-bit integer in decimal
void printInt(int64_t n) {
    // Use printf directly; keep output buffered by stdout.
    printf("%lld", (long long)n);
    fflush(stdout);
}

// writeByte: write a single character
void writeByte(int c) {
    putchar((int)c);
    fflush(stdout);
}

// Placeholder for virtual dispatch filler. Generated code may call
// unknown_method when virtual dispatch isn't implemented yet.
void unknown_method(void) {
    // no-op placeholder
}

// Allocate a simple array of elements where each element is 8 bytes
// (the codegen currently assumes 8-byte element size). Return pointer.
void *__alloc_array(long count) {
    if (count <= 0) return NULL;
    size_t sz = (size_t)count * 8u;
    void *p = malloc(sz);
    return p;
}
