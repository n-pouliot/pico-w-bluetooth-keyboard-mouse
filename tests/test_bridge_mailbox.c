#include "bridge_mailbox.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,     \
                          __LINE__, #condition);                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int keyboard_is_zero(const bridge_keyboard_report_t *report) {
    const bridge_keyboard_report_t zero = {0};
    return memcmp(report, &zero, sizeof(*report)) == 0;
}

static int mouse_is_zero(const bridge_mouse_report_t *report) {
    const bridge_mouse_report_t zero = {0};
    return memcmp(report, &zero, sizeof(*report)) == 0;
}

static int test_initial_release_and_generation(void) {
    bridge_keyboard_report_t keyboard = {0};
    bridge_mouse_report_t mouse = {0};
    bridge_keyboard_tx_t keyboard_tx;
    bridge_mouse_tx_t mouse_tx;

    bridge_mailbox_init();
    CHECK(bridge_keyboard_peek(&keyboard, &keyboard_tx));
    CHECK(keyboard_tx.release);
    CHECK(keyboard_is_zero(&keyboard));
    CHECK(bridge_mouse_peek(false, &mouse, &mouse_tx));
    CHECK(mouse_tx.release);
    CHECK(mouse_is_zero(&mouse));

    keyboard.modifier = 1u;
    CHECK(!bridge_keyboard_publish(0u, &keyboard));
    CHECK(bridge_role_activate(BRIDGE_ROLE_KEYBOARD) != 0u);
    return 0;
}

static int test_keyboard_release_barrier_and_stale_input(void) {
    bridge_keyboard_report_t report = {.modifier = 2u, .keycode = {4u}};
    bridge_keyboard_report_t observed;
    bridge_keyboard_tx_t tx;

    bridge_mailbox_init();
    const uint32_t generation = bridge_role_activate(BRIDGE_ROLE_KEYBOARD);
    CHECK(bridge_keyboard_publish(generation, &report));
    CHECK(bridge_keyboard_peek(&observed, &tx));
    CHECK(tx.release);
    CHECK(keyboard_is_zero(&observed));
    bridge_keyboard_complete(&tx);
    CHECK(bridge_keyboard_peek(&observed, &tx));
    CHECK(!tx.release);
    CHECK(memcmp(&observed, &report, sizeof(report)) == 0);

    report.keycode[1] = 5u;
    CHECK(bridge_keyboard_publish(generation, &report));
    bridge_keyboard_complete(&tx);
    CHECK(bridge_keyboard_peek(&observed, &tx));
    CHECK(observed.keycode[1] == 5u);

    bridge_role_release(BRIDGE_ROLE_KEYBOARD, generation);
    CHECK(!bridge_keyboard_publish(generation, &report));
    CHECK(bridge_keyboard_peek(&observed, &tx));
    CHECK(tx.release);
    CHECK(keyboard_is_zero(&observed));
    return 0;
}

static int test_mouse_lossless_chunking(void) {
    bridge_mouse_report_t report;
    bridge_mouse_tx_t tx;

    bridge_mailbox_init();
    const uint32_t generation = bridge_role_activate(BRIDGE_ROLE_MOUSE);
    CHECK(bridge_mouse_publish(generation, 3u, 300, -300, 200, -200));
    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(tx.release);
    bridge_mouse_complete(&tx);

    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(report.buttons == 3u);
    CHECK(report.x == 127 && report.y == -127);
    CHECK(report.wheel == 127 && report.pan == -127);
    CHECK(bridge_mouse_publish(generation, 1u, 10, 20, 0, 0));
    bridge_mouse_complete(&tx);

    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(report.buttons == 1u);
    CHECK(report.x == 127 && report.y == -127);
    CHECK(report.wheel == 73 && report.pan == -73);
    bridge_mouse_complete(&tx);
    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(report.x == 56 && report.y == -26);
    CHECK(report.wheel == 0 && report.pan == 0);
    bridge_mouse_complete(&tx);
    CHECK(!bridge_mouse_peek(false, &report, &tx));
    return 0;
}

