#include "bridge_mailbox.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "pico/critical_section.h"

_Static_assert(sizeof(bridge_keyboard_report_t) == 8, "keyboard report must be 8 bytes");
_Static_assert(sizeof(bridge_mouse_report_t) == 5, "mouse report must be 5 bytes");

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    uint32_t release_sequence;
    bridge_keyboard_report_t state;
    bool state_dirty;
    bool release_pending;
} keyboard_mailbox_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    uint32_t release_sequence;
    int32_t x;
    int32_t y;
    int32_t wheel;
    int32_t pan;
    uint8_t buttons;
    bool state_dirty;
    bool release_pending;
} mouse_mailbox_t;

static critical_section_t mailbox_lock;
static keyboard_mailbox_t keyboard_mailbox;
static mouse_mailbox_t mouse_mailbox;

static int32_t saturating_add_i32(int32_t lhs, int32_t rhs) {
    if ((rhs > 0) && (lhs > INT32_MAX - rhs)) {
        return INT32_MAX;
    }
    if ((rhs < 0) && (lhs < INT32_MIN - rhs)) {
        return INT32_MIN;
    }
    return lhs + rhs;
}

static int8_t next_usb_delta(int32_t value) {
    if (value > INT8_MAX) {
        return INT8_MAX;
    }
    if (value < -INT8_MAX) {
        return -INT8_MAX;
    }
    return (int8_t)value;
}

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation == 0u ? 1u : generation;
}

static void schedule_keyboard_release(void) {
    keyboard_mailbox.release_sequence++;
    keyboard_mailbox.release_pending = true;
}

static void schedule_mouse_release(void) {
    mouse_mailbox.release_sequence++;
    mouse_mailbox.release_pending = true;
}

void bridge_mailbox_init(void) {
    memset(&keyboard_mailbox, 0, sizeof(keyboard_mailbox));
    memset(&mouse_mailbox, 0, sizeof(mouse_mailbox));
    critical_section_init(&mailbox_lock);

    critical_section_enter_blocking(&mailbox_lock);
    schedule_keyboard_release();
    schedule_mouse_release();
    critical_section_exit(&mailbox_lock);
}

uint32_t bridge_role_activate(bridge_role_t role) {
    uint32_t generation = 0;

    critical_section_enter_blocking(&mailbox_lock);
    if (role == BRIDGE_ROLE_KEYBOARD) {
        keyboard_mailbox.generation =
            next_generation(keyboard_mailbox.generation);
        generation = keyboard_mailbox.generation;
        memset(&keyboard_mailbox.state, 0, sizeof(keyboard_mailbox.state));
        keyboard_mailbox.state_dirty = false;
        keyboard_mailbox.sequence++;
        schedule_keyboard_release();
    } else if (role == BRIDGE_ROLE_MOUSE) {
        mouse_mailbox.generation = next_generation(mouse_mailbox.generation);
        generation = mouse_mailbox.generation;
        mouse_mailbox.x = 0;
        mouse_mailbox.y = 0;
        mouse_mailbox.wheel = 0;
        mouse_mailbox.pan = 0;
        mouse_mailbox.buttons = 0;
        mouse_mailbox.state_dirty = false;
        mouse_mailbox.sequence++;
        schedule_mouse_release();
    }
    critical_section_exit(&mailbox_lock);

    return generation;
}

void bridge_role_release(bridge_role_t role, uint32_t generation) {
    critical_section_enter_blocking(&mailbox_lock);
    if ((role == BRIDGE_ROLE_KEYBOARD) && (keyboard_mailbox.generation == generation)) {
        keyboard_mailbox.generation =
            next_generation(keyboard_mailbox.generation);
        memset(&keyboard_mailbox.state, 0, sizeof(keyboard_mailbox.state));
        keyboard_mailbox.state_dirty = false;
        keyboard_mailbox.sequence++;
        schedule_keyboard_release();
    } else if ((role == BRIDGE_ROLE_MOUSE) && (mouse_mailbox.generation == generation)) {
        mouse_mailbox.generation = next_generation(mouse_mailbox.generation);
        mouse_mailbox.x = 0;
        mouse_mailbox.y = 0;
        mouse_mailbox.wheel = 0;
        mouse_mailbox.pan = 0;
        mouse_mailbox.buttons = 0;
        mouse_mailbox.state_dirty = false;
        mouse_mailbox.sequence++;
        schedule_mouse_release();
    }
    critical_section_exit(&mailbox_lock);
}

