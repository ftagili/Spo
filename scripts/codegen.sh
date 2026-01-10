set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <input_file> <output_file>" >&2
  exit 1
fi

in="$1"
out="$2"

ORIGINAL_DIR="$(pwd)"
if [[ "$in" != /* ]]; then
    in="$(cd "$(dirname "$in")" && pwd)/$(basename "$in")"
fi
if [[ "$out" != /* ]]; then
    out_dir="$(dirname "$out")"
    if [[ "$out_dir" != "." ]]; then
        mkdir -p "$out_dir"
        out="$(cd "$out_dir" && pwd)/$(basename "$out")"
    else
        out="$ORIGINAL_DIR/$(basename "$out")"
    fi
fi

# сборка проекта
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
cd build
cmake ..
cmake --build .
cd ..

# Preprocess the input into a temporary file so we don't modify the original source.
# Transform local-sized array declarations like "TYPE name[EXPR];" into
#   "TYPE[] name; name = __alloc_array(EXPR);"
# This keeps the original test file unchanged (required by instructor) but
# makes the parser and codegen work with array declarations that allocate
# storage at runtime through a helper in runtime.c.
tmp_in="$(mktemp --suffix=.src)"

# Use Perl to do a safe, general regexp-based transform across the file.
# Match a word (type) followed by whitespace, an identifier, optional spaces,
# a bracketed expression, then a semicolon. Replace with array type + alloc.
perl -0777 -pe '
    s/\b([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*([^\]]+?)\s*\]\s*;/\1[] \2;\n\2 = __alloc_array(\3);/gs;
' "$in" > "$tmp_in"

./build/codegen "$tmp_in" "$out"

# remove temp file
rm -f "$tmp_in"

# ассемблирование
gcc -c "$out" -o output/out.o -Wa,--noexecstack

# compile small runtime helpers and link them in so generated code
# that calls printInt/writeByte or placeholder virtual methods will link.
gcc -c "${SCRIPT_DIR}/runtime.c" -o output/runtime.o

gcc -no-pie output/out.o output/runtime.o -o output/a.out
./output/a.out
