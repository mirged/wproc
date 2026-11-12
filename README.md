# wproc

A process monitor for Linux, written in C11 with no libc. Not glibc, not
musl, not anything: `-nostdlib -ffreestanding`, a hand-written `_start`, and
raw x86_64 syscalls. The static binary is about 45 KB.

```
 wproc  debian  ·  up 01:31:54  ·  load 0.54 0.68 0.70  ·  408 tasks, 1 running   live  every 1.0s

 cpu                     ▁▁▂▁▁▁▃▅▂▁▁▁▂▁▁   6.8%   usr 5.6%  sys 1.2%  io 0.0%   16 cores
 mem  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━──  46.4%   7.0G of 15G  cache 8.5G   swap 512K of 23G

         cpu↓    name                 pid     mem       time  command
 █▌     31.5%  ● chrome             35122    419M    0:11.41  --type=renderer ...
 █▎     26.3%    steamwebhelper     33192    410M    6:42.97  --type=zygote ...
 ▌      10.5%    gnome-shell         2244    289M    3:16.27
                 · · ·  397 more, doing nothing in particular
 ? keys                                                                         1-7 of 409
```

## Building

```sh
make            # build/wproc
make test       # build and run the test suite
make debug      # -O0 -g
make CC=clang   # also works
```

Requires x86_64 Linux and gcc or clang. There are no other dependencies,
because there was no room for them.

## Usage

```
wproc [options]

  -b          batch mode: print plain snapshots to stdout
              (implied when stdout is not a terminal)
  -d <ms>     refresh interval in milliseconds (default 1000)
  -n <count>  exit after this many updates
  -l <rows>   batch mode: limit processes per snapshot
  -s <key>    sort by cpu, mem, pid, time or name
  -r <file>   record the session to a file
  -p <file>   play back a recording
```

Press `?` inside wproc for keys. The short version: `p m n t a` sort (press
again to reverse), `c` cycles the command column, `space` pauses, `+`/`-`
change speed, `q` quits. During playback, `←`/`→` step one frame and `[`/`]`
skip ten.

### Recording

```sh
wproc -r incident.wpr           # watch and record
wproc -b -r incident.wpr -n 600 # record ten minutes, headless
wproc -p incident.wpr           # replay it later, on any machine
wproc -b -p incident.wpr | less # or as text
```

## Layout

```
src/
├── main.c              argument parsing, live / batch / playback loops
├── util/               the standard library we did not link
│   ├── alloc           mmap-backed allocator (mremap to grow)
│   ├── string          mem*, str*, number formatting; also memcpy & co
│   ├── buffer          growable bytes with sticky allocation failure
│   ├── vector          type-erased dynamic array + heapsort
│   ├── parse           cursor-based number and field parsing
│   └── time            monotonic clock, sleeping, duration formatting
├── platform/linux/     the only code that knows about /proc
│   ├── start           _start, argv/envp/auxv (CLK_TCK, page size)
│   ├── file            read-until-EOF, write-all
│   ├── procfs          pid enumeration via getdents64
│   ├── process         /proc/<pid>/stat, statm, cmdline
│   ├── cpu             /proc/stat, loadavg, uptime, hostname
│   └── memory          /proc/meminfo
├── core/               turns samples into numbers worth showing
│   ├── process         sorted process table, pid-reuse detection
│   ├── cpu             utilisation from tick deltas
│   ├── memory          used / available / cache
│   └── monitor         one update = one snapshot
├── storage/            recording format and player
└── ui/                 terminal, input decoding, rendering
```

Dependencies point downward: `ui → storage → core → platform → util`. `util`
uses nothing except raw syscall wrappers, and only for `alloc` and `time`. The
UI renders a `struct snapshot` and has no idea whether that came from `/proc`
or from a file.

## Notes from the field

- **`/proc/<pid>/stat` rss is approximate.** The kernel sums per-CPU counters
  lazily, so a small process can show 0 and a large one can be off by
  megabytes. wproc reads `statm` instead, like htop does.
- **Process names can contain `)`.** The only safe delimiter in `stat` is the
  *last* closing paren.
- **pids get reused.** A pid with a different start time is treated as a new
  process, so CPU deltas never cross between two strangers.
- **GCC will turn your `memset` loop into a call to `memset`.** When you *are*
  `memset`, that recurses forever. `-fno-tree-loop-distribute-patterns` stops
  it.
- **The recording format** is little-endian and written field by field, with
  length-prefixed header and frames. Readers skip unknown trailing bytes, so
  new fields can be added without breaking old files (the hostname got in this
  way). See `include/storage/format.h`.

## Known limitations

- x86_64 only (the syscall numbers and `_start` are arch-specific).
- No signal handling. If wproc is killed with SIGTERM/SIGKILL, the terminal
  stays in raw mode; `reset` fixes it. Ctrl-C works normally, because in raw
  mode it reaches wproc as a keypress.
- Process names are shown as ASCII; other bytes are displayed as `?`.
