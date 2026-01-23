#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "../ast/ast.h"

// ------------------------- small utils -------------------------

static char *dup_cstr(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

static const char *after_colon(const char *label) {
  if (!label) return "";
  const char *c = strchr(label, ':');
  return c ? (c + 1) : label;
}

static int starts_with(const char *s, const char *pfx) {
  if (!s || !pfx) return 0;
  size_t n = strlen(pfx);
  return strncmp(s, pfx, n) == 0;
}

// forward decl (is_token_kind is defined later)
static int is_token_kind(const ASTNode *n, const char *kind);
// forward decl for helper that classifies stdlib functions
static int is_standard_library_func(const char *name);

// Extract a simple type name from a typeRef/type/genType AST node.
// Returns pointer into the AST node's label (do not free).
static const char *get_type_name(const ASTNode *type_node) {
  if (!type_node) return NULL;
  if (is_token_kind(type_node, "type") || is_token_kind(type_node, "typeRef")) {
    return after_colon(type_node->label);
  }
  if (type_node->label && strcmp(type_node->label, "genType") == 0 && type_node->numChildren > 0) {
    const ASTNode *idn = type_node->children[0];
    if (idn && is_token_kind(idn, "id")) return after_colon(idn->label);
  }
  return NULL;
}

static int is_token_kind(const ASTNode *n, const char *kind) {
  if (!n || !n->label || !kind) return 0;
  size_t k = strlen(kind);
  return strncmp(n->label, kind, k) == 0 && n->label[k] == ':';
}

static int align16(int x) { return (x + 15) & ~15; }

// ------------------------- dynamic arrays -------------------------

typedef struct {
  char *name;
  int offset; // от %r11 (frame base)
  char *type; // optional static type name for local (e.g. "ListInt")
} Local;

typedef struct {
  Local *v;
  int n, cap;
} LocalMap;

static void locals_init(LocalMap *m) { memset(m, 0, sizeof(*m)); }

static void locals_free(LocalMap *m) {
  if (!m) return;
  for (int i = 0; i < m->n; i++) free(m->v[i].name);
  for (int i = 0; i < m->n; i++) free(m->v[i].type);
  free(m->v);
  memset(m, 0, sizeof(*m));
}

static int locals_find(const LocalMap *m, const char *name) {
  if (!m || !name) return -1;
  for (int i = 0; i < m->n; i++) {
    if (m->v[i].name && strcmp(m->v[i].name, name) == 0) return i;
  }
  return -1;
}

static int locals_add(LocalMap *m, const char *name, int offset, const char *type) {
  if (!m || !name) return -1;
  if (locals_find(m, name) >= 0) return 0; // already exists

  if (m->n == m->cap) {
    int nc = m->cap ? (m->cap * 2) : 16;
    Local *nv = (Local *)realloc(m->v, (size_t)nc * sizeof(Local));
    if (!nv) return -1;
    m->v = nv;
    m->cap = nc;
  }
  m->v[m->n].name = dup_cstr(name);
  m->v[m->n].type = type ? dup_cstr(type) : NULL;
  m->v[m->n].offset = offset;
  m->n++;
  return 1;
}

static int locals_get_offset(const LocalMap *m, const char *name, int *out_off) {
  int idx = locals_find(m, name);
  if (idx < 0) return 0;
  if (out_off) *out_off = m->v[idx].offset;
  return 1;
}

static const char *locals_get_type(const LocalMap *m, const char *name) {
  int idx = locals_find(m, name);
  if (idx < 0) return NULL;
  return m->v[idx].type;
}

// ------------------------- string/const pools -------------------------

typedef struct {
  char *text;   // как в исходнике, включая кавычки: "Hello\n"
  int label_id; // .LC<label_id>
} StrLit;

typedef struct {
  StrLit *v;
  int n, cap;
} StrPool;

static void strpool_init(StrPool *p) { memset(p, 0, sizeof(*p)); }

static void strpool_free(StrPool *p) {
  if (!p) return;
  for (int i = 0; i < p->n; i++) free(p->v[i].text);
  free(p->v);
  memset(p, 0, sizeof(*p));
}

static int strpool_find(const StrPool *p, const char *text) {
  if (!p || !text) return -1;
  for (int i = 0; i < p->n; i++) {
    if (p->v[i].text && strcmp(p->v[i].text, text) == 0) return i;
  }
  return -1;
}

static int strpool_add(StrPool *p, const char *text, int label_id) {
  if (!p || !text) return -1;
  int idx = strpool_find(p, text);
  if (idx >= 0) return p->v[idx].label_id;

  if (p->n == p->cap) {
    int nc = p->cap ? (p->cap * 2) : 16;
    StrLit *nv = (StrLit *)realloc(p->v, (size_t)nc * sizeof(StrLit));
    if (!nv) return -1;
    p->v = nv;
    p->cap = nc;
  }
  p->v[p->n].text = dup_cstr(text);
  p->v[p->n].label_id = label_id;
  p->n++;
  return label_id;
}

typedef struct {
  int64_t value;
  int label_id; // .LCQ<label_id>
} Const64;

typedef struct {
  Const64 *v;
  int n, cap;
} ConstPool;

static void cpool_init(ConstPool *p) { memset(p, 0, sizeof(*p)); }

static void cpool_free(ConstPool *p) {
  free(p->v);
  memset(p, 0, sizeof(*p));
}

static int cpool_find(const ConstPool *p, int64_t val) {
  if (!p) return -1;
  for (int i = 0; i < p->n; i++) {
    if (p->v[i].value == val) return i;
  }
  return -1;
}

static int cpool_add(ConstPool *p, int64_t val, int label_id) {
  int idx = cpool_find(p, val);
  if (idx >= 0) return p->v[idx].label_id;

  if (p->n == p->cap) {
    int nc = p->cap ? (p->cap * 2) : 16;
    Const64 *nv = (Const64 *)realloc(p->v, (size_t)nc * sizeof(Const64));
    if (!nv) return -1;
    p->v = nv;
    p->cap = nc;
  }
  p->v[p->n].value = val;
  p->v[p->n].label_id = label_id;
  p->n++;
  return label_id;
}

// ------------------------- codegen context -------------------------

typedef struct {
  FILE *out;

  StrPool str_pool;
  ConstPool const_pool;

  int next_label;     // для локальных .Lxxx
  int next_str_label; // для .LCxxx
  int next_c64_label; // для .LCQxxx

  // function-local:
  const char *cur_func;
  int epilogue_label;
  LocalMap locals;

  int frame_size;     // размер кадра
  int scratch_size;   // сколько отдали под push/pop temp
  int locals_size;    // сколько заняли локалы

  // break targets stack:
  int *break_labels;
  int break_n, break_cap;

  /* top-level defined function names collected before generation */
  const char **defined_names;
  int defined_n;
  /* parallel array holding parameter counts (arity) for each defined name */
  int *defined_arity;
  /* simple field offset map: parallel arrays */
  const char **field_class_names; /* class name per field entry */
  const char **field_names;       /* field name */
  int *field_offsets;            /* offset in bytes */
  int field_n;
  /* set of class names for which we need vtable symbols (collected during gen_new)
    to ensure we emit a placeholder vtable even if class definition is absent. */
  const char **required_vtables;
  int req_vtables_n, req_vtables_cap;

} CG;

static void cg_init(CG *cg, FILE *out) {
  memset(cg, 0, sizeof(*cg));
  cg->out = out;
  strpool_init(&cg->str_pool);
  cpool_init(&cg->const_pool);
  cg->next_label = 1;
  cg->next_str_label = 0;
  cg->next_c64_label = 0;
  locals_init(&cg->locals);
  cg->defined_names = NULL;
  cg->defined_n = 0;
  cg->defined_arity = NULL;
  cg->field_class_names = NULL;
  cg->field_names = NULL;
  cg->field_offsets = NULL;
  cg->field_n = 0;
  cg->required_vtables = NULL;
  cg->req_vtables_n = 0;
  cg->req_vtables_cap = 0;
}

static void cg_free(CG *cg) {
  if (!cg) return;
  strpool_free(&cg->str_pool);
  cpool_free(&cg->const_pool);
  locals_free(&cg->locals);
  free(cg->break_labels);
  if (cg->defined_names) {
    for (int i = 0; i < cg->defined_n; i++) free((void*)cg->defined_names[i]);
    free((void*)cg->defined_names);
  }
  if (cg->defined_arity) free(cg->defined_arity);
  free((void*)cg->field_class_names);
  free((void*)cg->field_names);
  free(cg->field_offsets);
  if (cg->required_vtables) {
    for (int i = 0; i < cg->req_vtables_n; i++) free((void*)cg->required_vtables[i]);
    free((void*)cg->required_vtables);
  }
  memset(cg, 0, sizeof(*cg));
}

static void cg_add_required_vtable(CG *cg, const char *name) {
  if (!cg || !name) return;
  for (int i = 0; i < cg->req_vtables_n; i++) {
    if (cg->required_vtables[i] && !strcmp(cg->required_vtables[i], name)) return;
  }
  if (cg->req_vtables_n == cg->req_vtables_cap) {
    int nc = cg->req_vtables_cap ? (cg->req_vtables_cap * 2) : 8;
    const char **nv = (const char **)realloc((void*)cg->required_vtables, (size_t)nc * sizeof(char*));
    if (!nv) return;
    cg->required_vtables = nv;
    cg->req_vtables_cap = nc;
  }
  cg->required_vtables[cg->req_vtables_n++] = dup_cstr(name);
}

static int cg_has_defined_function(CG *cg, const char *name) {
  if (!cg || !name) return 0;
  for (int i = 0; i < cg->defined_n; i++) {
    const char *n = cg->defined_names[i];
    if (n && strcmp(n, name) == 0) return 1;
  }
  return 0;
}

static void emit(CG *cg, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(cg->out, fmt, ap);
  va_end(ap);
  fputc('\n', cg->out);
}

static int new_label(CG *cg) { return cg->next_label++; }

static void emit_label(CG *cg, int id) {
  emit(cg, ".L%d:", id);
}

static void break_push(CG *cg, int lbl) {
  if (cg->break_n == cg->break_cap) {
    int nc = cg->break_cap ? cg->break_cap * 2 : 8;
    int *nv = (int *)realloc(cg->break_labels, (size_t)nc * sizeof(int));
    if (!nv) return;
    cg->break_labels = nv;
    cg->break_cap = nc;
  }
  cg->break_labels[cg->break_n++] = lbl;
}

static int break_top(CG *cg) {
  if (!cg || cg->break_n <= 0) return -1;
  return cg->break_labels[cg->break_n - 1];
}

static void break_pop(CG *cg) {
  if (!cg || cg->break_n <= 0) return;
  cg->break_n--;
}

// ------------------------- literal parsing -------------------------

static int64_t parse_bits_literal(const char *s) {
  // s like: 0b10101 or 0B...
  if (!s) return 0;
  if (!(s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))) return 0;
  int64_t v = 0;
  for (const char *p = s + 2; *p; p++) {
    if (*p == '0' || *p == '1') {
      v = (v << 1) | (*p - '0');
    } else {
      break;
    }
  }
  return v;
}

