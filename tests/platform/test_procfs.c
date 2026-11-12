#include "test.h"
#include "platform/procfs.h"
#include "util/parse.h"

// Our own pid, from /proc/self/stat (there is no getpid wrapper, and
// we'd like to test the parser against something we know anyway).
static u32 self_pid(struct buffer *scratch) {
    if (!file_read_all("/proc/self/stat", scratch)) return 0;
    struct cursor c;
    cur_init(&c, (const char *)scratch->data, scratch->len);
    u64 pid = 0;
    cur_u64(&c, &pid);
    return (u32)pid;
}

TEST(test_pid_path) {
    char buf[32];
    CHECK(procfs_pid_path(buf, sizeof(buf), 42, "stat") == 13);
    CHECK_STR(buf, "/proc/42/stat");
    CHECK(procfs_pid_path(buf, 8, 42, "stat") == 0);
}

TEST(test_read_self) {
    struct buffer scratch;
    buf_init(&scratch);
    u32 pid = self_pid(&scratch);
    CHECK(pid > 0);

    struct procfs_process p;
    CHECK(procfs_read_process(pid, &p, &scratch));
    CHECK(p.pid == pid);
    CHECK(p.state == 'R');               // we are, in fact, running
    CHECK_STR(p.comm, "test_procfs");
    CHECK(p.ppid > 0);
    CHECK(p.threads == 1);
    CHECK(p.rss_pages > 0);
    CHECK(p.vsize > 0);

    char cmd[256];
    CHECK(procfs_read_cmdline(pid, cmd, sizeof(cmd), &scratch));
    CHECK(str_len(cmd) > 0);

    CHECK(!procfs_read_process(0xfffffff0u, &p, &scratch));  // no such pid
    buf_free(&scratch);
}

TEST(test_list_contains_self) {
    struct buffer scratch;
    buf_init(&scratch);
    u32 pid = self_pid(&scratch);

    struct vector pids;
    vec_init(&pids, sizeof(u32));
    CHECK(procfs_list_pids(&pids));
    CHECK(pids.len > 1);

    bool found_self = false, found_init = false;
    for (usize i = 0; i < pids.len; i++) {
        u32 p = *(u32 *)vec_at(&pids, i);
        if (p == pid) found_self = true;
        if (p == 1) found_init = true;
    }
    CHECK(found_self);
    CHECK(found_init);
    vec_free(&pids);
    buf_free(&scratch);
}

TEST(test_system_files) {
    struct buffer scratch;
    buf_init(&scratch);

    struct procfs_cpu cpu;
    CHECK(procfs_read_cpu(&cpu, &scratch));
    CHECK(cpu.ncpu >= 1);
    CHECK(cpu.total.user + cpu.total.idle > 0);

    struct procfs_meminfo mem;
    CHECK(procfs_read_meminfo(&mem, &scratch));
    CHECK(mem.total_kb > 0);
    CHECK(mem.free_kb <= mem.total_kb);
    CHECK(!mem.has_available || mem.available_kb <= mem.total_kb);

    struct procfs_loadavg load;
    CHECK(procfs_read_loadavg(&load, &scratch));
    CHECK(load.total >= 1);

    char host[PROCFS_HOSTNAME_MAX];
    CHECK(procfs_read_hostname(host, sizeof(host), &scratch));
    CHECK(str_len(host) > 0 && host[str_len(host) - 1] != '\n');

    u64 up = 0;
    CHECK(procfs_read_uptime(&up, &scratch));
    CHECK(up > 0);

    buf_free(&scratch);
}

TEST_MAIN("platform/procfs",
    T(test_pid_path),
    T(test_read_self),
    T(test_list_contains_self),
    T(test_system_files))
