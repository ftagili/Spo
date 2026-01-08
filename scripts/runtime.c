#include <stdio.h>
#include <stdint.h>

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
