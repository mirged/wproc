#include "platform/procfs.h"
#include "platform/file.h"
#include "util/parse.h"
#include "util/string.h"

// Lines look like "MemTotal:       16303428 kB".
bool procfs_read_meminfo(struct procfs_meminfo *out, struct buffer *scratch) {
    if (!file_read_all("/proc/meminfo", scratch)) return false;
    mem_set(out, 0, sizeof(*out));

    struct {
        const char *key;
        u64        *dst;
    } fields[] = {
        { "MemTotal:",     &out->total_kb },
        { "MemFree:",      &out->free_kb },
        { "MemAvailable:", &out->available_kb },
        { "Buffers:",      &out->buffers_kb },
        { "Cached:",       &out->cached_kb },
        { "SReclaimable:", &out->sreclaimable_kb },
        { "Shmem:",        &out->shmem_kb },
        { "SwapTotal:",    &out->swap_total_kb },
        { "SwapFree:",     &out->swap_free_kb },
    };

    struct cursor c;
    cur_init(&c, (const char *)scratch->data, scratch->len);
    bool have_total = false;

    while (!cur_eof(&c)) {
        const char *word;
        usize len;
        if (cur_word(&c, &word, &len)) {
            for (usize i = 0; i < ARRAY_LEN(fields); i++) {
                if (!str_eq_n(word, len, fields[i].key)) continue;
                if (cur_u64(&c, fields[i].dst)) {
                    if (fields[i].dst == &out->total_kb) have_total = true;
                    if (fields[i].dst == &out->available_kb) out->has_available = true;
                }
                break;
            }
        }
        cur_skip_line(&c);
    }
    return have_total;
}