bool bridge_keyboard_publish(uint32_t generation, const bridge_keyboard_report_t *report) {
    if (report == NULL) {
        return false;
    }

    bool accepted = false;
    critical_section_enter_blocking(&mailbox_lock);
    if ((generation != 0u) && (keyboard_mailbox.generation == generation)) {
        accepted = true;
        if (memcmp(&keyboard_mailbox.state, report, sizeof(*report)) != 0) {
            keyboard_mailbox.state = *report;
            keyboard_mailbox.state_dirty = true;
            keyboard_mailbox.sequence++;
        }
    }
    critical_section_exit(&mailbox_lock);
    return accepted;
}

bool bridge_mouse_publish(uint32_t generation, uint8_t buttons,
                          int32_t x, int32_t y, int32_t wheel, int32_t pan) {
    bool accepted = false;
    critical_section_enter_blocking(&mailbox_lock);
    if ((generation != 0u) && (mouse_mailbox.generation == generation)) {
        accepted = true;
        const bool changed = (mouse_mailbox.buttons != buttons) ||
                             (x != 0) || (y != 0) || (wheel != 0) || (pan != 0);
        if (changed) {
            mouse_mailbox.buttons = buttons;
            mouse_mailbox.x = saturating_add_i32(mouse_mailbox.x, x);
            mouse_mailbox.y = saturating_add_i32(mouse_mailbox.y, y);
            mouse_mailbox.wheel = saturating_add_i32(mouse_mailbox.wheel, wheel);
            mouse_mailbox.pan = saturating_add_i32(mouse_mailbox.pan, pan);
            mouse_mailbox.state_dirty = true;
            mouse_mailbox.sequence++;
        }
    }
    critical_section_exit(&mailbox_lock);
    return accepted;
}

bool bridge_keyboard_peek(bridge_keyboard_report_t *report, bridge_keyboard_tx_t *tx) {
    if ((report == NULL) || (tx == NULL)) {
        return false;
    }

    bool available = false;
    critical_section_enter_blocking(&mailbox_lock);
    if (keyboard_mailbox.release_pending) {
        memset(report, 0, sizeof(*report));
        tx->generation = keyboard_mailbox.generation;
        tx->sequence = keyboard_mailbox.release_sequence;
        tx->release = true;
        available = true;
    } else if (keyboard_mailbox.state_dirty) {
        *report = keyboard_mailbox.state;
        tx->generation = keyboard_mailbox.generation;
        tx->sequence = keyboard_mailbox.sequence;
        tx->release = false;
        available = true;
    }
    critical_section_exit(&mailbox_lock);
    return available;
}

void bridge_keyboard_complete(const bridge_keyboard_tx_t *tx) {
    if (tx == NULL) {
        return;
    }

    critical_section_enter_blocking(&mailbox_lock);
    if (tx->generation == keyboard_mailbox.generation) {
        if (tx->release) {
            if (tx->sequence == keyboard_mailbox.release_sequence) {
                keyboard_mailbox.release_pending = false;
            }
        } else if (tx->sequence == keyboard_mailbox.sequence) {
            keyboard_mailbox.state_dirty = false;
        }
    }
    critical_section_exit(&mailbox_lock);
}

