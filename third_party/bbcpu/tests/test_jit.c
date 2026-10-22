/* tests/test_jit.c (arm64): maps the x86-64 executable built from tests/jit_guest.c at its link
 * address, runs its test functions through bbcpu (interpreter or JIT: BB_JIT) and compares the
 * results with the ones it printed when run natively. tools/cpu/test_jit.sh drives it. */
#include "../src/cpu/bbcpu.h"
#include <inttypes.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

uint64_t bb_image_base; /* platform.c in the game: fault reports */

void *runtime_low_map(size_t size, int prot) {
    void *p = mmap(NULL, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_jit GUEST_EXE EXPECTED\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)size);
    if (fread(file, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);
    const struct mach_header_64 *mh = (const void *)file;
    if (mh->magic != MH_MAGIC_64) { fprintf(stderr, "not a 64-bit Mach-O\n"); return 2; }
    const uint8_t *lc = file + sizeof(*mh);
    const struct symtab_command *symtab = NULL;
    /* One mapping over all segments: they are 4 KiB-aligned, arm64 pages are 16 KiB. */
    uint64_t lo = UINT64_MAX, hi = 0;
    for (uint32_t i = 0, off = sizeof(*mh); i < mh->ncmds; ++i) {
        const struct load_command *cmd = (const void *)(file + off);
        if (cmd->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const void *)cmd;
            if (seg->vmsize && strcmp(seg->segname, "__PAGEZERO")) {
                if (seg->vmaddr < lo) lo = seg->vmaddr;
                if (seg->vmaddr + seg->vmsize > hi) hi = seg->vmaddr + seg->vmsize;
            }
        }
        off += cmd->cmdsize;
    }
    lo &= ~UINT64_C(0x3fff);
    hi = (hi + 0x3fff) & ~UINT64_C(0x3fff);
    if (mmap((void *)lo, hi - lo, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != (void *)lo) {
        fprintf(stderr, "cannot map %#llx-%#llx\n", (unsigned long long)lo, (unsigned long long)hi);
        return 2;
    }
    for (uint32_t i = 0; i < mh->ncmds; ++i) {
        const struct load_command *cmd = (const void *)lc;
        if (cmd->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const void *)lc;
            if (seg->vmsize && strcmp(seg->segname, "__PAGEZERO") && strcmp(seg->segname, "__LINKEDIT")) {
                memcpy((void *)seg->vmaddr, file + seg->fileoff, seg->filesize);
                if (!strcmp(seg->segname, "__TEXT")) bbcpu_add_guest_code(seg->vmaddr, seg->vmsize);
            }
        } else if (cmd->cmd == LC_SYMTAB) {
            symtab = (const void *)lc;
        }
        lc += cmd->cmdsize;
    }
    if (!symtab) { fprintf(stderr, "no symbols\n"); return 2; }
    const struct nlist_64 *syms = (const void *)(file + symtab->symoff);
    const char *strings = (const char *)(file + symtab->stroff);
    FILE *expected = fopen(argv[2], "r");
    if (!expected) { perror(argv[2]); return 2; }
    char name[64];
    unsigned long long a, b, c, want;
    char last[64] = "";
    uint64_t fn = 0;
    int failures = 0, cases = 0, bad_in_test = 0;
    while (fscanf(expected, "%63s %llx %llx %llx %llx", name, &a, &b, &c, &want) == 5) {
        if (strcmp(name, last)) {
            if (last[0]) printf("%-8s %s\n", last, bad_in_test ? "FAILED" : "ok");
            snprintf(last, sizeof(last), "%s", name);
            bad_in_test = 0;
            char symbol[80];
            snprintf(symbol, sizeof(symbol), "_t_%s", name);
            fn = 0;
            for (uint32_t i = 0; i < symtab->nsyms; ++i)
                if (!strcmp(strings + syms[i].n_un.n_strx, symbol)) fn = syms[i].n_value;
            if (!fn) { fprintf(stderr, "no %s\n", symbol); return 2; }
        }
        const uint64_t args[3] = {a, b, c};
        const uint64_t got = bbcpu_call((uintptr_t)fn, 3, args);
        ++cases;
        if (got != want && bad_in_test < 3) {
            printf("FAIL %s(%llx, %llx, %llx): expected %llx, got %" PRIx64 "\n", name, a, b, c, want, got);
        }
        if (got != want) { ++bad_in_test; ++failures; }
    }
    if (last[0]) printf("%-8s %s\n", last, bad_in_test ? "FAILED" : "ok");
    bbcpu_report();
    printf("%d of %d cases failed\n", failures, cases);
    return failures != 0;
}
