/*
 * One-shot maintenance firmware for deliberately clearing role records.
 * The normal bridge firmware never samples BOOTSEL or erases pairings.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"

#include "bt_example_common.h"
#include "bridge_log.h"
#include "pairing_store.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#ifndef CLEAR_ROLE_MASK
#error CLEAR_ROLE_MASK must select keyboard (1), mouse (2), or both (3)
#endif

#define CLEAR_KEYBOARD 1u
#define CLEAR_MOUSE 2u
#define LED_TICK_MS 150u

#define TLV_TAG_XHKB ((((uint32_t)'X') << 24) | (((uint32_t)'H') << 16) | \
                      (((uint32_t)'K') << 8) | (uint32_t)'B')
#define TLV_TAG_XHMS ((((uint32_t)'X') << 24) | (((uint32_t)'H') << 16) | \
                      (((uint32_t)'M') << 8) | (uint32_t)'S')

static btstack_packet_callback_registration_t hci_event_registration;
static btstack_timer_source_t led_timer;
static bool operation_complete;
static bool operation_success;
static uint32_t led_ticks;

static int find_identity(bd_addr_type_t type, const uint8_t address[6]) {
    const int maximum = le_device_db_max_count();
    for (int index = 0; index < maximum; ++index) {
        int stored_type = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t stored_address;
        le_device_db_info(index, &stored_type, stored_address, NULL);
        if ((stored_type != BD_ADDR_TYPE_UNKNOWN) &&
            (((unsigned int)stored_type & 1u) ==
             ((unsigned int)type & 1u)) &&
            (memcmp(stored_address, address, sizeof(bd_addr_t)) == 0)) {
            return index;
        }
    }
    return -1;
}

static bool clear_role(const btstack_tlv_t *tlv, void *context,
                       uint32_t tag, pairing_role_t role) {
    uint8_t bytes[PAIRING_STORE_RECORD_SIZE];
    pairing_role_record_t record;
    const int length = tlv->get_tag(context, tag, bytes, sizeof(bytes));
    if (length == 0) {
        return true;
    }

    const bool record_valid =
        (length == (int)sizeof(bytes)) &&
        (pairing_store_decode(bytes, sizeof(bytes), role, &record) ==
         PAIRING_STORE_OK);

    // Delete authorization first. If power fails before bond removal, an
    // orphan key may remain but normal firmware will not reconnect to it.
    tlv->delete_tag(context, tag);
    if (tlv->get_tag(context, tag, bytes, sizeof(bytes)) != 0) {
        return false;
    }
    if (!record_valid) {
        return true;
    }

    const int db_index = find_identity(
        (bd_addr_type_t)record.address_type, record.identity_address);
    if (db_index >= 0) {
        le_device_db_remove(db_index);
    }
    return true;
}

static void led_timeout(btstack_timer_source_t *timer) {
    bool on = false;
    if (operation_complete && operation_success) {
        on = true;
    } else if (operation_complete) {
        on = (led_ticks & 1u) == 0u;
    } else {
        on = (led_ticks % 8u) == 0u;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    led_ticks++;
    btstack_run_loop_set_timer(timer, LED_TICK_MS);
    btstack_run_loop_add_timer(timer);
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if ((packet_type != HCI_EVENT_PACKET) || operation_complete ||
        (hci_event_packet_get_type(packet) != BTSTACK_EVENT_STATE) ||
        (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)) {
        return;
    }

    const btstack_tlv_t *tlv = NULL;
    void *context = NULL;
    btstack_tlv_get_instance(&tlv, &context);
    bool success = tlv != NULL;
    if ((tlv != NULL) && ((CLEAR_ROLE_MASK & CLEAR_KEYBOARD) != 0u)) {
        const bool role_success = clear_role(
            tlv, context, TLV_TAG_XHKB, PAIRING_ROLE_KEYBOARD);
        success = role_success && success;
    }
    if ((tlv != NULL) && ((CLEAR_ROLE_MASK & CLEAR_MOUSE) != 0u)) {
        const bool role_success = clear_role(
            tlv, context, TLV_TAG_XHMS, PAIRING_ROLE_MOUSE);
        success = role_success && success;
    }

    operation_success = success;
    operation_complete = true;
    SYS_LOG("Maintenance clear %s. Reflash the normal xbox_pico UF2.\n",
            success ? "completed" : "failed");
    hci_power_control(HCI_POWER_OFF);
}

int btstack_main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    l2cap_init();
    sm_init();
    hci_event_registration.callback = packet_handler;
    hci_add_event_handler(&hci_event_registration);
    btstack_run_loop_set_timer_handler(&led_timer, led_timeout);
    btstack_run_loop_set_timer(&led_timer, LED_TICK_MS);
    btstack_run_loop_add_timer(&led_timer);
    hci_power_control(HCI_POWER_ON);
    return 0;
}

int main(void) {
    stdio_init_all();
    if (ble_bridge_bt_example_init() != 0) {
        return 1;
    }
    ble_bridge_bt_example_main();
    btstack_run_loop_execute();
    return 0;
}
