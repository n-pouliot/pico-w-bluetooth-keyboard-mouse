/*
 * USB/core-0 entry point for the dual BLE HID bridge.
 * TinyUSB-derived callback structure is used under TinyUSB's MIT license.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "bsp/board_api.h"
#include "tusb.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "bridge_mailbox.h"
#include "bridge_log.h"
#include "usb_descriptors.h"

extern void ble_host_main(void);

static bool ble_started;
static bool keyboard_in_flight;
static bool mouse_in_flight;
static bridge_keyboard_tx_t keyboard_tx;
static bridge_mouse_tx_t mouse_tx;

static void usb_task(void);
static void send_keyboard_report(void);
static void send_mouse_report(void);

int main(void) {
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb != NULL) {
        board_init_after_tusb();
    }

    stdio_init_all();
    bridge_mailbox_init();

    // Core 1 owns BTstack TLV flash writes; initialize the other core for the
    // Pico SDK's flash-safe multicore lockout protocol before launching it.
    flash_safe_execute_core_init();

    SYS_LOG("Dual BLE HID bridge starting\n");
    while (true) {
        usb_task();
    }
}

static void usb_task(void) {
    tud_task();

    // Avoid bringing up the radio before the USB host has configured the
    // bus-powered device. Once started, Bluetooth remains independent of later
    // USB unmount/remount events.
    if (!ble_started && tud_mounted()) {
        ble_started = true;
        SYS_LOG("Launching BLE host on Core 1\n");
        multicore_launch_core1(ble_host_main);
    }

    send_keyboard_report();
    send_mouse_report();
}

static void send_keyboard_report(void) {
    if (keyboard_in_flight || !tud_hid_n_ready(USB_HID_INSTANCE_KEYBOARD)) {
        return;
    }

    bridge_keyboard_report_t report;
    bridge_keyboard_tx_t tx;
    if (!bridge_keyboard_peek(&report, &tx)) {
        return;
    }

    if (tud_hid_n_report(USB_HID_INSTANCE_KEYBOARD, 0, &report, sizeof(report))) {
        keyboard_tx = tx;
        keyboard_in_flight = true;
    }
}

static void send_mouse_report(void) {
    if (mouse_in_flight || !tud_hid_n_ready(USB_HID_INSTANCE_MOUSE)) {
        return;
    }

    const bool boot_protocol =
        tud_hid_n_get_protocol(USB_HID_INSTANCE_MOUSE) == HID_PROTOCOL_BOOT;

    bridge_mouse_report_t report;
    bridge_mouse_tx_t tx;
    if (!bridge_mouse_peek(boot_protocol, &report, &tx)) {
        return;
    }

    const uint16_t report_len = boot_protocol ? 3u : (uint16_t)sizeof(report);
    if (tud_hid_n_report(USB_HID_INSTANCE_MOUSE, 0, &report, report_len)) {
        mouse_tx = tx;
        mouse_in_flight = true;
    }
}

void tud_mount_cb(void) {
    keyboard_in_flight = false;
    mouse_in_flight = false;
    bridge_mailbox_usb_resync();
    USB_LOG("Device mounted\n");
}

void tud_umount_cb(void) {
    keyboard_in_flight = false;
    mouse_in_flight = false;
    bridge_mailbox_usb_resync();
    USB_LOG("Device unmounted\n");
}

void tud_suspend_cb(bool remote_wakeup_en) {
    // Remote wake is not advertised and is never signalled by this firmware.
    (void)remote_wakeup_en;
    USB_LOG("Bus suspended\n");
}

void tud_resume_cb(void) {
    bridge_mailbox_usb_resync();
    USB_LOG("Bus resumed\n");
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report,
                                uint16_t len) {
    (void)report;
    (void)len;

    if ((instance == USB_HID_INSTANCE_KEYBOARD) && keyboard_in_flight) {
        bridge_keyboard_complete(&keyboard_tx);
        keyboard_in_flight = false;
    } else if ((instance == USB_HID_INSTANCE_MOUSE) && mouse_in_flight) {
        bridge_mouse_complete(&mouse_tx);
        mouse_in_flight = false;
    }
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type,
                              const uint8_t *report, uint16_t xferred_bytes) {
    (void)report_type;
    (void)report;
    (void)xferred_bytes;

    // Do not commit the mailbox token. The report, including a release barrier,
    // remains pending and will be retried when the endpoint is ready.
    if (instance == USB_HID_INSTANCE_KEYBOARD) {
        keyboard_in_flight = false;
    } else if (instance == USB_HID_INSTANCE_MOUSE) {
        mouse_in_flight = false;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    if ((buffer == NULL) || (report_id != 0) ||
        (report_type != HID_REPORT_TYPE_INPUT)) {
        return 0;
    }

    if (instance == USB_HID_INSTANCE_KEYBOARD) {
        bridge_keyboard_report_t report;
        bridge_keyboard_snapshot(&report);
        const uint16_t count = reqlen < sizeof(report) ? reqlen : sizeof(report);
        memcpy(buffer, &report, count);
        return count;
    }

    if (instance == USB_HID_INSTANCE_MOUSE) {
        bridge_mouse_report_t report;
        bridge_mouse_snapshot(&report);
        const uint16_t wire_len =
            tud_hid_n_get_protocol(instance) == HID_PROTOCOL_BOOT ? 3u : sizeof(report);
        const uint16_t count = reqlen < wire_len ? reqlen : wire_len;
        memcpy(buffer, &report, count);
        return count;
    }

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t bufsize) {
    // The keyboard descriptor contains the standard one-byte lock-LED output
    // report. Release 1 accepts and safely ignores it; BLE LED forwarding is a
    // documented limitation.
    if ((instance == USB_HID_INSTANCE_KEYBOARD) && (report_id == 0) &&
        (report_type == HID_REPORT_TYPE_OUTPUT) && (buffer != NULL) &&
        (bufsize >= 1)) {
        USB_LOG("Keyboard LED output: 0x%02x\n", buffer[0]);
    }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    (void)instance;
    (void)protocol;
    keyboard_in_flight = false;
    mouse_in_flight = false;
    bridge_mailbox_usb_resync();
}

bool tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate) {
    (void)instance;
    (void)idle_rate;
    return true;
}
