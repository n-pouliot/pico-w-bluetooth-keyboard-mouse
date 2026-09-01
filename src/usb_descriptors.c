/*
 * Static, canonical USB descriptors for the dual BLE HID bridge.
 * Derived from TinyUSB's hid_multiple_interface example (MIT license).
 */

#include "usb_descriptors.h"

#include <stddef.h>
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

#define USB_VID 0xCAFEu
#define USB_PID 0x4008u
#define USB_BCD 0x0200u
#define USB_DEVICE_RELEASE 0x0100u

#define USB_CONFIGURATION_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + (2u * TUD_HID_DESC_LEN))

const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_DEVICE_RELEASE,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const uint8_t usb_keyboard_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

const uint8_t usb_mouse_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

const uint8_t usb_configuration_descriptor[] = {
    // Configuration number, interface count, string, total length,
    // attributes, and configured current in mA. Remote wake is not exposed.
    TUD_CONFIG_DESCRIPTOR(1, USB_INTERFACE_COUNT, 0,
                          USB_CONFIGURATION_TOTAL_LEN, 0, 500),

    TUD_HID_DESCRIPTOR(USB_INTERFACE_KEYBOARD, 4,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(usb_keyboard_report_descriptor),
                       USB_ENDPOINT_KEYBOARD_IN,
                       USB_HID_ENDPOINT_SIZE, 1),

    TUD_HID_DESCRIPTOR(USB_INTERFACE_MOUSE, 5,
                       HID_ITF_PROTOCOL_MOUSE,
                       sizeof(usb_mouse_report_descriptor),
                       USB_ENDPOINT_MOUSE_IN,
                       USB_HID_ENDPOINT_SIZE, 1),
};

_Static_assert(USB_HID_INSTANCE_COUNT == CFG_TUD_HID,
               "TinyUSB HID instance count mismatch");
_Static_assert(sizeof(usb_configuration_descriptor) == USB_CONFIGURATION_TOTAL_LEN,
               "USB configuration descriptor length mismatch");
_Static_assert(USB_CONFIGURATION_TOTAL_LEN == 59u,
               "unexpected USB configuration topology");

uint8_t const *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
        case USB_HID_INSTANCE_KEYBOARD:
            return usb_keyboard_report_descriptor;
        case USB_HID_INSTANCE_MOUSE:
            return usb_mouse_report_descriptor;
        default:
            return NULL;
    }
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return usb_configuration_descriptor;
}

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_KEYBOARD_INTERFACE,
    STRID_MOUSE_INTERFACE,
};

static const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "xbox-pico project",
    "Dual BLE HID Bridge",
    NULL,
    "Keyboard",
    "Mouse",
};

static uint16_t desc_string[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t count = 0;

    if (index == STRID_LANGID) {
        memcpy(&desc_string[1], string_desc_arr[STRID_LANGID], 2);
        count = 1;
    } else if (index == STRID_SERIAL) {
        count = board_usb_get_serial(&desc_string[1], 32);
    } else {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        const char *const source = string_desc_arr[index];
        if (source == NULL) {
            return NULL;
        }

        count = strlen(source);
        if (count > 32) {
            count = 32;
        }
        for (size_t i = 0; i < count; ++i) {
            desc_string[i + 1] = (uint8_t)source[i];
        }
    }

    desc_string[0] = (uint16_t)((TUSB_DESC_STRING << 8) | ((2u * count) + 2u));
    return desc_string;
}
