/* bbcpu: recompiled game functions (docs/RECOMPILATION.md). BB_RECOMP_LIB names a library of C
 * written by tools/recomp/bbrecomp.c from the player's own eboot (src/recomp/rc.h): its functions
 * replace the translated ones. A call of one reaches the dispatcher (the JIT leaves its first
 * instruction untranslated) and bbcpu_step runs it. BB_RECOMP_OFF=OFFSET,... leaves chosen ones to
 * the translator (to find a wrong one by bisection). */
#include <setjmp.h>
#include <string.h>
#include "trace.h"
#include "../recomp/rc.h"
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

enum { BUCKETS = 16384 };
typedef struct Recomp { uint64_t address; RcFn fn; struct Recomp *next; } Recomp;
static Recomp *buckets[BUCKETS];
int bbcpu_recomp_count;
uint64_t bbcpu_recomp_calls;

static uint32_t bucket(uint64_t address) { return (uint32_t)((address * 0x9e3779b97f4a7c15ull) >> 50); }

RcFn bbcpu_recomp_at(uint64_t address) {
    for (const Recomp *r = buckets[bucket(address)]; r; r = r->next)
        if (r->address == address) return r->fn;
    return NULL;
}

static void add(uint64_t address, RcFn fn) {
    Recomp *r = malloc(sizeof(*r));
    if (!r) abort();
    *r = (Recomp){address, fn, buckets[bucket(address)]};
    buckets[bucket(address)] = r;
    ++bbcpu_recomp_count;
}

int bbcpu_is_tcb_load(const BbInsn *in) {
    if (in->mnemonic != ZYDIS_MNEMONIC_MOV || in->count != 2) return 0;
    const BbOp *d = &in->op[0], *s = &in->op[1];
    return d->type == OP_REG && d->kind == RK_GPR && d->size == 8 && s->type == OP_MEM && s->size == 8 &&
           s->base == 0xff && s->index == 0xff && ((s->segment == 1 && s->disp == 0) || s->segment == 2);
}

uint64_t bbcpu_recomp_hash(uint64_t start, uint64_t size) {
    uint64_t h = 0xcbf29ce484222325ull;
#define MIX(byte) (h = (h ^ (uint8_t)(byte)) * 0x100000001b3ull)
    for (uint64_t at = start; at < start + size;) {
        const BbBlock *block = bbcpu_block(at);
        if (!block->count) break;
        for (uint32_t i = 0; i < block->count && at < start + size; ++i) {
            const BbInsn *in = &block->insn[i];
            if (bbcpu_is_tcb_load(in)) {
                MIX(0xf5);
                MIX(in->op[0].reg);
            } else {
                for (int b = 0; b < in->length; ++b) MIX(((const uint8_t *)at)[b]);
            }
            at += in->length;
        }
    }
#undef MIX
    return h;
}

/* BB_RECOMP_THREADS / BB_RECOMP_NOT_THREADS=NAME,... (diagnostics): recompiled functions only on
 * the threads whose name contains one of the first, and none of the second (host thread names). */
static int name_matches(const char *list, const char *name) {
    if (!list) return 0;
    char copy[512];
    snprintf(copy, sizeof(copy), "%s", list);
    for (char *part = strtok(copy, ","); part; part = strtok(NULL, ","))
        if (*part && strstr(name, part)) return 1;
    return 0;
}
int bbcpu_recomp_thread_ok(void) {
    static _Thread_local int ok = -1;
    if (ok < 0) {
        const char *only = getenv("BB_RECOMP_THREADS"), *not = getenv("BB_RECOMP_NOT_THREADS");
        if (!only && !not) {
            ok = 1;
        } else {
            char name[64] = "";
            pthread_getname_np(pthread_self(), name, sizeof(name));
            ok = (!only || name_matches(only, name)) && !name_matches(not, name);
        }
    }
    return ok;
}

/* ---- the runtime the generated code calls (RcApi) ---- */

/* Calls the runtime makes in the translator get a return address of their own, never guest code
 * (FRAME_BASE + 16 * depth): bbcpu_run stops there. A longjmp (or an exception) in the guest can
 * leave several such frames at once: the guest then returns to an outer frame's address, and
 * return_hook goes back to that frame on the host too (the C frames between are left: recompiled
 * code holds nothing). One address for all of them returned into the innermost frame instead. */