bool bridge_mouse_peek(bool boot_protocol, bridge_mouse_report_t *report,
                       bridge_mouse_tx_t *tx) {
    if ((report == NULL) || (tx == NULL)) {
        return false;
    }

    bool available = false;
    critical_section_enter_blocking(&mailbox_lock);
    memset(tx, 0, sizeof(*tx));
    if (mouse_mailbox.release_pending) {
        memset(report, 0, sizeof(*report));
        tx->generation = mouse_mailbox.generation;
        tx->sequence = mouse_mailbox.release_sequence;
        tx->release = true;
        available = true;
    } else if (mouse_mailbox.state_dirty) {
        report->buttons = mouse_mailbox.buttons;
        report->x = next_usb_delta(mouse_mailbox.x);
        report->y = next_usb_delta(mouse_mailbox.y);
        report->wheel = boot_protocol ? 0 : next_usb_delta(mouse_mailbox.wheel);
        report->pan = boot_protocol ? 0 : next_usb_delta(mouse_mailbox.pan);

        tx->generation = mouse_mailbox.generation;
        tx->sequence = mouse_mailbox.sequence;
        tx->consume_x = report->x;
        tx->consume_y = report->y;
        tx->consume_wheel = boot_protocol ? mouse_mailbox.wheel : report->wheel;
        tx->consume_pan = boot_protocol ? mouse_mailbox.pan : report->pan;
        tx->release = false;
        available = true;
    }
    critical_section_exit(&mailbox_lock);
    return available;
}

void bridge_mouse_complete(const bridge_mouse_tx_t *tx) {
    if (tx == NULL) {
        return;
    }

    critical_section_enter_blocking(&mailbox_lock);
    if (tx->generation == mouse_mailbox.generation) {
        if (tx->release) {
            if (tx->sequence == mouse_mailbox.release_sequence) {
                mouse_mailbox.release_pending = false;
            }
        } else {
            mouse_mailbox.x -= tx->consume_x;
            mouse_mailbox.y -= tx->consume_y;
            mouse_mailbox.wheel -= tx->consume_wheel;
            mouse_mailbox.pan -= tx->consume_pan;

            if (tx->sequence == mouse_mailbox.sequence) {
                mouse_mailbox.state_dirty = (mouse_mailbox.x != 0) ||
                                             (mouse_mailbox.y != 0) ||
                                             (mouse_mailbox.wheel != 0) ||
                                             (mouse_mailbox.pan != 0);
            } else {
                mouse_mailbox.state_dirty = true;
            }
        }
    }
    critical_section_exit(&mailbox_lock);
}

void bridge_keyboard_snapshot(bridge_keyboard_report_t *report) {
    if (report == NULL) {
        return;
    }

    critical_section_enter_blocking(&mailbox_lock);
    if (keyboard_mailbox.release_pending) {
        memset(report, 0, sizeof(*report));
    } else {
        *report = keyboard_mailbox.state;
    }
    critical_section_exit(&mailbox_lock);
}

void bridge_mouse_snapshot(bridge_mouse_report_t *report) {
    if (report == NULL) {
        return;
    }

    critical_section_enter_blocking(&mailbox_lock);
    memset(report, 0, sizeof(*report));
    if (!mouse_mailbox.release_pending) {
        report->buttons = mouse_mailbox.buttons;
    }
    critical_section_exit(&mailbox_lock);
}

void bridge_mailbox_usb_resync(void) {
    critical_section_enter_blocking(&mailbox_lock);
    keyboard_mailbox.sequence++;
    schedule_keyboard_release();
    keyboard_mailbox.state_dirty = memcmp(&keyboard_mailbox.state,
                                          &(bridge_keyboard_report_t){0},
                                          sizeof(keyboard_mailbox.state)) != 0;

    mouse_mailbox.sequence++;
    schedule_mouse_release();
    mouse_mailbox.state_dirty = (mouse_mailbox.buttons != 0) ||
                                (mouse_mailbox.x != 0) ||
                                (mouse_mailbox.y != 0) ||
                                (mouse_mailbox.wheel != 0) ||
                                (mouse_mailbox.pan != 0);
    critical_section_exit(&mailbox_lock);
}
