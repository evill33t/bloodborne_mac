/* Synthetic faults: exact mapping and register recovery in normal and slow access code. */
#include "../src/cpu/cpu_internal.h"
#include <sys/ucontext.h>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

uint64_t bb_image_base;
static int report_fd;
static int watch_test;
static struct {
    BbGuestRegs regs;
    int translated;
    uint64_t watch_count, watch_value;
    char description[1024];
} report;

void *runtime_low_map(size_t size, int prot) {
    void *p = mmap(NULL, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static void fault(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info;
    report.translated = bbcpu_fault_guest_regs(context, &report.regs);
    bbcpu_describe_fault(context, report.description, sizeof(report.description));
    const ucontext_t *uc = context;
    const BbCpu *cpu = (const BbCpu *)(uintptr_t)uc->uc_mcontext->__ss.__x[28];
    if (cpu->watch) {
        report.watch_count = cpu->watch->count;
        report.watch_value = cpu->watch->records[1].value;
    }
    const ssize_t n = write(report_fd, &report, sizeof(report));
    _exit(n == sizeof(report) ? 17 : 18);
}

static void check(const unsigned char *code, size_t size, size_t fault_offset) {
    unsigned char *guest = mmap(NULL, 16384, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(guest != MAP_FAILED);
    memcpy(guest, code, size);
    /* Distinct synthetic blocks exercise retained metadata after several chunks fill. */
    for (unsigned i = 0; i < 400; ++i) {
        unsigned char *fn = guest + 4096 + i * 16;
        fn[0] = 0xb8; memcpy(fn + 1, &i, 4); fn[5] = 0xc3; /* mov eax,i; ret */
    }
    int fds[2];
    assert(pipe(fds) == 0);
    const pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(fds[0]); report_fd = fds[1];
        struct sigaction sa = {.sa_sigaction = fault, .sa_flags = SA_SIGINFO};
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
        bbcpu_add_guest_code((uintptr_t)guest, 16384);
        uint64_t memory[2] = {0};
        if (watch_test) {
            char setting[80];
            snprintf(setting, sizeof(setting), "%#llx:8", (unsigned long long)(uintptr_t)&memory[1]);
            setenv("BB_JIT_WATCH", setting, 1);
        }
        if (!watch_test) {
            const uint64_t valid[] = {(uintptr_t)memory};
            bbcpu_call((uintptr_t)guest, 1, valid); /* compile the fault site before new chunks */
        }
        for (unsigned i = 0; i < 400; ++i)
            assert(bbcpu_call((uintptr_t)guest + 4096 + i * 16, 0, NULL) == i);
        const uint64_t args[] = {0, (uintptr_t)memory};
        bbcpu_call((uintptr_t)guest, 2, args);
        _exit(19);
    }
    close(fds[1]);
    size_t got = 0;
    while (got < sizeof(report)) {
        const ssize_t n = read(fds[0], (char *)&report + got, sizeof(report) - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fds[0]);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 17);
    assert(got == sizeof(report) && report.translated);
    assert(report.regs.rip == (uintptr_t)guest + fault_offset);
    assert(report.regs.r[0] == 42 && report.regs.r[7] == 0);
    assert(strstr(report.description, "exact guest instruction"));
    if (watch_test) {
        assert(report.watch_count == 128 && report.watch_value == 42);
        assert(report.regs.r[1] == 42);
    }
    munmap(guest, 16384);
}

static void check_null_branch(void) {
    unsigned char *guest = mmap(NULL, 16384, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(guest != MAP_FAILED);
    /* xor eax,eax; jmp rax: an empty indirect-cache entry must not branch to host zero. */
    const unsigned char code[] = {0x31, 0xc0, 0xff, 0xe0};
    memcpy(guest, code, sizeof(code));
    int fds[2];
    assert(pipe(fds) == 0);
    const pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(fds[0]);
        assert(dup2(fds[1], STDERR_FILENO) == STDERR_FILENO);
        close(fds[1]);
        bbcpu_add_guest_code((uintptr_t)guest, 16384);
        bbcpu_call((uintptr_t)guest, 0, NULL);
        _exit(19);
    }
    close(fds[1]);
    char message[256] = {0};
    const ssize_t got = read(fds[0], message, sizeof(message) - 1);
    close(fds[0]);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 30);
    assert(got > 0 && strstr(message, "null guest branch"));
    char site[32];
    snprintf(site, sizeof(site), "%#llx", (unsigned long long)(uintptr_t)(guest + 2));
    assert(strstr(message, site));
    munmap(guest, 16384);
}

int main(void) {
    setenv("BB_JIT_FAULT_MAP", "1", 1);
    setenv("BB_JIT", "1", 1);
    /* mov rax,42; nop; mov rax,[rdi]; ret */
    const unsigned char aligned[] = {0x48,0xc7,0xc0,42,0,0,0,0x90,0x48,0x8b,0x07,0xc3};
    /* mov rax,42; mov rax,[rdi+1]; ret: out-of-line unaligned load. */
    const unsigned char slow[] = {0x48,0xc7,0xc0,42,0,0,0,0x48,0x8b,0x47,1,0xc3};
    check(aligned, sizeof(aligned), 8);
    check(slow, sizeof(slow), 7);
    watch_test = 1;
    /* mov rax,42; mov [rsi+8],rax; mov rcx,[rsi+8]; mov rax,[rdi]; ret */
    unsigned char watch[524] = {0x48,0xc7,0xc0,42,0,0,0};
    const unsigned char pair[] = {0x48,0x89,0x46,8,0x48,0x8b,0x4e,8};
    for (unsigned i = 0; i < 64; ++i) memcpy(watch + 7 + i * 8, pair, sizeof(pair));
    const unsigned char finish[] = {0x48,0x8b,0x07,0xc3};
    memcpy(watch + 519, finish, sizeof(finish));
    check(watch, 523, 519);
    check_null_branch();
    puts("PASS: translated faults and inline watched load/store values");
    return 0;
}
