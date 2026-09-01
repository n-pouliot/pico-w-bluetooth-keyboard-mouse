#ifndef XBOX_PICO_BLE_BRIDGE_POLICY_H
#define XBOX_PICO_BLE_BRIDGE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BLE_APPEARANCE_HINT_NONE = 0,
    BLE_APPEARANCE_HINT_GENERIC_HID,
    BLE_APPEARANCE_HINT_KEYBOARD,
    BLE_APPEARANCE_HINT_MOUSE,
    BLE_APPEARANCE_HINT_UNSUPPORTED_HID,
    BLE_APPEARANCE_HINT_CONFLICT,
} ble_appearance_hint_t;

typedef struct {
    bool advertises_hid;
    ble_appearance_hint_t appearance;
} ble_advertisement_policy_t;

/* Parses bounded GAP advertising data. False means structurally malformed. */
bool ble_bridge_parse_advertisement(const uint8_t *data, size_t length,
                                    ble_advertisement_policy_t *out_policy);

/* role uses the hid_report_role_t numeric values: keyboard=1, mouse=2. */
bool ble_bridge_appearance_allows_role(ble_appearance_hint_t hint,
                                       uint8_t role);

bool ble_bridge_address_matches(uint8_t expected_type,
                                const uint8_t expected_address[6],
                                uint8_t observed_type,
                                const uint8_t observed_address[6]);

bool ble_bridge_hids_packet_type_allowed(uint8_t packet_type,
                                          uint8_t hci_event_packet_type,
                                          uint8_t gattservice_meta_packet_type);

/* HCI event callbacks use channel zero; in-place GATT-service callbacks use CID. */
bool ble_bridge_hids_callback_route_valid(
    uint8_t packet_type, uint16_t channel, uint16_t hids_cid,
    uint8_t hci_event_packet_type, uint8_t gattservice_meta_packet_type);

/* BTstack event byte one encodes the payload length excluding two-byte header. */
bool ble_bridge_event_frame_valid(const uint8_t *packet, uint16_t size,
                                  uint16_t minimum_size);

#endif