static int64_t parse_int_literal_label(const ASTNode *n) {
  if (!n || !n->label) return 0;
  const char *v = after_colon(n->label);

  if (is_token_kind(n, "dec") || is_token_kind(n, "hex")) {
    // strtoll base 0 handles 123 and 0x...
    return (int64_t)strtoll(v, NULL, 0);
  }
  if (is_token_kind(n, "bits")) {
    return parse_bits_literal(v);
  }
  if (is_token_kind(n, "bool")) {
    return (strcmp(v, "true") == 0) ? 1 : 0;
  }
  if (is_token_kind(n, "char")) {
    // lexer: 'x' (без escape)
    size_t len = strlen(v);
    if (len >= 3 && v[0] == '\'' && v[len - 1] == '\'') {
      return (unsigned char)v[1];
    }
    return 0;
  }
  return 0;
}

// ------------------------- forward decls -------------------------

static void gen_stmt(CG *cg, const ASTNode *stmt);
static void gen_expr(CG *cg, const ASTNode *expr);
static void gen_cond_branch(CG *cg, const ASTNode *cond, int false_label);
static void gen_assign_index(CG *cg, const ASTNode *expr);
static int cg_has_defined_function(CG *cg, const char *name);

// ------------------------- pools collection (optional but handy) -------------------------

static void collect_literals(CG *cg, const ASTNode *n) {
  if (!n) return;

  if (is_token_kind(n, "string")) {
    const char *txt = after_colon(n->label); // includes quotes
    int id = strpool_add(&cg->str_pool, txt, cg->next_str_label++);
    (void)id;
  }

  // constants >32-bit: add later lazily (cpool_add on demand)

  for (int i = 0; i < n->numChildren; i++) {
    collect_literals(cg, n->children[i]);
  }
}

// ------------------------- locals collection -------------------------

static void collect_locals_from_block(CG *cg, const ASTNode *node, int *next_off) {
  if (!node) return;

  if (node->label && strcmp(node->label, "vardecl") == 0) {
    // vardecl: [0]=typeRef, [1]=vars
    if (node->numChildren >= 2) {
      const ASTNode *type_node = node->children[0];
      const char *type_name = get_type_name(type_node);
      const ASTNode *vars = node->children[1];
      if (vars && vars->label && strcmp(vars->label, "vars") == 0) {
        // children: id, optAssign, id, optAssign...
        for (int i = 0; i + 1 < vars->numChildren; i += 2) {
          const ASTNode *idn = vars->children[i];
          if (idn && is_token_kind(idn, "id")) {
            const char *name = after_colon(idn->label);
            int off = *next_off;
            if (locals_add(&cg->locals, name, off, type_name) > 0) {
              *next_off += 8;
            }
          }
        }
      }
    }
  }

  // рекурсивно по детям
  for (int i = 0; i < node->numChildren; i++) {
    collect_locals_from_block(cg, node->children[i], next_off);
  }
}

static void collect_params_as_locals(CG *cg, const ASTNode *signature, int *next_off) {
  // signature: [0]=typeRef, [1]=id, [2]=args
  if (!signature || !signature->label || strcmp(signature->label, "signature") != 0) return;
  if (signature->numChildren < 3) return;

  const ASTNode *args = signature->children[2];
  if (!args || !args->label || strcmp(args->label, "args") != 0) return;
  if (args->numChildren == 0) return;

  const ASTNode *arglist = args->children[0];
  if (!arglist || !arglist->label || strcmp(arglist->label, "arglist") != 0) return;

  for (int i = 0; i < arglist->numChildren; i++) {
    const ASTNode *arg = arglist->children[i]; // "arg"
    if (!arg || !arg->label || strcmp(arg->label, "arg") != 0) continue;
    if (arg->numChildren < 2) continue;
    const ASTNode *idn = arg->children[1];
    if (idn && is_token_kind(idn, "id")) {
      const char *name = after_colon(idn->label);
      // try extract type name from arg->children[0]
      const ASTNode *type_node = arg->children[0];
      const char *type_name = get_type_name(type_node);
      int off = *next_off;
      if (locals_add(&cg->locals, name, off, type_name) > 0) {
        *next_off += 8;
      }
    }
  }
}

// ------------------------- low-level helpers (stack temp) -------------------------

// Мы используем %r12 как temp stack pointer внутри кадра.
// push: aghi %r12,-8 ; stg %r2,0(%r12)
// pop -> %r3: lg %r3,0(%r12); aghi %r12,8

static void emit_push_r2(CG *cg) {
  emit(cg, "  aghi %%r12,-8");
  emit(cg, "  stg  %%r2,0(%%r12)");
}

static void emit_pop_to_r3(CG *cg) {
  emit(cg, "  lg   %%r3,0(%%r12)");
  emit(cg, "  aghi %%r12,8");
}

// load 64-bit immediate into %r2 (uses const pool if needed)
static void emit_load_imm64(CG *cg, int64_t v) {
  if (v >= -32768 && v <= 32767) {
    emit(cg, "  lghi %%r2,%lld", (long long)v);
    return;
  }
  if (v >= INT32_MIN && v <= INT32_MAX) {
    emit(cg, "  lgfi %%r2,%lld", (long long)v);
    return;
  }
  int lid = cpool_add(&cg->const_pool, v, cg->next_c64_label++);
  emit(cg, "  larl %%r1,.LCQ%d", lid);
  emit(cg, "  lg   %%r2,0(%%r1)");
}

// load address of string literal into %r2
static void emit_load_string(CG *cg, const char *txt) {
  int idx = strpool_find(&cg->str_pool, txt);
  int lid;
  if (idx >= 0) lid = cg->str_pool.v[idx].label_id;
  else lid = strpool_add(&cg->str_pool, txt, cg->next_str_label++);
  emit(cg, "  larl %%r2,.LC%d", lid);
}

// local var: load -> %r2
static void emit_load_local(CG *cg, const char *name) {
  int off = 0;
  if (!locals_get_offset(&cg->locals, name, &off)) {
    // unknown local — debug-friendly fallback
    emit(cg, "  lghi %%r2,0");
    return;
  }
  emit(cg, "  lg   %%r2,%d(%%r11)", off);
}

// local var: store from %r2
static void emit_store_local(CG *cg, const char *name) {
  int off = 0;
  if (!locals_get_offset(&cg->locals, name, &off)) {
    return;
  }
  emit(cg, "  stg  %%r2,%d(%%r11)", off);
}

// ------------------------- expression generation -------------------------

static int is_cmp_op(const char *op) {
  return op &&
    (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=") ||
     !strcmp(op, "==") || !strcmp(op, "!="));
}

