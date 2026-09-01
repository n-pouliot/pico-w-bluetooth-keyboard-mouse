#include "ble_bridge_policy.h"

#include <string.h>

enum {
    AD_TYPE_UUID16_INCOMPLETE = 0x02,
    AD_TYPE_UUID16_COMPLETE = 0x03,
    AD_TYPE_APPEARANCE = 0x19,
    UUID16_HUMAN_INTERFACE_DEVICE = 0x1812,
    APPEARANCE_GENERIC_HID = 0x03c0,
    APPEARANCE_HID_CATEGORY_MASK = 0xffc0,
    APPEARANCE_HID_KEYBOARD = 0x03c1,
    APPEARANCE_HID_MOUSE = 0x03c2,
};

static ble_appearance_hint_t classify_appearance(uint16_t appearance)
{
    if (appearance == APPEARANCE_GENERIC_HID) {
        return BLE_APPEARANCE_HINT_GENERIC_HID;
    }
    if (appearance == APPEARANCE_HID_KEYBOARD) {
        return BLE_APPEARANCE_HINT_KEYBOARD;
    }
    if (appearance == APPEARANCE_HID_MOUSE) {
        return BLE_APPEARANCE_HINT_MOUSE;
    }
    if ((appearance & APPEARANCE_HID_CATEGORY_MASK) ==
        APPEARANCE_GENERIC_HID) {
        return BLE_APPEARANCE_HINT_UNSUPPORTED_HID;
    }
    return BLE_APPEARANCE_HINT_NONE;
}

static ble_appearance_hint_t merge_appearance(
    ble_appearance_hint_t current, ble_appearance_hint_t observed)
{
    if (observed == BLE_APPEARANCE_HINT_NONE) {
        return current;
    }
    if (current == BLE_APPEARANCE_HINT_NONE || current == observed) {
        return observed;
    }
    return BLE_APPEARANCE_HINT_CONFLICT;
}

bool ble_bridge_parse_advertisement(const uint8_t *data, size_t length,
                                    ble_advertisement_policy_t *out_policy)
{
    size_t offset = 0u;

    if (out_policy == NULL || (data == NULL && length != 0u)) {
        return false;
    }
    memset(out_policy, 0, sizeof(*out_policy));

    while (offset < length) {
        const uint8_t item_length = data[offset];
        size_t item_end;
        uint8_t type;

        if (item_length == 0u) {
            return offset + 1u == length;
        }
        if ((size_t)item_length > length - offset - 1u) {
            return false;
        }
        item_end = offset + 1u + item_length;
        type = data[offset + 1u];

        if (type == AD_TYPE_UUID16_INCOMPLETE ||
            type == AD_TYPE_UUID16_COMPLETE) {
            size_t value_offset = offset + 2u;
            const size_t value_length = (size_t)item_length - 1u;
            if ((value_length & 1u) != 0u) {
                return false;
            }
            while (value_offset < item_end) {
                const uint16_t uuid = (uint16_t)data[value_offset] |
                                      ((uint16_t)data[value_offset + 1u] << 8);
                if (uuid == UUID16_HUMAN_INTERFACE_DEVICE) {
                    out_policy->advertises_hid = true;
                }
                value_offset += 2u;
            }
        } else if (type == AD_TYPE_APPEARANCE) {
            ble_appearance_hint_t observed;
            uint16_t appearance;
            if (item_length != 3u) {
                return false;
            }
            appearance = (uint16_t)data[offset + 2u] |
                         ((uint16_t)data[offset + 3u] << 8);
            observed = classify_appearance(appearance);
            out_policy->appearance = merge_appearance(out_policy->appearance,
                                                       observed);
            if (observed != BLE_APPEARANCE_HINT_NONE) {
                out_policy->advertises_hid = true;
            }
        }
        offset = item_end;
    }
    return true;
}

bool ble_bridge_appearance_allows_role(ble_appearance_hint_t hint,
                                       uint8_t role)
{
    switch (hint) {
    case BLE_APPEARANCE_HINT_NONE:
    case BLE_APPEARANCE_HINT_GENERIC_HID:
        return role == 1u || role == 2u;
    case BLE_APPEARANCE_HINT_KEYBOARD:
        return role == 1u;
    case BLE_APPEARANCE_HINT_MOUSE:
        return role == 2u;
    default:
        return false;
    }
}

bool ble_bridge_address_matches(uint8_t expected_type,
                                const uint8_t expected_address[6],
                                uint8_t observed_type,
                                const uint8_t observed_address[6])
{
    return expected_address != NULL && observed_address != NULL &&
           ((expected_type & 1u) == (observed_type & 1u)) &&
           memcmp(expected_address, observed_address, 6u) == 0;
}

bool ble_bridge_hids_packet_type_allowed(uint8_t packet_type,
                                          uint8_t hci_event_packet_type,
                                          uint8_t gattservice_meta_packet_type)
{
    return packet_type == hci_event_packet_type ||
           packet_type == gattservice_meta_packet_type;
}

bool ble_bridge_hids_callback_route_valid(
    uint8_t packet_type, uint16_t channel, uint16_t hids_cid,
    uint8_t hci_event_packet_type, uint8_t gattservice_meta_packet_type)
{
    if (packet_type == hci_event_packet_type) {
        return channel == 0u;
    }
    if (packet_type == gattservice_meta_packet_type) {
        return hids_cid != 0u && channel == hids_cid;
    }
    return false;
}

bool ble_bridge_event_frame_valid(const uint8_t *packet, uint16_t size,
                                  uint16_t minimum_size)
{
    return packet != NULL && size >= minimum_size && size >= 2u &&
           (uint16_t)((uint16_t)packet[1] + 2u) == size;
}
