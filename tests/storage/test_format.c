#include "test.h"
#include "storage/format.h"

TEST(test_little_endian) {
    struct buffer b;
    buf_init(&b);
    wr_u32(&b, 0x01020304u);
    CHECK(b.len == 4);
    CHECK(b.data[0] == 4 && b.data[1] == 3 && b.data[2] == 2 && b.data[3] == 1);

    wr_u16(&b, 0xbeef);
    wr_u64(&b, 0x1122334455667788ul);
    wr_str(&b, "hi");

    struct reader r;
    rd_init(&r, b.data, b.len);
    CHECK(rd_u32(&r) == 0x01020304u);
    CHECK(rd_u16(&r) == 0xbeef);
    CHECK(rd_u64(&r) == 0x1122334455667788ul);
    char s[8];
    rd_str(&r, s, sizeof(s));
    CHECK_STR(s, "hi");
    CHECK(rd_remaining(&r) == 0 && !r.failed);

    CHECK(rd_u8(&r) == 0);   // reading past the end...
    CHECK(r.failed);         // ...is sticky
    buf_free(&b);
}

TEST(test_header) {
    struct buffer b;
    buf_init(&b);
    struct format_header h = { FORMAT_VERSION, 750 }, got;
    format_write_header(&b, &h);
    CHECK(b.len == FORMAT_HEADER_SIZE);

    struct reader r;
    rd_init(&r, b.data, b.len);
    CHECK(format_read_header(&r, &got));
    CHECK(got.version == FORMAT_VERSION && got.interval_ms == 750);

    b.data[0] = 'X';
    rd_init(&r, b.data, b.len);
    CHECK(!format_read_header(&r, &got));
    b.data[0] = 'W';

    b.data[8] = 99;   // future version
    rd_init(&r, b.data, b.len);
    CHECK(!format_read_header(&r, &got));
    b.data[8] = FORMAT_VERSION;

    // A larger header from a future minor revision is skipped over.
    b.data[10] = FORMAT_HEADER_SIZE + 4;
    wr_u32(&b, 0xdeadbeef);
    wr_u32(&b, 0x12345678);
    rd_init(&r, b.data, b.len);
    CHECK(format_read_header(&r, &got));
    CHECK(rd_u32(&r) == 0x12345678);
    buf_free(&b);
}

static void make_snapshot(struct snapshot *s, struct vector *procs) {
    mem_set(s, 0, sizeof(*s));
    vec_init(procs, sizeof(struct process));

    struct process *a = vec_push(procs);
    a->pid = 1;
    a->ppid = 0;
    a->state = 'S';
    str_copy(a->comm, sizeof(a->comm), "init");
    str_copy(a->cmdline, sizeof(a->cmdline), "/sbin/init splash");
    a->nice = -5;
    a->threads = 1;
    a->cpu_permille = 1234;
    a->cpu_time_ms = 987654321;
    a->rss_kb = 4096;
    a->vsize_kb = 1u << 20;
    a->start_ticks = 3;

    struct process *b = vec_push(procs);
    b->pid = 4242;
    b->ppid = 1;
    b->state = 'R';
    str_copy(b->comm, sizeof(b->comm), "a) b (c");
    b->nice = 19;

    s->time_ms = 1000;
    s->uptime_s = 86400;
    s->load_x100[0] = 52;
    s->load_x100[2] = 7;
    s->tasks_total = 2;
    s->tasks_running = 1;
    s->spawned = 1;
    s->cpu.total_permille = 500;
    s->cpu.ncpu = 16;
    s->mem.total_kb = 16000000;
    s->mem.swap_used_kb = 512;
    s->procs = procs;
}

TEST(test_frame_roundtrip) {
    struct snapshot in, out;
    struct vector in_procs, out_procs;
    make_snapshot(&in, &in_procs);
    vec_init(&out_procs, sizeof(struct process));

    struct buffer b;
    buf_init(&b);
    format_write_frame(&b, &in);
    CHECK(!b.failed);

    struct reader r;
    rd_init(&r, b.data, b.len);
    CHECK(format_read_frame(&r, &out, &out_procs));
    CHECK(rd_remaining(&r) == 0);

    CHECK(out.time_ms == 1000 && out.uptime_s == 86400);
    CHECK(out.load_x100[0] == 52 && out.load_x100[2] == 7);
    CHECK(out.cpu.total_permille == 500 && out.cpu.ncpu == 16);
    CHECK(out.mem.total_kb == 16000000 && out.mem.swap_used_kb == 512);
    CHECK(out.procs == &out_procs && out_procs.len == 2);

    const struct process *p = vec_at(&out_procs, 0);
    CHECK(p->pid == 1 && p->state == 'S' && p->nice == -5);
    CHECK(p->cpu_permille == 1234 && p->cpu_time_ms == 987654321);
    CHECK(p->vsize_kb == (1u << 20) && p->start_ticks == 3);
    CHECK_STR(p->cmdline, "/sbin/init splash");
    p = vec_at(&out_procs, 1);
    CHECK_STR(p->comm, "a) b (c");
    CHECK(p->nice == 19 && p->cmdline[0] == '\0');

    buf_free(&b);
    vec_free(&in_procs);
    vec_free(&out_procs);
}

TEST(test_corrupt_frames) {
    struct snapshot in, out;
    struct vector in_procs, out_procs;
    make_snapshot(&in, &in_procs);
    vec_init(&out_procs, sizeof(struct process));

    struct buffer b;
    buf_init(&b);
    format_write_frame(&b, &in);

    // Truncated.
    struct reader r;
    rd_init(&r, b.data, b.len - 1);
    CHECK(!format_read_frame(&r, &out, &out_procs));
    rd_init(&r, b.data, b.len - 1);
    CHECK(!format_skip_frame(&r));

    // Absurd process count must be rejected before allocating for it.
    usize count_at = 4 + 8 + 8 + 12 + 16 + 24 + 64;
    u8 huge[4] = { 0xff, 0xff, 0xff, 0x7f };
    CHECK(buf_patch(&b, count_at, huge, 4));
    rd_init(&r, b.data, b.len);
    CHECK(!format_read_frame(&r, &out, &out_procs));

    buf_free(&b);
    vec_free(&in_procs);
    vec_free(&out_procs);
}

TEST(test_frame_forward_compat) {
    struct snapshot in, out;
    struct vector in_procs, out_procs;
    make_snapshot(&in, &in_procs);
    vec_init(&out_procs, sizeof(struct process));

    // Frame with 4 unknown trailing bytes, followed by a normal frame.
    struct buffer b;
    buf_init(&b);
    format_write_frame(&b, &in);
    u32 len = (u32)(b.len - 4) + 4;
    u8 le[4] = { (u8)len, (u8)(len >> 8), (u8)(len >> 16), (u8)(len >> 24) };
    buf_patch(&b, 0, le, 4);
    wr_u32(&b, 0xcafef00d);
    in.time_ms = 2000;
    format_write_frame(&b, &in);

    struct reader r;
    rd_init(&r, b.data, b.len);
    CHECK(format_read_frame(&r, &out, &out_procs) && out.time_ms == 1000);
    CHECK(format_read_frame(&r, &out, &out_procs) && out.time_ms == 2000);
    CHECK(rd_remaining(&r) == 0);

    buf_free(&b);
    vec_free(&in_procs);
    vec_free(&out_procs);
}

TEST_MAIN("storage/format",
    T(test_little_endian),
    T(test_header),
    T(test_frame_roundtrip),
    T(test_corrupt_frames),
    T(test_frame_forward_compat))
