#ifndef XBOX_PICO_BLE_HID_BRIDGE_H
#define XBOX_PICO_BLE_HID_BRIDGE_H

#include <stdbool.h>

/* Thread-safe request from USB/Core 0; Core 1 applies it in its run loop. */
void ble_bridge_set_usb_active(bool active);

#endif