static int test_usb_resync_and_boot_mouse(void) {
    bridge_mouse_report_t report;
    bridge_mouse_tx_t tx;

    bridge_mailbox_init();
    const uint32_t generation = bridge_role_activate(BRIDGE_ROLE_MOUSE);
    CHECK(bridge_mouse_publish(generation, 0x1fu, 1, 2, 9, -9) ==
          BRIDGE_MOUSE_PUBLISH_ACCEPTED);
    CHECK(bridge_mouse_peek(true, &report, &tx));
    bridge_mouse_complete(&tx);
    CHECK(bridge_mouse_peek(true, &report, &tx));
    CHECK(report.buttons == 0x07u && report.x == 1 && report.y == 2);
    CHECK(report.wheel == 0 && report.pan == 0);
    bridge_mouse_complete(&tx);
    CHECK(!bridge_mouse_peek(true, &report, &tx));

    bridge_mouse_usb_resync();
    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(tx.release && mouse_is_zero(&report));
    bridge_mouse_complete(&tx);
    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(report.buttons == 0x1fu);
    return 0;
}

static int test_per_interface_resync(void) {
    bridge_keyboard_report_t keyboard = {.keycode = {4u}};
    bridge_keyboard_report_t observed_keyboard;
    bridge_keyboard_tx_t keyboard_tx;
    bridge_mouse_report_t observed_mouse;
    bridge_mouse_tx_t mouse_tx;

    bridge_mailbox_init();
    const uint32_t keyboard_generation =
        bridge_role_activate(BRIDGE_ROLE_KEYBOARD);
    const uint32_t mouse_generation = bridge_role_activate(BRIDGE_ROLE_MOUSE);
    CHECK(bridge_keyboard_publish(keyboard_generation, &keyboard));
    CHECK(bridge_mouse_publish(mouse_generation, 1u, 0, 0, 0, 0) ==
          BRIDGE_MOUSE_PUBLISH_ACCEPTED);
    CHECK(bridge_keyboard_peek(&observed_keyboard, &keyboard_tx));
    bridge_keyboard_complete(&keyboard_tx);
    CHECK(bridge_mouse_peek(false, &observed_mouse, &mouse_tx));
    bridge_mouse_complete(&mouse_tx);
    CHECK(bridge_keyboard_peek(&observed_keyboard, &keyboard_tx));
    bridge_keyboard_complete(&keyboard_tx);
    CHECK(bridge_mouse_peek(false, &observed_mouse, &mouse_tx));
    bridge_mouse_complete(&mouse_tx);

    bridge_keyboard_usb_resync();
    CHECK(bridge_keyboard_peek(&observed_keyboard, &keyboard_tx));
    CHECK(keyboard_tx.release);
    CHECK(!bridge_mouse_peek(false, &observed_mouse, &mouse_tx));
    return 0;
}

static int test_mouse_overflow_is_fail_safe(void) {
    bridge_mouse_report_t report;
    bridge_mouse_tx_t tx;

    bridge_mailbox_init();
    const uint32_t generation = bridge_role_activate(BRIDGE_ROLE_MOUSE);
    CHECK(bridge_mouse_peek(false, &report, &tx));
    bridge_mouse_complete(&tx);
    CHECK(bridge_mouse_publish(generation, 1u, INT32_MAX, 0, 0, 0) ==
          BRIDGE_MOUSE_PUBLISH_ACCEPTED);
    CHECK(bridge_mouse_publish(generation, 1u, 1, 0, 0, 0) ==
          BRIDGE_MOUSE_PUBLISH_OVERFLOW);
    CHECK(bridge_mouse_peek(false, &report, &tx));
    CHECK(tx.release && mouse_is_zero(&report));
    CHECK(bridge_mouse_publish(generation, 1u, 1, 0, 0, 0) ==
          BRIDGE_MOUSE_PUBLISH_STALE);
    return 0;
}

int test_bridge_mailbox(void) {
    CHECK(test_initial_release_and_generation() == 0);
    CHECK(test_keyboard_release_barrier_and_stale_input() == 0);
    CHECK(test_mouse_lossless_chunking() == 0);
    CHECK(test_usb_resync_and_boot_mouse() == 0);
    CHECK(test_per_interface_resync() == 0);
    CHECK(test_mouse_overflow_is_fail_safe() == 0);
    return 0;
}
