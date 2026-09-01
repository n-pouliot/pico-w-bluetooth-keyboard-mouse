#ifndef XBOX_PICO_BRIDGE_LOG_H
#define XBOX_PICO_BRIDGE_LOG_H

#include <stdio.h>

#define SYS_LOG(...) printf("[SYS] " __VA_ARGS__)
#define BLE_LOG(...) printf("[BLE] " __VA_ARGS__)

#ifdef ENABLE_USB_LOGGING
#define USB_LOG(...) printf("[USB] " __VA_ARGS__)
#else
#define USB_LOG(...) ((void)0)
#endif

#endif
