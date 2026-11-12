#include "wproc.h"
#include "platform/linux.h"

// Initial process stack on x86_64 Linux:
//
//   rsp -> argc
//          argv[0] .. argv[argc-1], NULL
//          envp[0] .. NULL
//          auxv pairs (key, value) .. AT_NULL
//
// Without libc nobody parses this for us, and the auxiliary vector is
// where the kernel tells us USER_HZ and the page size.

__attribute__((noreturn, used))
void linux_start(u64 *sp);

__attribute__((noreturn, used))
void linux_start(u64 *sp) {
    struct wproc_startup s = {
        .argc = (i32)sp[0],
        .argv = (char **)(sp + 1),
        .clk_tck = 100,
        .page_size = 4096,
    };
    s.envp = s.argv + s.argc + 1;

    char **e = s.envp;
    while (*e) e++;

    for (u64 *aux = (u64 *)(e + 1); aux[0] != AT_NULL; aux += 2) {
        if (aux[0] == AT_CLKTCK && aux[1]) s.clk_tck = aux[1];
        if (aux[0] == AT_PAGESZ && aux[1]) s.page_size = aux[1];
    }

    sys_exit_group(wproc_main(&s));
}

__attribute__((naked, noreturn))
void _start(void) {
    __asm__ volatile (
        "xor %rbp, %rbp\n"      // mark the outermost frame
        "mov %rsp, %rdi\n"      // pointer to argc
        "and $-16, %rsp\n"      // SysV ABI: 16-byte aligned before call
        "call linux_start\n"
        "hlt\n"
    );
}
