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
#include "hardware/watchdog.h"

#include "bridge_mailbox.h"
#include "bridge_log.h"
#include "ble_hid_bridge.h"
#include "usb_descriptors.h"

extern void ble_host_main(void);

#define BLE_CORE1_STACK_SIZE_BYTES 0x2000u
#define BLE_CORE1_STACK_GUARD_WORDS 32u
#define BLE_CORE1_STACK_CANARY UINT32_C(0x58b1ec0d)

static uint32_t core1_stack[BLE_CORE1_STACK_SIZE_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(8)));

_Static_assert(PICO_CORE1_STACK_SIZE == 0,
               "Core 1 uses the explicit main-SRAM stack below");
_Static_assert(sizeof(core1_stack) == BLE_CORE1_STACK_SIZE_BYTES,
               "Core 1 stack budget must remain 8 KiB");

static bool ble_started;
static bool keyboard_in_flight;
static bool mouse_in_flight;
static bridge_keyboard_tx_t keyboard_tx;
static bridge_mouse_tx_t mouse_tx;

static void usb_task(void);
static void send_keyboard_report(void);
static void send_mouse_report(void);
static void initialize_core1_stack_guard(void);
static void check_core1_stack_guard(void);

int main(void) {
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb != NULL) {
        board_init_after_tusb();
    }

    stdio_init_all();
    bridge_mailbox_init();
    initialize_core1_stack_guard();

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
    check_core1_stack_guard();

    // Avoid bringing up the radio before the USB host has configured the
    // bus-powered device. Core 1 explicitly quiesces and resumes BLE for later
    // USB unmount, suspend, mount, and resume transitions.
    if (!ble_started && tud_mounted()) {
        ble_started = true;
        ble_bridge_set_usb_active(true);
        SYS_LOG("Launching BLE host on Core 1\n");
        multicore_launch_core1_with_stack(ble_host_main, core1_stack,
                                          sizeof(core1_stack));
    }

    send_keyboard_report();
    send_mouse_report();
}

static void initialize_core1_stack_guard(void) {
    for (size_t index = 0u; index < BLE_CORE1_STACK_GUARD_WORDS; ++index) {
        __atomic_store_n(&core1_stack[index], BLE_CORE1_STACK_CANARY,
                         __ATOMIC_RELAXED);
    }
}

static void check_core1_stack_guard(void) {
    if (!ble_started) {
        return;
    }
    for (size_t index = 0u; index < BLE_CORE1_STACK_GUARD_WORDS; ++index) {
        if (__atomic_load_n(&core1_stack[index], __ATOMIC_RELAXED) !=
            BLE_CORE1_STACK_CANARY) {
            SYS_LOG("Core 1 stack guard breached; fail-safe reboot\n");
            watchdog_reboot(0u, 0u, 10u);
            while (true) {
                tight_loop_contents();
            }
        }
    }
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
    ble_bridge_set_usb_active(true);
    USB_LOG("Device mounted\n");
}

void tud_umount_cb(void) {
    keyboard_in_flight = false;
    mouse_in_flight = false;
    ble_bridge_set_usb_active(false);
    USB_LOG("Device unmounted\n");
}

void tud_suspend_cb(bool remote_wakeup_en) {
    // Remote wake is not advertised and is never signalled by this firmware.
    // The 5 ms Core-1 control loop also powers down BLE. This is a safety
    // strategy, not a claim that board-level USB suspend current is verified.
    (void)remote_wakeup_en;
    ble_bridge_set_usb_active(false);
    USB_LOG("Bus suspended\n");
}

void tud_resume_cb(void) {
    bridge_mailbox_usb_resync();
    ble_bridge_set_usb_active(true);
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
        const bool boot_protocol =
            tud_hid_n_get_protocol(instance) == HID_PROTOCOL_BOOT;
        bridge_mouse_snapshot(boot_protocol, &report);
        const uint16_t wire_len = boot_protocol ? 3u : sizeof(report);
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
    (void)protocol;
    if (instance == USB_HID_INSTANCE_KEYBOARD) {
        keyboard_in_flight = false;
        bridge_keyboard_usb_resync();
    } else if (instance == USB_HID_INSTANCE_MOUSE) {
        mouse_in_flight = false;
        bridge_mouse_usb_resync();
    }
}

bool tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate) {
    (void)instance;
    (void)idle_rate;
    return true;
}
