#ifndef XBOX_PICO_BLE_BRIDGE_POLICY_H
#define XBOX_PICO_BLE_BRIDGE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Public by design: the user types this on a passkey-entry keyboard. */
#define BLE_BRIDGE_PAIRING_PASSKEY UINT32_C(739241)

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

typedef enum {
    BLE_RADIO_ACTION_NONE = 0,
    BLE_RADIO_ACTION_POWER_OFF,
    BLE_RADIO_ACTION_POWER_ON,
} ble_radio_action_t;

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

/* Pure transition policy used by the transport timer and host regression tests. */
ble_radio_action_t ble_bridge_radio_next_action(bool usb_requested,
                                                bool radio_working,
                                                bool transition_pending,
                                                bool restart_required,
                                                bool retry_ready);

/* Authorizes deletion of one exact, newly occupied candidate bond slot. */
bool ble_bridge_bond_removal_allowed(
    bool slot_was_occupied, int expected_index, uint8_t expected_type,
    const uint8_t expected_address[6], int observed_index,
    uint8_t observed_type, const uint8_t observed_address[6]);

/* Authorizes the informational display event for the fixed-code workflow. */
bool ble_bridge_passkey_display_allowed(bool context_securing,
                                        bool enrollment_current,
                                        bool secure_connection,
                                        uint32_t displayed_passkey);

#endif
