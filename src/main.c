#include "wproc.h"
#include "platform/file.h"
#include "util/buffer.h"

i32 wproc_main(const struct wproc_startup *s) {
    struct buffer out;
    buf_init(&out);
    buf_append_str(&out, "clk_tck=");
    buf_append_u64(&out, s->clk_tck);
    buf_append_str(&out, " page_size=");
    buf_append_u64(&out, s->page_size);
    buf_append_char(&out, '\n');
    bool ok = !out.failed && file_write_all(1, out.data, out.len);
    buf_free(&out);
    return ok ? 0 : 1;
}
