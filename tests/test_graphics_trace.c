#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "graphics_trace.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static char *contents(FILE *file)
{
    CHECK(fflush(file) == 0 && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size >= 0 && size < 100000);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    char *text = malloc((size_t)size + 1);
    CHECK(text != NULL && fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = '\0';
    return text;
}

static void pixel_and_clock_tests(void)
{
    Memory m;
    GraphicsTrace t;
    GraphicsTraceOptions o;
    graphics_trace_options_init(&o); o.enabled = true; o.frames = 1;
    FILE *out = tmpfile(); CHECK(out != NULL);
    memory_init(&m);
    CHECK(graphics_trace_start(&t, &m, &o, out));
    memory_write(&m, 0xFF43, 5); memory_write(&m, 0xFF42, 7);
    memory_write(&m, 0xFF47, 0x1B);
    memory_write(&m, 0x98A3, 0x80);
    memory_write(&m, 0x880E, 0); memory_write(&m, 0x880F, 0x40);
    memory_write(&m, 0xFF40, 0x81); // Signed addressing, BG on.
    CHECK(t.cycles == 0 && m.timer.divider == 0);
    memory_tick(&m, 65664); // A batch must retain exact intra-batch event times.
    CHECK(t.cycles == 65664 && m.ppu.framebuffer[40][20] == 1);
    CHECK(m.ppu.frame_ready && m.io[0x0F] == 1);
    memory_tick(&m, 70224); // No pixel records for frame 2 with a one-frame limit.
    char *text = contents(out);
    CHECK(strstr(text, "frame=1 cycle=18492] PIXEL xy=20,40 map=98A3 tile=80 row_address=880E bytes=00,40 color=2 BGP=1B palette=1 framebuffer=1"));
    CHECK(strstr(text, "frame=1 cycle=65664] VBLANK"));
    CHECK(strstr(text, "pc=unknown origin=raw-bus"));
    CHECK(!strstr(text, "frame=2"));
    free(text); CHECK(fclose(out) == 0);
}

static void blank_and_restart_tests(void)
{
    Memory m;
    GraphicsTrace t;
    GraphicsTraceOptions o;
    FILE *out = tmpfile(); CHECK(out);
    memory_init(&m); graphics_trace_options_init(&o); o.enabled = true;
    CHECK(graphics_trace_start(&t, &m, &o, out));
    memory_write(&m, 0xFF40, 0x80); // LCD on, BG off.
    memory_tick(&m, 18492);
    memory_write(&m, 0xFF40, 0);
    ppu_attach_vram(&m.ppu, NULL);
    memory_write(&m, 0xFF40, 0x91); // Restart, but no tile source attached.
    memory_tick(&m, 18492);
    CHECK(t.frame == 2 && t.cycles == 36984 && m.ppu.framebuffer[40][20] == 0);
    char *text = contents(out);
    CHECK(strstr(text, "frame=1 cycle=18492] PIXEL xy=20,40 source=BG-disabled map=n/a"));
    CHECK(strstr(text, "frame=2 cycle=36984] PIXEL xy=20,40 source=VRAM-detached map=n/a"));
    free(text); CHECK(fclose(out) == 0);
}

static void diagnostic_equivalence(const char *rom)
{
    Memory a, b;
    CPU ca, cb;
    GraphicsTrace trace;
    GraphicsTraceOptions options;
    FILE *out = tmpfile(); CHECK(out != NULL);
    memory_init(&a); memory_init(&b);
    CHECK(load_rom(&a.cartridge, rom) && load_rom(&b.cartridge, rom));
    reset_cpu(&ca, &a); reset_cpu(&cb, &b);
    graphics_trace_options_init(&options); options.enabled = true;
    CHECK(graphics_trace_start(&trace, &b, &options, out));
    unsigned frames = 0;
    for (unsigned step = 0; step < 200000 && frames < 2; ++step) {
        CHECK(cpu_step(&ca, &a) == cpu_step(&cb, &b));
        CHECK(a.ppu.frame_ready == b.ppu.frame_ready);
        if (a.ppu.frame_ready) {
            ++frames; a.ppu.frame_ready = b.ppu.frame_ready = false;
        }
    }
    CHECK(frames == 2 && trace.cycles == cb.total_cycles);
    for (unsigned r = REG_A; r <= REG_PC; ++r) CHECK(get_reg(&ca, (Reg)r) == get_reg(&cb, (Reg)r));
    CHECK(ca.total_cycles == cb.total_cycles && ca.state == cb.state && ca.ime == cb.ime);
    CHECK(memcmp(a.vram, b.vram, sizeof(a.vram)) == 0);
    CHECK(memcmp(a.wram, b.wram, sizeof(a.wram)) == 0);
    CHECK(memcmp(a.hram, b.hram, sizeof(a.hram)) == 0);
    CHECK(memcmp(a.io, b.io, sizeof(a.io)) == 0 && a.ie == b.ie);
    CHECK(a.timer.divider == b.timer.divider && a.timer.tima == b.timer.tima);
    CHECK(a.ppu.ly == b.ppu.ly && a.ppu.dot == b.ppu.dot && a.ppu.stat == b.ppu.stat);
    CHECK(memcmp(a.ppu.framebuffer, b.ppu.framebuffer, sizeof(a.ppu.framebuffer)) == 0);
    char *text = contents(out);
    CHECK(strstr(text, "ROM_DATA_READ pc=") && strstr(text, "VRAM_WRITE pc="));
    CHECK(strstr(text, "address=98A2 value=00"));
    CHECK(strstr(text, "PIXEL xy=20,40 map=98A2 tile=00 row_address=8000 bytes=00,00 color=0 BGP=E4 palette=0 framebuffer=0"));
    CHECK(strstr(text, "frame=2"));
    unsigned reads = 0, writes = 0;
    for (char *p = text; (p = strstr(p, "ROM_DATA_READ")); ++p) ++reads;
    for (char *p = text; (p = strstr(p, "VRAM_WRITE")); ++p) ++writes;
    CHECK(reads <= 16 && writes <= 64);
    free(text); CHECK(fclose(out) == 0);
}

static void bounds_and_options(void)
{
    GraphicsTraceOptions o;
    graphics_trace_options_init(&o);
    char *args[] = {"test", "--trace-pixel", "159,143", "--trace-frames", "60"};
    int i = 1; CHECK(graphics_trace_option(&o, 5, args, &i) == 1 && i == 2);
    CHECK(o.enabled && o.x == 159 && o.y == 143);
    i = 3; CHECK(graphics_trace_option(&o, 5, args, &i) == 1 && o.frames == 60);
    char *bad[] = {"test", "--trace-pixel", "160,40"};
    i = 1; CHECK(graphics_trace_option(&o, 3, bad, &i) == -1);
    Memory m;
    GraphicsTrace t;
    FILE *out = tmpfile(); CHECK(out);
    memory_init(&m);
    graphics_trace_options_init(&o);
    CHECK(graphics_trace_start(&t, &m, &o, out) && !m.trace);
    char *text = contents(out); CHECK(*text == '\0'); free(text);
    o.enabled = true;
    CHECK(graphics_trace_start(&t, &m, &o, out));
    for (unsigned n = 0; n < 1000; ++n)
        graphics_trace_pixel(&t, 0x9800, 0, 0x8000, 0, 0, 0, 0, 0);
    text = contents(out);
    unsigned lines = 0;
    for (char *p = text; *p; ++p) if (*p == '\n') ++lines;
    CHECK(lines == 257 && t.events == 256); // START + bounded records.
    char *limit = strstr(text, "LIMIT event_cap=256");
    CHECK(limit && !strstr(limit + 1, "LIMIT event_cap=256"));
    o.enabled = false;
    CHECK(graphics_trace_start(&t, &m, &o, out) && !m.trace && !m.ppu.trace);
    free(text); CHECK(fclose(out) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    pixel_and_clock_tests(); blank_and_restart_tests(); diagnostic_equivalence(argv[1]); bounds_and_options();
    puts("PASS: graphics trace pixel provenance, exact cycles, bounded output and execution equivalence");
    return 0;
}