#define FRAME_BASE UINT64_C(0x0000700100000000)
typedef struct { jmp_buf jump; } Frame; /* _setjmp: no signal mask */
static _Thread_local Frame *frames;
static _Thread_local uint32_t frame_depth, frame_capacity;

/* Statistics for the report: calls into the translator, longjmps to an outer frame, deepest. */
static uint64_t translated_runs, frame_jumps, deepest_frame, recomp_image_base, host_calls;
/* BB_RECOMP_TARGETS=1: the targets of calls into the translator, counted (sampled 1 in 64). */
enum { TARGET_SLOTS = 4096 };
static struct { uint64_t rip, count; } targets[TARGET_SLOTS];
static int count_targets(void) {
    static int value = -1;
    if (value < 0) value = getenv("BB_RECOMP_TARGETS") && getenv("BB_RECOMP_TARGETS")[0] == '1';
    return value;
}
static void note_target(uint64_t rip) {
    static _Thread_local uint32_t tick;
    if ((++tick & 63) || !count_targets()) return;
    for (uint32_t i = 0, h = (uint32_t)((rip * 0x9e3779b97f4a7c15ull) >> 52); i < 16; ++i) {
        uint64_t *slot = &targets[(h + i) % TARGET_SLOTS].rip, expected = 0;
        if (__atomic_load_n(slot, __ATOMIC_RELAXED) == rip ||
            __atomic_compare_exchange_n(slot, &expected, rip, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            __atomic_add_fetch(&targets[(h + i) % TARGET_SLOTS].count, 1, __ATOMIC_RELAXED);
            return;
        }
    }
}

static void return_hook(BbCpu *cpu) {
    const uint64_t rip = cpu->rip;
    if (rip < FRAME_BASE || rip >= FRAME_BASE + (uint64_t)frame_depth * 16 || (rip & 15)) return;
    __atomic_add_fetch(&frame_jumps, 1, __ATOMIC_RELAXED);
    _longjmp(frames[(rip - FRAME_BASE) / 16].jump, 1);
}

/* The translator from cpu->rip until the guest returns through `slot` (the return address the
 * caller left there, replaced by this frame's). */
static void run_translated(BbCpu *cpu, uint64_t slot) {
    if (frame_depth == frame_capacity) {
        frame_capacity = frame_capacity ? frame_capacity * 2 : 64;
        frames = realloc(frames, frame_capacity * sizeof(*frames));
        if (!frames) abort();
    }
    const volatile uint32_t depth = frame_depth++;
    __atomic_add_fetch(&translated_runs, 1, __ATOMIC_RELAXED);
    note_target(cpu->rip);
    if (depth + 1 > __atomic_load_n(&deepest_frame, __ATOMIC_RELAXED)) __atomic_store_n(&deepest_frame, depth + 1, __ATOMIC_RELAXED);
    const uint64_t stop = FRAME_BASE + (uint64_t)depth * 16;
    bb_store(slot, 8, stop);
    if (_setjmp(frames[depth].jump) == 0) bbcpu_run(cpu, stop);
    frame_depth = depth;
}

static uint64_t rc_step(BbCpu *cpu) {
    const BbBlock *block = bbcpu_block(cpu->rip);
    return bbcpu_step_insn(cpu, &block->insn[0]);
}

/* BB_RECOMP_NODIRECT=1 (diagnostics): calls and tail calls between recompiled functions go
 * through the translator's dispatcher instead of being made directly. */
static int no_direct(void) {
    static int value = -1;
    if (value < 0) {
        const char *env = getenv("BB_RECOMP_NODIRECT");
        value = env && env[0] == '1';
    }
    return value;
}

/* An import stub (jmp [rip+slot]) whose slot holds a host function (the runtime's libraries):
 * that function at once, with the return address on top of the guest stack, without the
 * translator (a _setjmp and its dispatch loop). 1: done, cpu->rip is where it returned to. */
static int call_import(BbCpu *cpu, uint64_t target) {
    if (!bbcpu_is_guest_code(target)) return 0;
    const uint8_t *code = (const uint8_t *)(uintptr_t)target;
    if (code[0] != 0xff || code[1] != 0x25) return 0;
    int32_t disp;
    memcpy(&disp, code + 2, 4);
    const uint64_t host = bb_load(target + 6 + (uint64_t)(int64_t)disp, 8);
    if (!host || bbcpu_is_guest_code(host)) return 0;
    __atomic_add_fetch(&host_calls, 1, __ATOMIC_RELAXED);
    cpu->rip = host;
    bbcpu_call_host_at_rip(cpu); /* pops the return address into rip */
    return 1;
}

static void rc_call(BbCpu *cpu, uint64_t target, uint64_t next) {
    const RcFn fn = no_direct() || !bbcpu_recomp_thread_ok() ? NULL : bbcpu_recomp_at(target);
    cpu->r[RSP] -= 8;
    if (fn) {
        bbcpu_recomp_counted();
        bb_store(cpu->r[RSP], 8, next);
        cpu->rip = target;
        fn(cpu);
        return;
    }
    bb_store(cpu->r[RSP], 8, next);
    if (call_import(cpu, target)) return;
    /* The translator (and natives, imports: bbcpu_run calls them). */
    cpu->rip = target;
    run_translated(cpu, cpu->r[RSP]);
    cpu->rip = next;
}

static void rc_tail(BbCpu *cpu) {
    const RcFn fn = no_direct() || !bbcpu_recomp_thread_ok() ? NULL : bbcpu_recomp_at(cpu->rip);
    if (fn) {
        bbcpu_recomp_counted();
        fn(cpu);
        return;
    }
    if (call_import(cpu, cpu->rip)) return; /* a tail call of an import: it returns for us */
    const uint64_t back = bb_load(cpu->r[RSP], 8);
    run_translated(cpu, cpu->r[RSP]);
    cpu->rip = back;
}

static void rc_bail(BbCpu *cpu, uint64_t entry_rsp) {
    const uint64_t back = bb_load(entry_rsp, 8);
    run_translated(cpu, entry_rsp);
    cpu->rip = back;
}

/* Calls of recompiled functions per second, every 10 s while there are any. */
static void *report(void *unused) {
    (void)unused;
    for (uint64_t last = 0;;) {
        sleep(10);
        const uint64_t now = __atomic_load_n(&bbcpu_recomp_calls, __ATOMIC_RELAXED);
        static uint64_t last_runs;
        const uint64_t runs = __atomic_load_n(&translated_runs, __ATOMIC_RELAXED);
        if (now != last)
            printf("Recompiled: %.0f calls/s; %.0f/s into the translator, %.0f/s straight to host imports, "
                   "%llu longjmps to outer frames, frames up to %llu deep\n", (double)(now - last) / 10.0,
                   (double)(runs - last_runs) / 10.0, (double)(__atomic_exchange_n(&host_calls, 0, __ATOMIC_RELAXED)) / 10.0,
                   (unsigned long long)__atomic_load_n(&frame_jumps, __ATOMIC_RELAXED),
                   (unsigned long long)__atomic_load_n(&deepest_frame, __ATOMIC_RELAXED));
        last = now;
        last_runs = runs;
        if (count_targets()) {
            uint64_t best[10][2] = {{0}};
            for (int i = 0; i < TARGET_SLOTS; ++i) {
                const uint64_t c = __atomic_exchange_n(&targets[i].count, 0, __ATOMIC_RELAXED);
                for (int k = 0; k < 10; ++k)
                    if (c > best[k][1]) { memmove(best[k + 1], best[k], (size_t)(9 - k) * sizeof(best[0])); best[k][0] = targets[i].rip; best[k][1] = c; break; }
            }
            printf("Recompiled: calls into the translator by target (x64 samples):");
            for (int k = 0; k < 10 && best[k][1]; ++k)
                printf(" %#llx:%llu", (unsigned long long)(best[k][0] - recomp_image_base), (unsigned long long)best[k][1] * 64);
            printf("\n");
        }
    }
    return NULL;
}

/* A set of image offsets from an environment variable: "OFFSET,OFFSET..." or "@FILE" (offsets
 * separated by commas, spaces or lines). */
typedef struct { uint64_t *v; size_t n; int given; } OffsetSet;
static int by_value(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
static OffsetSet offset_set(const char *name) {
    OffsetSet set = {0};
    const char *value = getenv(name);
    if (!value || !*value) return set;
    set.given = 1;
    char *text = NULL;
    if (value[0] == '@') {
        FILE *f = fopen(value + 1, "r");
        if (!f) { fprintf(stderr, "Recompiled: %s: cannot read %s\n", name, value + 1); return set; }
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = calloc((size_t)size + 1, 1);
        if (text && fread(text, 1, (size_t)size, f) != (size_t)size) text[0] = 0;
        fclose(f);
    } else {
        text = strdup(value);
    }
    size_t capacity = 0;
    for (char *p = text; p && *p;) {
        char *end;
        const uint64_t v = strtoull(p, &end, 0);
        if (end == p) { ++p; continue; }
        if (set.n == capacity) { capacity = capacity ? capacity * 2 : 256; set.v = realloc(set.v, capacity * sizeof(*set.v)); }
        set.v[set.n++] = v;
        p = end;
    }
    free(text);
    if (set.n) qsort(set.v, set.n, sizeof(*set.v), by_value);
    return set;
}
static int in_set(const OffsetSet *set, uint64_t offset) {
    return set->n && bsearch(&offset, set->v, set->n, sizeof(*set->v), by_value) != NULL;
}

static size_t recomp_used;

/* Code the runtime rewrites while the game runs (live switches): the functions that hold it stay
 * with the translator, whose blocks bbcpu_invalidate drops. */
enum { MAX_EXCLUDED = 64 };
static struct { uint64_t start, end; } excluded[MAX_EXCLUDED];
static int excluded_count;
void bbcpu_recomp_exclude(uint64_t address, size_t size) {
    if (excluded_count == MAX_EXCLUDED) {
        fputs("Recompiled: too many excluded ranges\n", stderr);
        abort();
    }
    excluded[excluded_count].start = address;
    excluded[excluded_count].end = address + size;
    ++excluded_count;
}
static int is_excluded(uint64_t start, uint64_t size) {
    for (int i = 0; i < excluded_count; ++i)
        if (excluded[i].start < start + size && start < excluded[i].end) return 1;
    return 0;
}

size_t bbcpu_recomp_functions(void) {
    return recomp_used;
}

void bbcpu_recomp_load(uint64_t image_base) {
    recomp_image_base = image_base;
    const char *path = getenv("BB_RECOMP_LIB");
    if (!path || !*path) return;
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    const RcInit init = library ? (RcInit)dlsym(library, BB_RECOMP_INIT) : NULL;
    if (!init) {
        fprintf(stderr, "Recompiled: %s: %s\n", path, dlerror());
        return;
    }
    bbcpu_return_hook = return_hook;
    static RcApi api;
    api = (RcApi){.version = BB_RECOMP_API_VERSION, .image_base = image_base, .step = rc_step,
                  .call = rc_call, .tail = rc_tail, .bail = rc_bail};
    size_t count = 0;
    const RcFunction *functions = init(&api, &count);
    /* BB_RECOMP_OFF: these not; BB_RECOMP_ONLY: only these (bisecting). */
    const OffsetSet off = offset_set("BB_RECOMP_OFF"), only = offset_set("BB_RECOMP_ONLY");
    size_t used = 0, changed = 0, kept = 0;
    for (size_t i = 0; i < count; ++i) {
        if (in_set(&off, functions[i].offset) || (only.given && !in_set(&only, functions[i].offset))) continue;
        if (is_excluded(image_base + functions[i].offset, functions[i].size)) {
            ++kept;
            continue;
        }
        if (bbcpu_recomp_hash(image_base + functions[i].offset, functions[i].size) != functions[i].hash) {
            ++changed; /* patched since (game patches, hooks): the translator runs it */
            continue;
        }
        add(image_base + functions[i].offset, functions[i].fn);
        if (functions[i].direct && !no_direct()) *functions[i].direct = functions[i].fn;
        ++used;
    }
    recomp_used = used;
    printf("Recompiled: %zu of %zu game functions replaced, %zu changed by patches, %zu rewritten "
           "live (%s)\n", used, count, changed, kept, path);
    pthread_t thread;
    if (used && pthread_create(&thread, NULL, report, NULL) == 0) pthread_detach(thread);
}
