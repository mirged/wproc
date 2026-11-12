#ifndef WPROC_H
#define WPROC_H

#include "util/types.h"

#define WPROC_VERSION "0.1.0"

// What the process entry point hands to the program. Filled in by
// src/platform/linux/start.c from the initial stack (argv, envp, auxv).
struct wproc_startup {
    i32    argc;
    char **argv;
    char **envp;
    u64    clk_tck;    // USER_HZ, units of /proc tick counters
    u64    page_size;
};

// Defined by the program (src/main.c, or a test).
i32 wproc_main(const struct wproc_startup *startup);

#endif // WPROC_H
