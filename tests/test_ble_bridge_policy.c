#include "ble_bridge_policy.h"

#include <stdio.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,     \
                          __LINE__, #condition);                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_callback_forms_and_frames(void) {
    const uint8_t frame[] = {0xf2u, 0x06u, 0x01u, 0u, 0u, 0u, 0u, 0u};
    CHECK(ble_bridge_hids_packet_type_allowed(0x04u, 0x04u, 0xf2u));
    CHECK(ble_bridge_hids_packet_type_allowed(0xf2u, 0x04u, 0xf2u));
    CHECK(!ble_bridge_hids_packet_type_allowed(0x02u, 0x04u, 0xf2u));
    CHECK(ble_bridge_hids_callback_route_valid(0x04u, 0u, 7u,
                                                0x04u, 0xf2u));
    CHECK(ble_bridge_hids_callback_route_valid(0xf2u, 7u, 7u,
                                                0x04u, 0xf2u));
    CHECK(!ble_bridge_hids_callback_route_valid(0x04u, 7u, 7u,
                                                 0x04u, 0xf2u));
    CHECK(!ble_bridge_hids_callback_route_valid(0xf2u, 0u, 7u,
                                                 0x04u, 0xf2u));
    CHECK(ble_bridge_event_frame_valid(frame, sizeof(frame), sizeof(frame)));
    CHECK(!ble_bridge_event_frame_valid(frame, sizeof(frame) - 1u, 3u));
    CHECK(!ble_bridge_event_frame_valid(frame, sizeof(frame), 9u));
    return 0;
}

static int test_advertisement_appearance_policy(void) {
    const uint8_t keyboard[] = {
        3u, 0x03u, 0x12u, 0x18u,
        3u, 0x19u, 0xc1u, 0x03u,
    };
    const uint8_t generic[] = {3u, 0x19u, 0xc0u, 0x03u};
    const uint8_t unsupported[] = {3u, 0x19u, 0xc4u, 0x03u};
    const uint8_t conflicting[] = {
        3u, 0x19u, 0xc1u, 0x03u,
        3u, 0x19u, 0xc2u, 0x03u,
    };
    const uint8_t malformed[] = {4u, 0x03u, 0x12u};
    ble_advertisement_policy_t policy;

    CHECK(ble_bridge_parse_advertisement(keyboard, sizeof(keyboard), &policy));
    CHECK(policy.advertises_hid);
    CHECK(policy.appearance == BLE_APPEARANCE_HINT_KEYBOARD);
    CHECK(ble_bridge_appearance_allows_role(policy.appearance, 1u));
    CHECK(!ble_bridge_appearance_allows_role(policy.appearance, 2u));

    CHECK(ble_bridge_parse_advertisement(generic, sizeof(generic), &policy));
    CHECK(ble_bridge_appearance_allows_role(policy.appearance, 1u));
    CHECK(ble_bridge_appearance_allows_role(policy.appearance, 2u));

    CHECK(ble_bridge_parse_advertisement(unsupported, sizeof(unsupported),
                                         &policy));
    CHECK(!ble_bridge_appearance_allows_role(policy.appearance, 1u));
    CHECK(!ble_bridge_appearance_allows_role(policy.appearance, 2u));

    CHECK(ble_bridge_parse_advertisement(conflicting, sizeof(conflicting),
                                         &policy));
    CHECK(policy.appearance == BLE_APPEARANCE_HINT_CONFLICT);
    CHECK(!ble_bridge_appearance_allows_role(policy.appearance, 1u));
    CHECK(!ble_bridge_parse_advertisement(malformed, sizeof(malformed),
                                          &policy));
    return 0;
}

static int test_address_matching(void) {
    const uint8_t address[] = {1u, 2u, 3u, 4u, 5u, 6u};
    const uint8_t other[] = {1u, 2u, 3u, 4u, 5u, 7u};
    CHECK(ble_bridge_address_matches(1u, address, 3u, address));
    CHECK(!ble_bridge_address_matches(1u, address, 0u, address));
    CHECK(!ble_bridge_address_matches(1u, address, 1u, other));
    return 0;
}

int test_ble_bridge_policy(void) {
    CHECK(test_callback_forms_and_frames() == 0);
    CHECK(test_advertisement_appearance_policy() == 0);
    CHECK(test_address_matching() == 0);
    return 0;
}
