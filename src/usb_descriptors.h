#ifndef XBOX_PICO_USB_DESCRIPTORS_H
#define XBOX_PICO_USB_DESCRIPTORS_H

#include <stdint.h>

enum {
    USB_HID_INSTANCE_KEYBOARD = 0,
    USB_HID_INSTANCE_MOUSE,
    USB_HID_INSTANCE_COUNT,
};

enum {
    USB_INTERFACE_KEYBOARD = 0,
    USB_INTERFACE_MOUSE,
    USB_INTERFACE_COUNT,
};

#define USB_ENDPOINT_KEYBOARD_IN 0x81u
#define USB_ENDPOINT_MOUSE_IN    0x82u
#define USB_HID_ENDPOINT_SIZE    8u

extern const uint8_t usb_keyboard_report_descriptor[];
extern const uint8_t usb_mouse_report_descriptor[];
extern const uint8_t usb_configuration_descriptor[];

#endif
