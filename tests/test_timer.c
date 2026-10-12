#include <stdio.h>
#include <stdlib.h>
#include "timer.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void register_tests(void)
{
    Timer t = {0};
    uint8_t flags = 0;
    timer_tick(&t, &flags, 255);
    CHECK(timer_read(&t, 0xFF04) == 0);
    timer_tick(&t, &flags, 1);
    CHECK(timer_read(&t, 0xFF04) == 1);
    timer_tick(&t, &flags, 65536 - 256);
    CHECK(t.divider == 0 && flags == 0);
    timer_tick(&t, &flags, 300);
    timer_write(&t, 0xFF04, 0xFF);
    CHECK(t.divider == 0); // Any write resets the entire divider.
    timer_write(&t, 0xFF05, 0x56);
    timer_write(&t, 0xFF06, 0xA7);
    CHECK(timer_read(&t, 0xFF05) == 0x56);
    CHECK(timer_read(&t, 0xFF06) == 0xA7);
    for (unsigned value = 0; value < 256; ++value) {
        timer_write(&t, 0xFF07, (uint8_t)value);
        CHECK(timer_read(&t, 0xFF07) == (uint8_t)(value | 0xF8));
    }
}

static void frequency_tests(void)
{
    const unsigned periods[] = {1024,16,64,256};
    for (unsigned speed = 0; speed < 4; ++speed) {
        Timer t = {0};
        uint8_t flags = 0;
        timer_write(&t, 0xFF07, (uint8_t)(4 | speed));
        timer_tick(&t, &flags, periods[speed] - 1);
        CHECK(t.tima == 0);
        timer_tick(&t, &flags, 1);
        CHECK(t.tima == 1);
        timer_tick(&t, &flags, periods[speed] * 3);
        CHECK(t.tima == 4 && flags == 0);
        timer_write(&t, 0xFF07, (uint8_t)speed);
        timer_tick(&t, &flags, periods[speed] * 2);
        CHECK(t.tima == 4); // Disabling TIMA does not stop DIV.
        CHECK(t.divider == periods[speed] * 6);
    }
}

static void edge_tests(void)
{
    Timer t = {0};
    uint8_t flags = 0;
    timer_write(&t, 0xFF07, 5);
    timer_tick(&t, &flags, 8); // Selected divider bit 3 is high.
    timer_write(&t, 0xFF04, 0);
    CHECK(t.tima == 1); // Reset generates a falling edge.
    timer_write(&t, 0xFF04, 0);
    CHECK(t.tima == 1); // No edge when already low.
    timer_tick(&t, &flags, 8);
    timer_write(&t, 0xFF07, 0);
    CHECK(t.tima == 2); // Disabling a high signal also generates an edge.
    timer_write(&t, 0xFF07, 5);
    CHECK(t.tima == 2); // Rising edges do not increment.
    timer_write(&t, 0xFF07, 6);
    CHECK(t.tima == 3); // Switch from high bit 3 to low bit 5.
}

static void overflow_tests(void)
{
    Timer t = {0};
    uint8_t flags = 0x11;
    timer_write(&t, 0xFF07, 5);
    timer_write(&t, 0xFF05, 0xFF);
    timer_write(&t, 0xFF06, 0x42);
    timer_tick(&t, &flags, 16);
    CHECK(t.tima == 0 && t.reload_delay == 4 && flags == 0x11);
    timer_tick(&t, &flags, 3);
    CHECK(t.tima == 0 && flags == 0x11);
    timer_write(&t, 0xFF06, 0x77); // Pending reload uses the current TMA.
    timer_tick(&t, &flags, 1);
    CHECK(t.tima == 0x77 && flags == 0x15);

    t = (Timer){0}; flags = 0;
    timer_write(&t, 0xFF07, 5);
    timer_write(&t, 0xFF05, 0xFF);
    timer_tick(&t, &flags, 16);
    timer_write(&t, 0xFF05, 0x23); // TIMA write cancels the pending reload/IRQ.
    timer_tick(&t, &flags, 4);
    CHECK(t.tima == 0x23 && t.reload_delay == 0 && flags == 0);

    // Chunking elapsed time must not change timer state or interrupt requests.
    Timer a = {0}, b = {0};
    uint8_t fa = 0, fb = 0;
    timer_write(&a, 0xFF07, 5); timer_write(&b, 0xFF07, 5);
    timer_tick(&a, &fa, 10000);
    for (unsigned i = 0; i < 10000; ++i) timer_tick(&b, &fb, 1);
    CHECK(a.divider == b.divider && a.tima == b.tima);
    CHECK(a.reload_delay == b.reload_delay && fa == fb);
}

int main(void)
{
    register_tests(); frequency_tests(); edge_tests(); overflow_tests();
    puts("PASS: timer registers, frequencies, write edges and overflow behavior");
    return 0;
}
