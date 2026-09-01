#ifndef XBOX_PICO_BRIDGE_MAILBOX_H
#define XBOX_PICO_BRIDGE_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BRIDGE_ROLE_KEYBOARD = 0,
    BRIDGE_ROLE_MOUSE = 1,
} bridge_role_t;

typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} bridge_keyboard_report_t;

typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t pan;
} bridge_mouse_report_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    bool release;
} bridge_keyboard_tx_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    int32_t consume_x;
    int32_t consume_y;
    int32_t consume_wheel;
    int32_t consume_pan;
    bool release;
} bridge_mouse_tx_t;

typedef enum {
    BRIDGE_MOUSE_PUBLISH_STALE = 0,
    BRIDGE_MOUSE_PUBLISH_ACCEPTED,
    BRIDGE_MOUSE_PUBLISH_OVERFLOW,
} bridge_mouse_publish_result_t;

void bridge_mailbox_init(void);

// Starts a new accepted role generation. A zero-state USB barrier is scheduled
// before input from the new generation can be observed by the host.
uint32_t bridge_role_activate(bridge_role_t role);

// Invalidates one active generation and schedules an idempotent zero-state
// barrier. Stale callbacks using the old generation are rejected.
void bridge_role_release(bridge_role_t role, uint32_t generation);

bool bridge_keyboard_publish(uint32_t generation, const bridge_keyboard_report_t *report);
bridge_mouse_publish_result_t bridge_mouse_publish(
    uint32_t generation, uint8_t buttons,
    int32_t x, int32_t y, int32_t wheel, int32_t pan);

bool bridge_keyboard_peek(bridge_keyboard_report_t *report, bridge_keyboard_tx_t *tx);
void bridge_keyboard_complete(const bridge_keyboard_tx_t *tx);

bool bridge_mouse_peek(bool boot_protocol, bridge_mouse_report_t *report,
                       bridge_mouse_tx_t *tx);
void bridge_mouse_complete(const bridge_mouse_tx_t *tx);

void bridge_keyboard_snapshot(bridge_keyboard_report_t *report);
void bridge_mouse_snapshot(bool boot_protocol, bridge_mouse_report_t *report);

// Used after USB mount or a per-interface protocol change. The host receives a
// release first, then the current canonical state; BLE state is not discarded.
void bridge_keyboard_usb_resync(void);
void bridge_mouse_usb_resync(void);
void bridge_mailbox_usb_resync(void);

#endif