static void gen_call(CG *cg, const ASTNode *call) {
  // call: children[0]=id, children[1]=args
  const ASTNode *idn = (call->numChildren > 0) ? call->children[0] : NULL;
  const char *fname = (idn && is_token_kind(idn, "id")) ? after_colon(idn->label) : NULL;

  const ASTNode *args = (call->numChildren > 1) ? call->children[1] : NULL;
  const ASTNode *list = NULL;
  int nargs = 0;

  if (args && args->label && strcmp(args->label, "args") == 0 && args->numChildren > 0) {
    list = args->children[0];
    if (list && list->label && strcmp(list->label, "list") == 0) {
      nargs = list->numChildren;
    }
  }

  // Evaluate args left-to-right, push each result.
  for (int i = 0; i < nargs; i++) {
    gen_expr(cg, list->children[i]);
    emit_push_r2(cg);
  }

  if (nargs > 5) {
    emit(cg, "  # ERROR: >5 args not supported yet, extra args ignored");
    // Pop anyway to keep stack balanced:
    for (int i = 0; i < nargs; i++) emit_pop_to_r3(cg);
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // Pop обратно в r2..r(2+nargs-1) в обратном порядке
  for (int i = nargs - 1; i >= 0; i--) {
    int reg = 2 + i; // r2..r6
    emit(cg, "  lg   %%r%d,0(%%r12)", reg);
    emit(cg, "  aghi %%r12,8");
  }

  if (!fname || !*fname) {
    emit(cg, "  # ERROR: call without function name");
    emit(cg, "  lghi %%r2,0");
    return;
  }
  // If the name refers to an actually defined top-level function, call it
  // directly (don't mangle). Otherwise, if we're inside a mangled method
  // (Class__method) and the call is unqualified, treat it as a method of
  // the same class and mangle to Class__name. Standard library functions
  // are also left unmangled.
  if (cg_has_defined_function(cg, fname) || is_standard_library_func(fname)) {
    emit(cg, "  brasl %%r14,%s", fname);
  } else if (cg->cur_func && strstr(cg->cur_func, "__") && strstr(fname, "__") == NULL) {
    const char *p = strstr(cg->cur_func, "__");
    if (p) {
      size_t cls_len = (size_t)(p - cg->cur_func);
      char mangled[256];
      if (cls_len + 2 + strlen(fname) < sizeof(mangled)) {
        memcpy(mangled, cg->cur_func, cls_len);
        mangled[cls_len] = '\0';
        strcat(mangled, "__");
        strcat(mangled, fname);
        emit(cg, "  brasl %%r14,%s", mangled);
      } else {
        emit(cg, "  brasl %%r14,%s", fname);
      }
    } else {
      emit(cg, "  brasl %%r14,%s", fname);
    }
  } else {
    /* If there is no exact top-level function named `fname`, try to
       find a mangled variant defined in this translation unit with the
       form `fname__...` and call that. This is a pragmatic compatibility
       fix for simple cases where the source calls an overloaded name
       (e.g., `sum`) but codegen produced mangled definitions
       (`sum__List_int`, `sum__List_Vec2i`). Prefer the first match. */
    const char *mangled_found = NULL;
    if (cg && fname) {
      size_t base_len = strlen(fname);
      /* Prefer a candidate with the same number of arguments (arity) when possible. */
      if (cg->defined_arity) {
        for (int i = 0; i < cg->defined_n; i++) {
          const char *dn = cg->defined_names[i];
          if (!dn) continue;
          if (strncmp(dn, fname, base_len) == 0 && dn[base_len] == '_' && dn[base_len+1] == '_' && cg->defined_arity[i] == nargs) {
            mangled_found = dn;
            break;
          }
        }
      }
      /* Fallback: first prefix match if exact-arity candidate not found. */
      if (!mangled_found) {
        for (int i = 0; i < cg->defined_n; i++) {
          const char *dn = cg->defined_names[i];
          if (!dn) continue;
          if (strncmp(dn, fname, base_len) == 0 && dn[base_len] == '_' && dn[base_len+1] == '_') {
            mangled_found = dn;
            break;
          }
        }
      }
    }
    if (mangled_found) {
      emit(cg, "  brasl %%r14,%s", mangled_found);
    } else {
      emit(cg, "  brasl %%r14,%s", fname); // result in r2
    }
  }
  
  if (!strcmp(fname, "puts") || !strcmp(fname, "printf")) {
    emit(cg, "  # Flush stdout after %s to ensure immediate output", fname);
    emit(cg, "  larl %%r2,stdout");
    emit(cg, "  lg   %%r2,0(%%r2)");
    emit(cg, "  brasl %%r14,fflush");
  }
}

static void gen_binop(CG *cg, const ASTNode *expr) {
  // binop: left, op(token op:*), right
  const ASTNode *L = expr->children[0];
  const ASTNode *OP = expr->children[1];
  const ASTNode *R = expr->children[2];
  const char *op = (OP && is_token_kind(OP, "op")) ? after_colon(OP->label) : "?";

  // comparisons should be handled in cond context; here we return 0/1.
  if (is_cmp_op(op)) {
    // Compute L and R, compare, set r2 = 0/1
    int lbl_true = new_label(cg);
    int lbl_end  = new_label(cg);

    gen_expr(cg, L);
    emit_push_r2(cg);
    gen_expr(cg, R);
    emit_pop_to_r3(cg);              // r3 = L, r2 = R
    emit(cg, "  cgr  %%r3,%%r2");     // sets CC based on (r3 - r2)

    if (!strcmp(op, "==")) emit(cg, "  je   .L%d", lbl_true);
    else if (!strcmp(op, "!=")) emit(cg, "  jne  .L%d", lbl_true);
    else if (!strcmp(op, "<")) emit(cg, "  jl   .L%d", lbl_true);
    else if (!strcmp(op, "<=")) emit(cg, "  jle  .L%d", lbl_true);
    else if (!strcmp(op, ">")) emit(cg, "  jh   .L%d", lbl_true);   // high (signed greater) in GAS mnemonics can be jh/jg; jh works too
    else if (!strcmp(op, ">=")) emit(cg, "  jhe  .L%d", lbl_true);  // >=
    else emit(cg, "  # unknown cmp op");

    emit(cg, "  lghi %%r2,0");
    emit(cg, "  j    .L%d", lbl_end);
    emit_label(cg, lbl_true);
    emit(cg, "  lghi %%r2,1");
    emit_label(cg, lbl_end);
    return;
  }

  // arithmetic:
  gen_expr(cg, L);
  emit_push_r2(cg);
  gen_expr(cg, R);
  emit_pop_to_r3(cg); // r3 = L, r2 = R

  if (!strcmp(op, "+")) {
    emit(cg, "  agr  %%r2,%%r3"); // r2 = r2 + r3
  } else if (!strcmp(op, "-")) {
    emit(cg, "  sgr  %%r3,%%r2"); // r3 = L - R
    emit(cg, "  lgr  %%r2,%%r3");
  } else if (!strcmp(op, "*")) {
    // msgr %r2,%r3: r2 = r2 * r3 (64-bit result in r2)
    emit(cg, "  msgr %%r2,%%r3");   // r2 = R * L, result in r2
  } else if (!strcmp(op, "/")) {
    emit(cg, "  lgr  %%r4,%%r2");   // divisor = R
    emit(cg, "  lgr  %%r2,%%r3");   // copy dividend L
    emit(cg, "  srag %%r2,%%r2,63"); // sign-extend high part (all 0 or all 1)
    // dividend pair: r2:r3, divisor: r4
    emit(cg, "  dsgr %%r2,%%r4");   // remainder in r2, quotient in r3
    emit(cg, "  lgr  %%r2,%%r3");   // вернуть quotient в r2
  } else if (!strcmp(op, "%")) {
    emit(cg, "  lgr  %%r4,%%r2");   // divisor = R
    emit(cg, "  lgr  %%r2,%%r3");
    emit(cg, "  srag %%r2,%%r2,63");
    emit(cg, "  dsgr %%r2,%%r4");   // remainder in r2, quotient in r3
    // remainder уже в r2, ничего не копируем
  } else {
    emit(cg, "  # ERROR: unknown binop '%s'", op);
    emit(cg, "  lghi %%r2,0");
  }
}

static void gen_unop(CG *cg, const ASTNode *expr) {
  // unop: op, operand
  const ASTNode *OP = expr->children[0];
  const ASTNode *X  = expr->children[1];
  const char *op = (OP && is_token_kind(OP, "op")) ? after_colon(OP->label) : "?";

  gen_expr(cg, X);

  if (!strcmp(op, "-")) {
    // r2 = -r2
    emit(cg, "  lghi %%r3,0");
    emit(cg, "  sgr  %%r3,%%r2");
    emit(cg, "  lgr  %%r2,%%r3");
  } else if (!strcmp(op, "+")) {
    // no-op
  } else {
    emit(cg, "  # ERROR: unknown unop '%s'", op);
  }
}

static void gen_assign(CG *cg, const ASTNode *expr) {
  // assign: id, expr
  const ASTNode *idn = expr->children[0];
  const ASTNode *rhs = expr->children[1];
  const char *name = (idn && is_token_kind(idn, "id")) ? after_colon(idn->label) : NULL;

  gen_expr(cg, rhs);           // result -> r2
  if (name) emit_store_local(cg, name);
}

static void gen_compound_assign(CG *cg, const ASTNode *expr) {
  // compound_assign: id, op, rhs
  const ASTNode *idn = expr->children[0];
  const ASTNode *opn = expr->children[1];
  const ASTNode *rhs = expr->children[2];

  const char *name = (idn && is_token_kind(idn, "id")) ? after_colon(idn->label) : NULL;
  const char *op   = (opn && is_token_kind(opn, "op")) ? after_colon(opn->label) : NULL;

  if (!name || !op) {
    emit(cg, "  # ERROR: malformed compound_assign");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // load current value -> r2, push
  emit_load_local(cg, name);
  emit_push_r2(cg);

  // rhs -> r2, pop old -> r3
  gen_expr(cg, rhs);
  emit_pop_to_r3(cg);

  if (!strcmp(op, "+=")) {
    emit(cg, "  agr  %%r2,%%r3");
  } else if (!strcmp(op, "-=")) {
    emit(cg, "  sgr  %%r3,%%r2");
    emit(cg, "  lgr  %%r2,%%r3");
} else if (!strcmp(op, "*=")) {
    // r3 = old, r2 = rhs
    // msgr %r2,%r3: r2 = r2 * r3 (64-bit result in r2)
    emit(cg, "  msgr %%r2,%%r3");   // r2 = rhs * old, result in r2

  } else if (!strcmp(op, "/=")) {
    // dividend = old (в r3), divisor = rhs (в r2)
    emit(cg, "  lgr  %%r4,%%r2");   // r4 = divisor
    emit(cg, "  lgr  %%r2,%%r3");   // r2 = old (копия)
    emit(cg, "  srag %%r2,%%r2,63"); // high = sign(old)
    emit(cg, "  dsgr %%r2,%%r4");   // remainder in r2, quotient in r3
    emit(cg, "  lgr  %%r2,%%r3");   // вернуть quotient в r2

  } else if (!strcmp(op, "%=")) {
    emit(cg, "  lgr  %%r4,%%r2");   // r4 = divisor
    emit(cg, "  lgr  %%r2,%%r3");   // r2 = old (копия)
    emit(cg, "  srag %%r2,%%r2,63");
    emit(cg, "  dsgr %%r2,%%r4");   // remainder in r2, quotient in r3
    // remainder уже в r2, ничего не копируем

  } else {
    emit(cg, "  # ERROR: unknown compound op '%s'", op);
    emit(cg, "  lghi %%r2,0");
  }

  // store back
  emit_store_local(cg, name);
}

static void gen_index(CG *cg, const ASTNode *expr) {
  // index: id, args(list) ; трактуем как *(base + idx*8)
  const ASTNode *idn = expr->children[0];
  const char *base_name = (idn && is_token_kind(idn, "id")) ? after_colon(idn->label) : NULL;

  const ASTNode *args = (expr->numChildren > 1) ? expr->children[1] : NULL;
  const ASTNode *list = NULL;

  if (args && args->label && strcmp(args->label, "args") == 0 && args->numChildren > 0) {
    list = args->children[0];
  }
  if (!base_name || !list || !list->label || strcmp(list->label, "list") != 0 || list->numChildren < 1) {
    emit(cg, "  # ERROR: malformed index");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // base pointer -> r3, idx -> r2
  emit_load_local(cg, base_name);
  emit(cg, "  lgr  %%r3,%%r2");
  gen_expr(cg, list->children[0]); // idx -> r2

  // r2 = idx*8
  emit(cg, "  sllg %%r2,%%r2,3");
  // r1 = base + idx*8
  emit(cg, "  la   %%r1,0(%%r3,%%r2)");
  // load *(r1)
  emit(cg, "  lg   %%r2,0(%%r1)");
}

static void gen_assign_index(CG *cg, const ASTNode *expr) {
  // assign_index: id, argExprList, rhs
  const ASTNode *idn = expr->children[0];
  const ASTNode *args = (expr->numChildren > 1) ? expr->children[1] : NULL;
  const ASTNode *rhs = (expr->numChildren > 2) ? expr->children[2] : NULL;

  const char *base_name = (idn && is_token_kind(idn, "id")) ? after_colon(idn->label) : NULL;
  const ASTNode *list = NULL;

  if (args && args->label && strcmp(args->label, "args") == 0 && args->numChildren > 0) {
    list = args->children[0];
  }
  if (!base_name || !list || !list->label || strcmp(list->label, "list") != 0 || list->numChildren < 1 || !rhs) {
    emit(cg, "  # ERROR: malformed assign_index");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // Compute base pointer -> r3
  int off = 0;
  if (locals_get_offset(&cg->locals, base_name, &off)) {
    // base is a local variable (pointer)
    emit_load_local(cg, base_name);
    emit(cg, "  lgr  %%r3,%%r2");
  } else {
    // base not found as local — try treating it as a field of 'this'
    if (locals_get_offset(&cg->locals, "this", &off)) {
      // load this pointer into r2 -> r3
      emit_load_local(cg, "this");
      emit(cg, "  lgr  %%r3,%%r2");
      // Find field offset from cg map (prefer current class)
      int fo = 8; int foundf = 0;
      if (cg) {
        for (int i = 0; i < cg->field_n; i++) {
          if (cg->field_names[i] && strcmp(cg->field_names[i], base_name) == 0) {
            if (cg->cur_func && strstr(cg->cur_func, "__")) {
              char cls[256];
              const char *p = strstr(cg->cur_func, "__");
              size_t len = (size_t)(p - cg->cur_func);
              if (len < sizeof(cls)) {
                memcpy(cls, cg->cur_func, len);
                cls[len] = '\0';
                if (!strcmp(cls, cg->field_class_names[i])) { fo = cg->field_offsets[i]; foundf = 1; break; }
              }
            }
          }
        }
        if (!foundf) {
          for (int i = 0; i < cg->field_n; i++) {
            if (cg->field_names[i] && strcmp(cg->field_names[i], base_name) == 0) { fo = cg->field_offsets[i]; foundf = 1; break; }
          }
        }
      }
      emit(cg, "  # field '%s' offset %d (this.%s)", base_name, fo, base_name);
      emit(cg, "  lg   %%r2,%d(%%r3)", fo); // r2 = this->base (pointer)
      emit(cg, "  lgr  %%r3,%%r2");
    } else {
      // unknown base — produce debug-friendly zero
      emit(cg, "  # ERROR: unknown base '%s' for assign_index", base_name);
      emit(cg, "  lghi %%r2,0");
      emit(cg, "  lgr  %%r3,%%r2");
    }
  }

  // compute index -> r2
  gen_expr(cg, list->children[0]);
  // r2 = idx * 8
  emit(cg, "  sllg %%r2,%%r2,3");
  // r1 = base + idx*8
  emit(cg, "  la   %%r1,0(%%r3,%%r2)");

  // push address (move into r2 then push)
  emit(cg, "  lgr  %%r2,%%r1");
  emit_push_r2(cg);

  // evaluate rhs -> r2
  gen_expr(cg, rhs);

  // pop address into r3
  emit_pop_to_r3(cg); // r3 = addr

  // store r2 into *(r3) but guard against NULL addresses to avoid segfault
  {
    int lbl_ok = new_label(cg);
    emit(cg, "  lghi %%r4,0");
    emit(cg, "  cgr  %%r3,%%r4");
    emit(cg, "  je   .L%d", lbl_ok); // if r3 == 0 jump to skip
    emit(cg, "  stg  %%r2,0(%%r3)");
    emit_label(cg, lbl_ok);
  }
}

static void gen_field_access(CG *cg, const ASTNode *expr) {
  // fieldAccess: obj, field_id
  // obj -> r3, затем load obj + field_offset -> r2
  if (expr->numChildren < 2) {
    emit(cg, "  # ERROR: malformed fieldAccess");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  const ASTNode *obj = expr->children[0];
  const ASTNode *field_id = expr->children[1];
  const char *field_name = (field_id && is_token_kind(field_id, "id")) ? after_colon(field_id->label) : NULL;

  if (!field_name) {
    emit(cg, "  # ERROR: fieldAccess without field name");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // Evaluate object expression -> r2
  gen_expr(cg, obj);
  emit(cg, "  lgr  %%r3,%%r2"); // r3 = object pointer
  // Look up field offset in cg map
  int found = 0;
  int off = 8; // default
  if (cg && field_name) {
    for (int i = 0; i < cg->field_n; i++) {
      if (cg->field_names[i] && cg->field_class_names[i] && strcmp(cg->field_names[i], field_name) == 0) {
        // If we're inside a method, prefer matching class
        if (cg->cur_func && strstr(cg->cur_func, "__")) {
          char cls[256];
          const char *p = strstr(cg->cur_func, "__");
          size_t len = (size_t)(p - cg->cur_func);
          if (len < sizeof(cls)) {
            memcpy(cls, cg->cur_func, len);
            cls[len] = '\0';
            if (!strcmp(cls, cg->field_class_names[i])) {
              off = cg->field_offsets[i]; found = 1; break;
            }
          }
        }
      }
    }
    if (!found) {
      for (int i = 0; i < cg->field_n; i++) {
        if (cg->field_names[i] && strcmp(cg->field_names[i], field_name) == 0) { off = cg->field_offsets[i]; found = 1; break; }
      }
    }
  }
  emit(cg, "  # field '%s' offset %d", field_name, off);
  emit(cg, "  lg   %%r2,%d(%%r3)", off);
}

static void gen_method_call(CG *cg, const ASTNode *expr) {
  // methodCall: obj, method_id, args
  if (expr->numChildren < 2) {
    emit(cg, "  # ERROR: malformed methodCall");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  const ASTNode *obj = expr->children[0];
  const ASTNode *method_id = expr->children[1];
  const ASTNode *args = (expr->numChildren > 2) ? expr->children[2] : NULL;

  const char *method_name = (method_id && is_token_kind(method_id, "id")) ? after_colon(method_id->label) : NULL;

  if (!method_name) {
    emit(cg, "  # ERROR: methodCall without method name");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // Evaluate object expression -> r2, push it
  gen_expr(cg, obj);
  emit_push_r2(cg); // Push object as first argument

  // Evaluate method arguments
  const ASTNode *list = NULL;
  int nargs = 0;
  if (args && args->label && strcmp(args->label, "args") == 0 && args->numChildren > 0) {
    list = args->children[0];
    if (list && list->label && strcmp(list->label, "list") == 0) {
      nargs = list->numChildren;
    }
  }

  // Evaluate args left-to-right, push each result
  for (int i = 0; i < nargs; i++) {
    gen_expr(cg, list->children[i]);
    emit_push_r2(cg);
  }

  // Pop arguments into registers (object is first, so it goes to r2)
  int total_args = 1 + nargs; // object + method args
  if (total_args > 5) {
    emit(cg, "  # ERROR: >5 args not supported yet");
    for (int i = 0; i < total_args; i++) emit_pop_to_r3(cg);
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // Pop in reverse order: last arg -> r(2+nargs), object -> r2
  for (int i = total_args - 1; i >= 0; i--) {
    int reg = 2 + i;
    emit(cg, "  lg   %%r%d,0(%%r12)", reg);
    emit(cg, "  aghi %%r12,8");
  }

  // TODO: Get method slot and implementation label from TypeEnv
  // For now, use mangled name: Class__method
  emit(cg, "  # TODO: virtual method dispatch for '%s'", method_name);

  // Try a simple static dispatch: if the object is a local id and we recorded
  // its static type, call the mangled function <Type>__<method> directly.
  if (obj && obj->label && is_token_kind(obj, "id")) {
    const char *obj_name = after_colon(obj->label);
    const char *static_type = locals_get_type(&cg->locals, obj_name);
    if (static_type) {
      char mangled[256];
      snprintf(mangled, sizeof(mangled), "%s__%s", static_type, method_name);
      emit(cg, "  # static dispatch to %s (object '%s' has type %s)", mangled, obj_name, static_type);
      emit(cg, "  brasl %%r14,%s", mangled);
      return;
    }
  }
  // If static dispatch is not possible, try to find a top-level mangled
  // implementation matching "<ClassOrBase>__<method>" where the arity
  // (including the implicit 'this' argument) matches total_args. This
  // avoids dereferencing a null vtable pointer when objects are created
  // with uninitialized vptr (placeholders used during codegen).
  {
    int total_args = 1 + nargs; /* object + method args */
    const char *candidate = NULL;
    if (cg && method_name) {
      size_t mlen = strlen(method_name);
      /* prefer candidate with matching arity */
      if (cg->defined_names && cg->defined_arity) {
        for (int i = 0; i < cg->defined_n; i++) {
          const char *dn = cg->defined_names[i];
          if (!dn) continue;
          /* look for suffix __<method> */
          const char *p = strstr(dn, "__");
          if (!p) continue;
          const char *suf = p + 2;
          if (!strcmp(suf, method_name) || (strncmp(suf, method_name, mlen) == 0 && suf[mlen] == '\0')) {
            if (cg->defined_arity[i] == total_args) { candidate = dn; break; }
          }
        }
      }
      /* fallback: first suffix match */
      if (!candidate && cg && cg->defined_names) {
        for (int i = 0; i < cg->defined_n; i++) {
          const char *dn = cg->defined_names[i];
          if (!dn) continue;
          const char *p = strstr(dn, "__");
          if (!p) continue;
          const char *suf = p + 2;
          if (!strcmp(suf, method_name) || (strncmp(suf, method_name, mlen) == 0 && suf[mlen] == '\0')) { candidate = dn; break; }
        }
      }
    }
    if (candidate) {
      emit(cg, "  # static-like dispatch to %s (method lookup by name+arity)", candidate);
      emit(cg, "  brasl %%r14,%s", candidate);
      return;
    }
  }

  /* Fallback: attempt vtable dispatch (may crash if vptr is NULL). */
  emit(cg, "  # Load vtable pointer from object (offset 0)");
  emit(cg, "  lg   %%r1,0(%%r2)"); // r1 = vtable pointer
  emit(cg, "  # TODO: Load method pointer from vtable[slot]");
  emit(cg, "  # For now, call mangled name (placeholder)");
  emit(cg, "  brasl %%r14,unknown_method"); // Placeholder
}

static void gen_new(CG *cg, const ASTNode *expr) {
  // new: class_id, args
  if (expr->numChildren < 1) {
    emit(cg, "  # ERROR: malformed new");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  const ASTNode *class_id = expr->children[0];
  const ASTNode *args = (expr->numChildren > 1) ? expr->children[1] : NULL;

  const char *class_name = (class_id && is_token_kind(class_id, "id")) ? after_colon(class_id->label) : NULL;

  if (!class_name) {
    emit(cg, "  # ERROR: new without class name");
    emit(cg, "  lghi %%r2,0");
    return;
  }

  // TODO: Get class size from TypeEnv
  // For now, allocate 16 bytes (vptr + one field)
  emit(cg, "  # allocate object of class '%s' (heap)", class_name);
  emit(cg, "  # Allocate memory using libc malloc(size)");
  emit(cg, "  lghi %%r2,16"); /* size */
  emit(cg, "  # call __runtime_malloc(size) -> returns pointer in %%r2");
  emit(cg, "  brasl %%r14,__runtime_malloc");
  emit(cg, "  lgr  %%r1,%%r2"); /* r1 = pointer to allocated memory */
  // Initialize vtable pointer: point to a per-class vtable symbol so
  // method dispatch that reads the vptr won't dereference a NULL address.
  {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s_vtable", class_name);
    emit(cg, "  # initialize vtable pointer to %s", buf);
    emit(cg, "  larl %%r2,%s", buf);
    emit(cg, "  stg  %%r2,0(%%r1)");
    /* record we need a vtable symbol for this class in case the class
       definition does not appear in the AST (so we can emit a placeholder
       vtable later and avoid linker errors). */
    cg_add_required_vtable(cg, class_name);
  }

  // Evaluate constructor arguments if any
  const ASTNode *list = NULL;
  int nargs = 0;
  if (args && args->label && strcmp(args->label, "args") == 0 && args->numChildren > 0) {
    list = args->children[0];
    if (list && list->label && strcmp(list->label, "list") == 0) {
      nargs = list->numChildren;
    }
  }

  // TODO: Call constructor if needed
  if (nargs > 0) {
    emit(cg, "  # TODO: call constructor with %d arguments", nargs);
  }

  // Return object pointer in r2
  emit(cg, "  lgr  %%r2,%%r1");
}

static void gen_expr(CG *cg, const ASTNode *expr) {
  if (!expr || !expr->label) {
    emit(cg, "  lghi %%r2,0");
    return;
  }

  const char *L = expr->label;

  if (!strcmp(L, "binop") && expr->numChildren >= 3) {
    gen_binop(cg, expr);
    return;
  }
  if (!strcmp(L, "unop") && expr->numChildren >= 2) {
    gen_unop(cg, expr);
    return;
  }
  if (!strcmp(L, "assign") && expr->numChildren >= 2) {
    gen_assign(cg, expr);
    return;
  }
  if (!strcmp(L, "compound_assign") && expr->numChildren >= 3) {
    gen_compound_assign(cg, expr);
    return;
  }
  if (!strcmp(L, "call")) {
    gen_call(cg, expr);
    return;
  }
  if (!strcmp(L, "index")) {
    gen_index(cg, expr);
    return;
  }
  if (!strcmp(L, "assign_index")) {
    gen_assign_index(cg, expr);
    return;
  }
  if (!strcmp(L, "fieldAccess") && expr->numChildren >= 2) {
    gen_field_access(cg, expr);
    return;
  }
  if (!strcmp(L, "methodCall") && expr->numChildren >= 2) {
    gen_method_call(cg, expr);
    return;
  }
  if (!strcmp(L, "new") && expr->numChildren >= 1) {
    gen_new(cg, expr);
    return;
  }
  if (!strcmp(L, "address") && expr->numChildren >= 1) {
    /* Address-of: &var */
    const ASTNode *id_node = expr->children[0];
    const char *name = (id_node && is_token_kind(id_node, "id")) ? after_colon(id_node->label) : NULL;
    if (name) {
      // Load address of local variable into r2
      int offset = 0;
      if (locals_get_offset(&cg->locals, name, &offset)) {
        emit(cg, "  la   %%r2,%d(%%r11)", offset); // Load address: r2 = r11 + offset
      } else {
        emit(cg, "  # ERROR: unknown variable '%s' for address-of", name);
        emit(cg, "  lghi %%r2,0");
      }
    } else {
      emit(cg, "  # ERROR: malformed address-of expression");
      emit(cg, "  lghi %%r2,0");
    }
    return;
  }

  // leaf tokens:
  if (is_token_kind(expr, "id")) {
    emit_load_local(cg, after_colon(expr->label));
    return;
  }
  if (is_token_kind(expr, "string")) {
    emit_load_string(cg, after_colon(expr->label));
    return;
  }
  if (is_token_kind(expr, "dec") || is_token_kind(expr, "hex") ||
      is_token_kind(expr, "bits") || is_token_kind(expr, "bool") ||
      is_token_kind(expr, "char")) {
    int64_t v = parse_int_literal_label(expr);
    emit_load_imm64(cg, v);
    return;
  }

  emit(cg, "  # ERROR: unknown expr node '%s'", L);
  emit(cg, "  lghi %%r2,0");
}

// ------------------------- condition branching -------------------------

static void gen_cond_branch(CG *cg, const ASTNode *cond, int false_label) {
  // Идея:
  // - если cond это binop со сравнением: делаем cgr и ветку на FALSE
  // - иначе: вычисляем cond -> r2; ltgr r2,r2; je false

  if (cond && cond->label && !strcmp(cond->label, "binop") && cond->numChildren >= 3) {
    const ASTNode *L = cond->children[0];
    const ASTNode *OP = cond->children[1];
    const ASTNode *R = cond->children[2];
    const char *op = (OP && is_token_kind(OP, "op")) ? after_colon(OP->label) : NULL;

    if (op && is_cmp_op(op)) {
      gen_expr(cg, L);
      emit_push_r2(cg);
      gen_expr(cg, R);
      emit_pop_to_r3(cg);           // r3=L, r2=R
      emit(cg, "  cgr  %%r3,%%r2");

      // branch to FALSE if condition fails:
      if (!strcmp(op, "==")) {
        emit(cg, "  jne  .L%d", false_label);
      } else if (!strcmp(op, "!=")) {
        emit(cg, "  je   .L%d", false_label);
      } else if (!strcmp(op, "<")) {
        emit(cg, "  jhe  .L%d", false_label); // if !(L<R) => L>=R
      } else if (!strcmp(op, "<=")) {
        emit(cg, "  jh   .L%d", false_label); // if !(L<=R) => L>R
      } else if (!strcmp(op, ">")) {
        emit(cg, "  jle  .L%d", false_label); // if !(L>R) => L<=R
      } else if (!strcmp(op, ">=")) {
        emit(cg, "  jl   .L%d", false_label); // if !(L>=R) => L<R
      } else {
        emit(cg, "  # unknown cmp op -> treat as false on zero");
        emit(cg, "  je   .L%d", false_label);
      }
      return;
    }
  }

  // fallback: compute to r2 and test nonzero
  gen_expr(cg, cond);
  emit(cg, "  ltgr %%r2,%%r2");      // sets CC based on r2
  emit(cg, "  je   .L%d", false_label);
}

// ------------------------- statement generation -------------------------

static void gen_vardecl(CG *cg, const ASTNode *stmt) {
  // vardecl: typeRef, vars
  if (stmt->numChildren < 2) return;
  const ASTNode *vars = stmt->children[1];
  if (!vars || !vars->label || strcmp(vars->label, "vars") != 0) return;

  for (int i = 0; i + 1 < vars->numChildren; i += 2) {
    const ASTNode *idn = vars->children[i];
    const ASTNode *opt = vars->children[i + 1];

    const char *name = (idn && is_token_kind(idn, "id")) ? after_colon(idn->label) : NULL;
    if (!name) continue;

    if (opt && opt->label && !strcmp(opt->label, "assign") && opt->numChildren > 0) {
      gen_expr(cg, opt->children[0]);
    } else {
      emit(cg, "  lghi %%r2,0");
    }
    emit_store_local(cg, name);
  }
}

static void gen_block(CG *cg, const ASTNode *block) {
  // block: children[0]=stmts
  if (!block || !block->label || strcmp(block->label, "block") != 0) return;
  if (block->numChildren <= 0) return;
  const ASTNode *stmts = block->children[0];
  if (!stmts || !stmts->label || strcmp(stmts->label, "stmts") != 0) return;

  for (int i = 0; i < stmts->numChildren; i++) {
    gen_stmt(cg, stmts->children[i]);
  }
}

static void gen_if(CG *cg, const ASTNode *stmt) {
  // if: [0]=cond, [1]=then, [2]=optElse (else/noelse)
  if (stmt->numChildren < 2) return;
  const ASTNode *cond = stmt->children[0];
  const ASTNode *thenS = stmt->children[1];
  const ASTNode *elseN = (stmt->numChildren > 2) ? stmt->children[2] : NULL;

  int lbl_else = new_label(cg);
  int lbl_end  = new_label(cg);

  gen_cond_branch(cg, cond, lbl_else);
  gen_stmt(cg, thenS);
  emit(cg, "  j    .L%d", lbl_end);

  emit_label(cg, lbl_else);
  if (elseN && elseN->label && strcmp(elseN->label, "else") == 0 && elseN->numChildren > 0) {
    gen_stmt(cg, elseN->children[0]);
  }
  emit_label(cg, lbl_end);
}

static void gen_while(CG *cg, const ASTNode *stmt) {
  // while: [0]=cond, [1]=body
  if (stmt->numChildren < 2) return;
  const ASTNode *cond = stmt->children[0];
  const ASTNode *body = stmt->children[1];

  int lbl_head = new_label(cg);
  int lbl_exit = new_label(cg);

  break_push(cg, lbl_exit);

  emit_label(cg, lbl_head);
  gen_cond_branch(cg, cond, lbl_exit);
  gen_stmt(cg, body);
  emit(cg, "  j    .L%d", lbl_head);

  emit_label(cg, lbl_exit);
  break_pop(cg);
}

static void gen_do_while(CG *cg, const ASTNode *stmt) {
  // doWhile: [0]=block, [1]=cond
  if (stmt->numChildren < 2) return;
  const ASTNode *body = stmt->children[0];
  const ASTNode *cond = stmt->children[1];

  int lbl_body = new_label(cg);
  int lbl_exit = new_label(cg);

  break_push(cg, lbl_exit);

  emit_label(cg, lbl_body);
  gen_stmt(cg, body);

  // if cond true -> jump body, else fall to exit
  // (удобно: если false -> exit, иначе j body)
  gen_expr(cg, cond);
  emit(cg, "  ltgr %%r2,%%r2");
  emit(cg, "  jne  .L%d", lbl_body);

  emit_label(cg, lbl_exit);
  break_pop(cg);
}

static void gen_return(CG *cg, const ASTNode *stmt) {
  // return: maybe one child expr
  if (stmt->numChildren > 0) {
    gen_expr(cg, stmt->children[0]); // result -> r2
  } else {
    emit(cg, "  lghi %%r2,0");
  }
  emit(cg, "  j    .L%d", cg->epilogue_label);
}

static void gen_break(CG *cg) {
  int lbl = break_top(cg);
  if (lbl < 0) {
    emit(cg, "  # ERROR: break outside loop");
    return;
  }
  emit(cg, "  j    .L%d", lbl);
}

static void gen_stmt(CG *cg, const ASTNode *stmt) {
  if (!stmt || !stmt->label) return;

  const char *L = stmt->label;

  if (!strcmp(L, "block")) {
    gen_block(cg, stmt);
    return;
  }
  if (!strcmp(L, "vardecl")) {
    gen_vardecl(cg, stmt);
    return;
  }
  if (!strcmp(L, "exprstmt")) {
    if (stmt->numChildren > 0) gen_expr(cg, stmt->children[0]);
    return;
  }
  if (!strcmp(L, "if")) {
    gen_if(cg, stmt);
    return;
  }
  if (!strcmp(L, "while")) {
    gen_while(cg, stmt);
    return;
  }
  if (!strcmp(L, "doWhile")) {
    gen_do_while(cg, stmt);
    return;
  }
  if (!strcmp(L, "return")) {
    gen_return(cg, stmt);
    return;
  }
  if (!strcmp(L, "break")) {
    gen_break(cg);
    return;
  }

  emit(cg, "  # WARN: unknown statement '%s' ignored", L);
}

// ------------------------- function generation -------------------------

// Build a mangled type name for a type AST node. Returns malloc'd string (caller must free).
static char *mangle_type(const ASTNode *type_node) {
  if (!type_node) return dup_cstr("void");

  // token kinds: type, typeRef (leaf)
  if (is_token_kind(type_node, "type") || is_token_kind(type_node, "typeRef") || is_token_kind(type_node, "IDENTIFIER")) {
    return dup_cstr(after_colon(type_node->label));
  }

  if (type_node->label && strcmp(type_node->label, "genType") == 0 && type_node->numChildren > 0) {
    // genType: [0]=id, [1]=typeRef (param)
    const ASTNode *idn = type_node->children[0];
    const ASTNode *param = (type_node->numChildren > 1) ? type_node->children[1] : NULL;
    const char *base = idn && is_token_kind(idn, "id") ? after_colon(idn->label) : "gen";
    if (!param) return dup_cstr(base);
    char *p = mangle_type(param);
    size_t n = strlen(base) + 1 + strlen(p) + 1;
    char *s = (char*)malloc(n);
    if (!s) { free(p); return dup_cstr(base); }
    snprintf(s, n, "%s_%s", base, p);
    free(p);
    return s;
  }

  if (type_node->label && strcmp(type_node->label, "array") == 0 && type_node->numChildren > 0) {
    char *inner = mangle_type(type_node->children[0]);
    size_t n = strlen(inner) + 5;
    char *s = (char*)malloc(n);
    if (!s) { free(inner); return inner; }
    snprintf(s, n, "%s_arr", inner);
    free(inner);
    return s;
  }

  // fallback: if node has a child token, use it
  for (int i = 0; i < type_node->numChildren; i++) {
    const ASTNode *c = type_node->children[i];
    if (c && c->label && strchr(c->label, ':')) return dup_cstr(after_colon(c->label));
  }

  return dup_cstr(type_node->label ? type_node->label : "type");
}

// Return a mangled name for a function including parameter types.
// Returns malloc'd string (caller may free). Existing callers expect char* that stays alive
// until cg_free frees the defined names array, so allocating here is fine.
static const char *get_func_name(const ASTNode *funcDefOrDecl) {
  if (!funcDefOrDecl || funcDefOrDecl->numChildren < 1) return dup_cstr("unknown");
  const ASTNode *sig = funcDefOrDecl->children[0];
  if (!sig || !sig->label || strcmp(sig->label, "signature") != 0) return dup_cstr("unknown");
  if (sig->numChildren < 2) return dup_cstr("unknown");

  const ASTNode *idn = sig->children[1];
  const char *base = "unknown";
  if (idn && is_token_kind(idn, "id")) base = after_colon(idn->label);

  // args are at sig->children[2] -> args -> arglist
  if (sig->numChildren < 3) return dup_cstr(base);
  const ASTNode *args = sig->children[2];
  if (!args || !args->label || strcmp(args->label, "args") != 0) return dup_cstr(base);
  if (args->numChildren == 0) return dup_cstr(base);
  const ASTNode *arglist = args->children[0];
  if (!arglist || !arglist->label || strcmp(arglist->label, "arglist") != 0) return dup_cstr(base);

  // Build mangled: base__T1_T2...
  // Compute length
  size_t len = strlen(base) + 3; // base + '__' + null
  char **parts = NULL;
  int parts_n = 0;
  for (int i = 0; i < arglist->numChildren; i++) {
    const ASTNode *arg = arglist->children[i];
    if (!arg || !arg->label || strcmp(arg->label, "arg") != 0) continue;
    const ASTNode *type_node = (arg->numChildren > 0) ? arg->children[0] : NULL;
    char *t = mangle_type(type_node);
    if (!t) continue;
    parts = (char**)realloc(parts, (size_t)(parts_n + 1) * sizeof(char*));
    parts[parts_n++] = t;
    len += strlen(t) + 1; // underscore separator
  }

  if (parts_n == 0) {
    return dup_cstr(base);
  }

  char *res = (char*)malloc(len);
  if (!res) {
    for (int i = 0; i < parts_n; i++) free(parts[i]);
    free(parts);
    return dup_cstr(base);
  }
  strcpy(res, base);
  strcat(res, "__");
  for (int i = 0; i < parts_n; i++) {
    strcat(res, parts[i]);
    if (i + 1 < parts_n) strcat(res, "_");
    free(parts[i]);
  }
  free(parts);
  return res;
}

static void emit_prologue(CG *cg) {
  // save r6..r15 in caller save area, then allocate frame, write backchain
  emit(cg, "  stmg %%r6,%%r15,48(%%r15)");
  emit(cg, "  lgr  %%r1,%%r15");
  emit(cg, "  aghi %%r15,-%d", cg->frame_size);
  emit(cg, "  stg  %%r1,0(%%r15)");      // backchain
  emit(cg, "  lgr  %%r11,%%r15");        // frame base
  emit(cg, "  la   %%r12,%d(%%r15)", cg->frame_size); // temp stack top
}

static void emit_epilogue(CG *cg) {
  emit_label(cg, cg->epilogue_label);
  emit(cg, "  lg   %%r15,0(%%r15)");     // restore old sp from backchain
  emit(cg, "  lmg  %%r6,%%r15,48(%%r15)");
  emit(cg, "  br   %%r14");
}

static void store_params_to_locals(CG *cg, const ASTNode *signature) {
  // args in r2..r6; store each parameter into its local slot
  if (!signature || !signature->label || strcmp(signature->label, "signature") != 0) return;
  if (signature->numChildren < 3) return;

  const ASTNode *args = signature->children[2];
  if (!args || !args->label || strcmp(args->label, "args") != 0) return;
  if (args->numChildren == 0) return;

  const ASTNode *arglist = args->children[0];
  if (!arglist || !arglist->label || strcmp(arglist->label, "arglist") != 0) return;

  int reg = 2;
  for (int i = 0; i < arglist->numChildren && reg <= 6; i++, reg++) {
    const ASTNode *arg = arglist->children[i];
    if (!arg || !arg->label || strcmp(arg->label, "arg") != 0) continue;
    if (arg->numChildren < 2) continue;

    const ASTNode *idn = arg->children[1];
    if (idn && is_token_kind(idn, "id")) {
      const char *name = after_colon(idn->label);
      int off = 0;
      if (locals_get_offset(&cg->locals, name, &off)) {
        emit(cg, "  stg  %%r%d,%d(%%r11)", reg, off);
      }
    }
  }

  if (arglist->numChildren > 5) {
    emit(cg, "  # WARN: >5 params not handled (need stack args)");
  }
}

static int is_standard_library_func(const char *name) {
  // List of standard library functions that should remain extern
  if (!name) return 0;
  return (!strcmp(name, "printf") || !strcmp(name, "scanf") || 
          !strcmp(name, "malloc") || !strcmp(name, "free") ||
          !strcmp(name, "fopen") || !strcmp(name, "fclose") ||
          !strcmp(name, "fread") || !strcmp(name, "fwrite") ||
          !strcmp(name, "read") || !strcmp(name, "write") ||
          !strcmp(name, "atoi") || !strcmp(name, "atol") ||
          !strcmp(name, "puts") || !strcmp(name, "putchar") ||
          !strcmp(name, "gets") || !strcmp(name, "getchar") ||
          !strcmp(name, "exit") || !strcmp(name, "abort") ||
          !strcmp(name, "memcpy") || !strcmp(name, "memset") ||
          !strcmp(name, "strlen") || !strcmp(name, "strcmp") ||
          !strcmp(name, "fflush"));
}

static void gen_function_stub(CG *cg, const ASTNode *fn) {
  // Generate a stub implementation for a function declaration
  const char *name = get_func_name(fn);
  
  emit(cg, "");
  emit(cg, "  .text");
  emit(cg, "  .globl %s", name);
  emit(cg, "  .type  %s,@function", name);
  emit(cg, "%s:", name);
  
  // Minimal prologue
  emit(cg, "  stmg %%r6,%%r15,48(%%r15)");
  emit(cg, "  lgr  %%r1,%%r15");
  emit(cg, "  aghi %%r15,-160");
  emit(cg, "  stg  %%r1,0(%%r15)");
  
  // Return 0
  emit(cg, "  lghi %%r2,0");
  
  // Epilogue
  emit(cg, "  lg   %%r15,0(%%r15)");
  emit(cg, "  lmg  %%r6,%%r15,48(%%r15)");
  emit(cg, "  br   %%r14");
  emit(cg, "  .size %s, .-%s", name, name);
}

static void gen_function_with_name(CG *cg, const ASTNode *fn, const char *name) {
  if (!name) name = "unknown";
  cg->cur_func = name;

  locals_free(&cg->locals);
  locals_init(&cg->locals);

  // layout locals:
  // header area: 160 bytes (we keep ABI-friendly convention)
  // locals start: offset 160 from r11
  int next_off = 160;

  // parameters as locals first:
  const ASTNode *sig = (fn->numChildren > 0) ? fn->children[0] : NULL;
  collect_params_as_locals(cg, sig, &next_off);

  // then locals from body:
  const ASTNode *body = (fn->numChildren > 1) ? fn->children[1] : NULL;
  collect_locals_from_block(cg, body, &next_off);

  cg->locals_size = next_off - 160;

  // scratch for expression temp pushes
  cg->scratch_size = 512;

  cg->frame_size = align16(160 + cg->locals_size + cg->scratch_size);
  if (cg->frame_size > 4000) {
    // чтобы la disp не вышел за 4095
    cg->scratch_size = 256;
    cg->frame_size = align16(160 + cg->locals_size + cg->scratch_size);
  }

  cg->epilogue_label = new_label(cg);

  emit(cg, "");
  emit(cg, "  .text");
  emit(cg, "  .globl %s", name);
  emit(cg, "  .type  %s,@function", name);
  emit(cg, "%s:", name);

  emit_prologue(cg);
  store_params_to_locals(cg, sig);

  // если это funcDef, то есть тело block; если funcDecl — просто return 0
  if (fn->label && strcmp(fn->label, "funcDef") == 0 && fn->numChildren >= 2) {
    gen_stmt(cg, fn->children[1]);
  }

  // default return 0 if falls through
  emit(cg, "  lghi %%r2,0");
  emit(cg, "  j    .L%d", cg->epilogue_label);

  emit_epilogue(cg);
  emit(cg, "  .size %s, .-%s", name, name);

  /* clear cur_func to avoid dangling pointer usage outside this function */
  cg->cur_func = NULL;
}

/* Wrapper kept for compatibility: allocates name, calls the generator and frees name */
static void gen_function(CG *cg, const ASTNode *fn) {
  char *name = (char*)get_func_name(fn);
  gen_function_with_name(cg, fn, name);
  free(name);
}

// ------------------------- top-level generation -------------------------

static void emit_rodata(CG *cg) {
  if (cg->str_pool.n == 0 && cg->const_pool.n == 0) return;

  emit(cg, "");
  emit(cg, "  .section .rodata");

  for (int i = 0; i < cg->str_pool.n; i++) {
    emit(cg, ".LC%d:", cg->str_pool.v[i].label_id);
    // текст уже с кавычками и escape'ами из lexer
    emit(cg, "  .asciz %s", cg->str_pool.v[i].text);
  }

  for (int i = 0; i < cg->const_pool.n; i++) {
    emit(cg, ".LCQ%d:", cg->const_pool.v[i].label_id);
    // 64-bit literal
    emit(cg, "  .quad %lld", (long long)cg->const_pool.v[i].value);
  }
}

// Helper to extract class name from class node
static const char *extract_class_name_from_ast(const ASTNode *class_node) {
  if (!class_node || class_node->numChildren < 1) return NULL;
  const ASTNode *idn = class_node->children[0];
  if (idn && is_token_kind(idn, "id")) {
    return after_colon(idn->label);
  }
  return NULL;
}

// Helper to extract base class name from class node
static const char *extract_base_name_from_ast(const ASTNode *class_node) {
  if (!class_node || class_node->numChildren < 2) return NULL;
  // Look for "extends" child
  for (int i = 0; i < class_node->numChildren; i++) {
    const ASTNode *child = class_node->children[i];
    if (child && child->label && !strcmp(child->label, "extends")) {
      if (child->numChildren > 0) {
        const ASTNode *base_id = child->children[0];
        if (base_id && is_token_kind(base_id, "id")) {
          return after_colon(base_id->label);
        }
      }
    }
  }
  return NULL;
}

// Helper to collect field names from class node
static void collect_fields_from_class(const ASTNode *class_node, const char ***field_names, int *n_fields) {
  *field_names = NULL;
  *n_fields = 0;
  
  if (!class_node) return;
  
  // Find members container
  const ASTNode *members = NULL;
  for (int i = 0; i < class_node->numChildren; i++) {
    const ASTNode *child = class_node->children[i];
    if (child && child->label && !strcmp(child->label, "members")) {
      members = child;
      break;
    }
  }
  
  if (!members) return;
  
  // Collect fields from members
  for (int i = 0; i < members->numChildren; i++) {
    const ASTNode *member = members->children[i];
    if (!member || !member->label) continue;
    
    // Look for field nodes
    const ASTNode *field_node = NULL;
    if (!strcmp(member->label, "member") && member->numChildren > 0) {
      // member can have modifier and then field
      for (int j = 0; j < member->numChildren; j++) {
        const ASTNode *child = member->children[j];
        if (child && child->label && !strcmp(child->label, "field")) {
          field_node = child;
          break;
        }
      }
    } else if (!strcmp(member->label, "field")) {
      field_node = member;
    }
    
    if (field_node && field_node->numChildren >= 2) {
      // field: optTypeRef, fieldList
      const ASTNode *field_list = field_node->children[1];
      if (field_list && field_list->label && !strcmp(field_list->label, "fieldlist")) {
        for (int j = 0; j < field_list->numChildren; j++) {
          const ASTNode *field_id = field_list->children[j];
          if (field_id && is_token_kind(field_id, "id")) {
            const char *field_name = after_colon(field_id->label);
            if (field_name) {
              int new_cap = (*n_fields == 0) ? 4 : (*n_fields * 2);
              const char **new_names = (const char **)realloc(*field_names, new_cap * sizeof(const char*));
              if (new_names) {
                *field_names = new_names;
                (*field_names)[*n_fields] = field_name;
                (*n_fields)++;
              }
            }
          }
        }
      }
    }
  }
}

// Generate type information section
static void emit_type_info(CG *cg, const ASTNode *root) {
  if (!root || strcmp(root->label, "source") != 0 || root->numChildren < 1) return;
  
  const ASTNode *items = root->children[0];
  if (!items || !items->label || strcmp(items->label, "items") != 0) return;
  
  emit(cg, "");
  emit(cg, "  .section .data.typeinfo");
  emit(cg, "  .align 8");
  
  // Collect all classes
  for (int i = 0; i < items->numChildren; i++) {
    const ASTNode *item = items->children[i];
    if (!item || !item->label || strcmp(item->label, "class") != 0) continue;
    
    const char *class_name = extract_class_name_from_ast(item);
    const char *base_name = extract_base_name_from_ast(item);
    
    if (!class_name) continue;
    
    const char **field_names = NULL;
    int n_fields = 0;
    collect_fields_from_class(item, &field_names, &n_fields);

    /* populate cg field map entries for this class */
    for (int j = 0; j < n_fields; j++) {
      int new_n = cg->field_n + 1;
      const char **nc = (const char **)realloc((void*)cg->field_class_names, (size_t)new_n * sizeof(char*));
      if (nc) cg->field_class_names = nc;
      const char **nn = (const char **)realloc((void*)cg->field_names, (size_t)new_n * sizeof(char*));
      if (nn) cg->field_names = nn;
      int *no = (int *)realloc(cg->field_offsets, (size_t)new_n * sizeof(int));
      if (no) cg->field_offsets = no;
      if (cg->field_class_names && cg->field_names && cg->field_offsets) {
        cg->field_class_names[cg->field_n] = dup_cstr(class_name);
        cg->field_names[cg->field_n] = dup_cstr(field_names[j]);
        cg->field_offsets[cg->field_n] = 8 + j * 8;
        cg->field_n = new_n;
      }
    }
    
    // Emit type info structure
    emit(cg, "");
    emit(cg, "  .type %s_typeinfo,@object", class_name);
    emit(cg, "  .size %s_typeinfo, %d", class_name, 8 + 8 + 8 + 8 * n_fields); // name_ptr, base_ptr, size, fields...
    emit(cg, "%s_typeinfo:", class_name);
    emit(cg, "  .quad .LC_type_%s_name", class_name); // pointer to type name string
    if (base_name) {
      emit(cg, "  .quad %s_typeinfo", base_name); // pointer to base class typeinfo
    } else {
      emit(cg, "  .quad 0"); // no base class
    }
    emit(cg, "  .quad %d", 8 + 8 * n_fields); // size in bytes (vptr + fields)
    emit(cg, "  .quad %d", n_fields); // number of fields
    
    // Emit field information: offset and name pointer for each field
    for (int j = 0; j < n_fields; j++) {
      emit(cg, "  .quad %d", 8 + j * 8); // field offset
      if (field_names && j < n_fields && field_names[j]) {
        emit(cg, "  .quad .LC_field_%s_%s", class_name, field_names[j]); // pointer to field name string
      } else {
        emit(cg, "  .quad 0"); // no field name
      }
    }
    
    // Emit type name string and field name strings
    emit(cg, "");
    emit(cg, "  .section .rodata");
    emit(cg, ".LC_type_%s_name:", class_name);
    emit(cg, "  .asciz \"%s\"", class_name);
    
    // Emit field name strings
    for (int j = 0; j < n_fields; j++) {
      if (field_names && j < n_fields && field_names[j]) {
        emit(cg, ".LC_field_%s_%s:", class_name, field_names[j]);
        emit(cg, "  .asciz \"%s\"", field_names[j]);
      }
    }
    
    free(field_names);
   /* Note: vtable placeholders are emitted later in a single pass
     from cg->required_vtables. Do not emit per-class vtable here to
     avoid duplicate symbol definitions. If this class needs a vtable
     (e.g. an object of this class is instantiated), gen_new will
     register it via cg_add_required_vtable and it will be emitted
     once at the end of codegen. */
  }
  /* Emit placeholder vtables for any classes that were requested during
     codegen (via gen_new) but which did not exist as class definitions in
     the AST. This ensures symbols like List_vtable are defined. */
  if (cg->required_vtables && cg->req_vtables_n > 0) {
    // Build a set of class_names we already emitted (from items loop)
    // We used cg_add_required_vtable above for emitted classes too, so
    // to avoid duplicating, just iterate required_vtables and emit
    // those that were not already emitted by this function. To track
    // emitted ones we can use a temporary array of emitted names built
    // earlier; but since we already ensured required_vtables contains
    // all names, we'll emit placeholders only for those entries that do
    // not currently have a symbol in the file — simple approach: emit
    // vtable for every required_vtables entry but guard by local
    // duplication prevention by tracking what we output here.
    const char **emitted_local = NULL;
    int emitted_local_n = 0, emitted_local_cap = 0;
    for (int i = 0; i < cg->req_vtables_n; i++) {
      const char *vn = cg->required_vtables[i];
      // skip duplicates in this emission pass
      int dup = 0;
      for (int j = 0; j < emitted_local_n; j++) if (!strcmp(emitted_local[j], vn)) { dup = 1; break; }
      if (dup) continue;
      // Emit placeholder vtable
      emit(cg, "");
      emit(cg, "  .section .data.vtables");
      emit(cg, "  .align 8");
      emit(cg, "%s_vtable:", vn);
      emit(cg, "  .quad 0");
      // record in emitted_local
      if (emitted_local_n == emitted_local_cap) {
        int nc = emitted_local_cap ? emitted_local_cap * 2 : 8;
        const char **nv = (const char **)realloc((void*)emitted_local, (size_t)nc * sizeof(char*));
        if (!nv) break;
        emitted_local = nv; emitted_local_cap = nc;
      }
      emitted_local[emitted_local_n++] = vn;
    }
    free((void*)emitted_local);
  }
}

// публичная функция: сгенерить asm из AST root
int codegen_s390x_from_ast(FILE *out, const ASTNode *root) {
  if (!out || !root || !root->label) return 0;

  CG cg;
  cg_init(&cg, out);

  // соберём string literals заранее (можно и лениво, но так проще)
  collect_literals(&cg, root);

  // ожидаем: source -> items -> (funcDef|funcDecl)*
  if (strcmp(root->label, "source") != 0 || root->numChildren < 1) {
    fprintf(stderr, "codegen: expected root 'source'\n");
    cg_free(&cg);
    return 0;
  }

  const ASTNode *items = root->children[0];
  if (!items || !items->label || strcmp(items->label, "items") != 0) {
    fprintf(stderr, "codegen: expected 'items'\n");
    cg_free(&cg);
    return 0;
  }

  // First pass: collect defined function names
  typedef struct {
    const char **names;
    int *arity;    /* parallel array with parameter counts */
    int n, cap;
  } FuncSet;
  
  FuncSet defined;
  memset(&defined, 0, sizeof(defined));
  
  for (int i = 0; i < items->numChildren; i++) {
    const ASTNode *fn = items->children[i];
    if (!fn || !fn->label) continue;

    if (!strcmp(fn->label, "funcDef")) {
      const char *nm = get_func_name(fn);
      /* compute arity (number of args) from signature if available */
      int ar = 0;
      if (fn->numChildren > 0) {
        const ASTNode *sig = fn->children[0];
        if (sig && sig->label && strcmp(sig->label, "signature") == 0 && sig->numChildren >= 3) {
          const ASTNode *args = sig->children[2];
          if (args && args->label && strcmp(args->label, "args") == 0 && args->numChildren > 0) {
            const ASTNode *arglist = args->children[0];
            if (arglist && arglist->label && strcmp(arglist->label, "arglist") == 0) ar = arglist->numChildren;
          }
        }
      }
      // Add to set (expand names and arity arrays together)
      if (defined.n == defined.cap) {
        int nc = defined.cap ? (defined.cap * 2) : 16;
        const char **nv = (const char **)realloc(defined.names, (size_t)nc * sizeof(const char*));
        int *na = (int *)realloc(defined.arity, (size_t)nc * sizeof(int));
        if (nv) defined.names = nv;
        if (na) defined.arity = na;
        if (defined.names && defined.arity) defined.cap = nc;
      }
      if (defined.names && defined.arity && defined.n < defined.cap) {
        defined.names[defined.n] = nm;
        defined.arity[defined.n] = ar;
        defined.n++;
      }
    }
  }

  // Copy set into CG so codegen routines can query which top-level
  // functions are defined (used to avoid mangling calls to actual
  // global functions).
  if (defined.n > 0) {
    // Transfer ownership of the names array to cg so other routines
    // can consult which top-level functions are defined.
    cg.defined_names = defined.names;
    cg.defined_arity = defined.arity;
    cg.defined_n = defined.n;
    defined.names = NULL;
    defined.arity = NULL;
    defined.n = 0;
    defined.cap = 0;
  }

  // Convert class members (methods) into top-level mangled functions
  // so they are emitted by the existing function generator. The
  // convention used here is: <ClassName>__<methodName>
  for (int i = 0; i < items->numChildren; i++) {
    const ASTNode *item = items->children[i];
    if (!item || !item->label) continue;
    if (strcmp(item->label, "class") != 0) continue;

    const char *class_name = extract_class_name_from_ast(item);
    if (!class_name) continue;

    // Find members node
    const ASTNode *members = NULL;
    for (int j = 0; j < item->numChildren; j++) {
      const ASTNode *c = item->children[j];
      if (c && c->label && strcmp(c->label, "members") == 0) { members = c; break; }
    }
    if (!members) continue;

    /* populate cg field map entries for this class early so generators
       can consult field offsets while emitting functions */
    const char **cls_field_names = NULL;
    int cls_n_fields = 0;
    collect_fields_from_class(item, &cls_field_names, &cls_n_fields);
    for (int j = 0; j < cls_n_fields; j++) {
      int new_n = cg.field_n + 1;
      const char **nc = (const char **)realloc((void*)cg.field_class_names, (size_t)new_n * sizeof(char*));
      if (nc) cg.field_class_names = nc;
      const char **nn = (const char **)realloc((void*)cg.field_names, (size_t)new_n * sizeof(char*));
      if (nn) cg.field_names = nn;
      int *no = (int *)realloc(cg.field_offsets, (size_t)new_n * sizeof(int));
      if (no) cg.field_offsets = no;
      if (cg.field_class_names && cg.field_names && cg.field_offsets) {
        cg.field_class_names[cg.field_n] = dup_cstr(class_name);
        cg.field_names[cg.field_n] = dup_cstr(cls_field_names[j]);
        cg.field_offsets[cg.field_n] = 8 + j * 8;
        cg.field_n = new_n;
      }
    }
    free((void*)cls_field_names);

    // For each member, if it contains an inner funcDef, create a top-level
    // funcDef with a mangled name and the same body. We prepend an implicit
    // 'this' parameter of type <ClassName> so the generated function will
    // receive the object pointer in r2 as gen_method_call expects.
    for (int m = 0; m < members->numChildren; m++) {
      const ASTNode *member = members->children[m];
      if (!member || !member->label) continue;

      // Look for a nested funcDef inside this member
      for (int k = 0; k < member->numChildren; k++) {
        const ASTNode *child = member->children[k];
        if (!child || !child->label) continue;
        if (strcmp(child->label, "funcDef") != 0) continue;

        const ASTNode *orig_fn = child;
        const ASTNode *orig_sig = (orig_fn->numChildren > 0) ? orig_fn->children[0] : NULL;
        if (!orig_sig || !orig_sig->label || strcmp(orig_sig->label, "signature") != 0) continue;

        // Extract method name from signature (if present)
        const char *method_name = "unknown";
        if (orig_sig->numChildren >= 2) {
          const ASTNode *idn = orig_sig->children[1];
          if (idn && is_token_kind(idn, "id")) method_name = after_colon(idn->label);
        }

        // Build mangled name: Class__method
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "%s__%s", class_name, method_name);

        // Build new signature: [0]=returnType (reuse), [1]=mangled id, [2]=args
        ASTNode *new_sig = ast_create_node("signature");
        if (orig_sig->numChildren >= 1) {
          ast_add_child(new_sig, (ASTNode*)orig_sig->children[0]); // return type (reuse)
        }
        ASTNode *idnode = ast_create_leaf_token("id", mangled);
        ast_add_child(new_sig, idnode);

        // Build args: create arglist with implicit 'this' arg first
        ASTNode *args_node = ast_create_node("args");
        ASTNode *arglist = ast_create_node("arglist");

        // implicit this: arg -> [ typeRef(class_name), id(this) ]
        ASTNode *this_arg = ast_create_node("arg");
        ASTNode *this_type = ast_create_leaf_token("typeRef", (char*)class_name);
        ASTNode *this_id = ast_create_leaf_token("id", "this");
        ast_add_child(this_arg, this_type);
        ast_add_child(this_arg, this_id);
        ast_add_child(arglist, this_arg);

        // Append original args (if any)
        if (orig_sig->numChildren >= 3) {
          const ASTNode *old_args = orig_sig->children[2];
          if (old_args && old_args->label && strcmp(old_args->label, "args") == 0 && old_args->numChildren > 0) {
            const ASTNode *old_arglist = old_args->children[0];
            if (old_arglist && old_arglist->label && strcmp(old_arglist->label, "arglist") == 0) {
              for (int a = 0; a < old_arglist->numChildren; a++) {
                ast_add_child(arglist, (ASTNode*)old_arglist->children[a]); // reuse
              }
            }
          }
        }

        ast_add_child(args_node, arglist);
        ast_add_child(new_sig, args_node);

        // Create new top-level funcDef and attach signature + body
        ASTNode *new_fn = ast_create_node("funcDef");
        ast_add_child(new_fn, new_sig);
        if (orig_fn->numChildren >= 2) ast_add_child(new_fn, (ASTNode*)orig_fn->children[1]); // reuse body

        // Append to top-level items so the normal function emitter will generate it
        ast_add_child((ASTNode*)items, new_fn);
        /* Register the newly created mangled function name in cg->defined_names
           so subsequent call-site resolution can find it. */
        {
          const char *nm_new = get_func_name(new_fn); /* allocated */
          int ar_new = 0;
          /* compute arity from new_sig (args list) */
          if (new_sig && new_sig->numChildren >= 3) {
            const ASTNode *argsn = new_sig->children[2];
            if (argsn && argsn->label && strcmp(argsn->label, "args") == 0 && argsn->numChildren > 0) {
              const ASTNode *argl = argsn->children[0];
              if (argl && argl->label && strcmp(argl->label, "arglist") == 0) ar_new = argl->numChildren;
            }
          }
          int new_count = cg.defined_n + 1;
          const char **nn = (const char **)realloc((void*)cg.defined_names, (size_t)new_count * sizeof(char*));
          int *na = (int *)realloc(cg.defined_arity, (size_t)new_count * sizeof(int));
          if (nn) cg.defined_names = nn;
          if (na) cg.defined_arity = na;
          if (cg.defined_names && cg.defined_arity) {
            cg.defined_names[cg.defined_n] = nm_new;
            cg.defined_arity[cg.defined_n] = ar_new;
            cg.defined_n = new_count;
          } else {
            free((void*)nm_new);
          }
        }
      }
    }
  }
  
  // Second pass: generate code (deduplicate functions with identical mangled names)
  char **emitted = NULL;
  int emitted_n = 0;

  for (int i = 0; i < items->numChildren; i++) {
    const ASTNode *fn = items->children[i];
    if (!fn || !fn->label) continue;

    if (!strcmp(fn->label, "funcDef")) {
      char *nm = (char*)get_func_name(fn); // allocated
      int dup = 0;
      for (int k = 0; k < emitted_n; k++) {
        if (emitted[k] && !strcmp(emitted[k], nm)) { dup = 1; break; }
      }
      if (dup) {
        emit(&cg, "  # duplicate function '%s' skipped", nm);
        free(nm);
        continue;
      }
      // record and emit
      emitted = (char**)realloc(emitted, (size_t)(emitted_n + 1) * sizeof(char*));
      if (emitted) emitted[emitted_n++] = nm;
      gen_function_with_name(&cg, fn, nm);

    } else if (!strcmp(fn->label, "funcDecl")) {
      const char *nm = get_func_name(fn);
      // Check if function is defined (by earlier collected defined.names)
      int found = 0;
      for (int j = 0; j < defined.n; j++) {
        if (defined.names[j] && !strcmp(defined.names[j], nm)) {
          found = 1;
          break;
        }
      }
      /* Determine base name (unmangled) for standard-library checks.
         Mangled names look like "name__Type..."; we should treat the
         prefix before "__" as the base when deciding if this is a
         standard library function, and emit an extern for the base
         symbol instead of generating a stub for the mangled name. */
      const char *base = nm;
      char basebuf[256];
      char *sep = strstr(nm, "__");
      if (sep) {
        size_t len = (size_t)(sep - nm);
        if (len >= sizeof(basebuf)) len = sizeof(basebuf) - 1;
        memcpy(basebuf, nm, len);
        basebuf[len] = '\0';
        base = basebuf;
      }

      // Generate stub if not defined and not a standard library function
      if (!found && !is_standard_library_func(base)) {
        gen_function_stub(&cg, fn);
      } else if (!found && is_standard_library_func(base)) {
        // Keep as extern for standard library functions (use base name)
        emit(&cg, "  .extern %s", base);
      }
      free((void*)nm);
    }
  }

  // free emitted names
  for (int i = 0; i < emitted_n; i++) free(emitted[i]);
  free(emitted);
  
  // defined.names memory has been moved into cg.defined_names above; do not free here

  emit(&cg, "");
  emit(&cg, "  # External symbols for standard library");
  emit(&cg, "  .extern stdout");
  emit(&cg, "  .extern fflush");

  emit_type_info(&cg, root);
  emit_rodata(&cg);

  cg_free(&cg);
  return 1;
}
