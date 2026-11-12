#include "platform/procfs.h"
#include "platform/linux.h"
#include "util/parse.h"
#include "util/string.h"

usize procfs_pid_path(char *out, usize cap, u32 pid, const char *leaf) {
    char num[20];
    usize nlen = str_from_u64(num, pid);
    usize llen = str_len(leaf);
    usize total = 6 + nlen + 1 + llen;  // "/proc/" + pid + "/" + leaf
    if (total + 1 > cap) return 0;

    usize n = 0;
    mem_copy(out + n, "/proc/", 6);
    n += 6;
    mem_copy(out + n, num, nlen);
    n += nlen;
    out[n++] = '/';
    mem_copy(out + n, leaf, llen);
    n += llen;
    out[n] = '\0';
    return n;
}

bool procfs_list_pids(struct vector *pids) {
    vec_clear(pids);

    i32 fd = sys_openat(AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0) return false;

    // u64 storage so dirent records are suitably aligned.
    u64 storage[1024];
    char *buf = (char *)storage;
    bool ok = true;

    for (;;) {
        isize n = sys_getdents64(fd, buf, sizeof(storage));
        if (n == -EINTR) continue;
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;

        for (isize pos = 0; pos < n;) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + pos);
            pos += d->d_reclen;

            u64 pid;
            if (d->d_type != DT_DIR) continue;
            if (!parse_u64(d->d_name, &pid) || pid == 0 || pid > U32_MAX) continue;

            u32 *slot = vec_push(pids);
            if (!slot) {
                ok = false;
                break;
            }
            *slot = (u32)pid;
        }
        if (!ok) break;
    }

    sys_close(fd);
    return ok;
}
