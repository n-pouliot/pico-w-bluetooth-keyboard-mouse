/*
 * Dual HID-over-GATT central for xbox-pico.
 *
 * The event-loop structure is derived from BlueKitchen's hog_host_demo.c and
 * remains subject to that file's non-commercial 3-clause BSD-style license.
 * Project-specific state management and HID translation are original work.
 */

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"

#include "bridge.h"
#include "ble_bridge_policy.h"
#include "bt_example_common.h"
#include "bridge_log.h"
#include "bridge_mailbox.h"
#include "hid_report_parser.h"
#include "pairing_store.h"
#include "pico/cyw43_arch.h"

#define DEVICE_CONTEXT_COUNT 2u
#define SHARED_REPORT_MAP_STORAGE_SIZE 4096u
#define ENROLLMENT_WINDOW_SECONDS 180u
#define ENROLLMENT_WINDOW_MS (ENROLLMENT_WINDOW_SECONDS * 1000u)
#define CONNECT_TIMEOUT_MS 10000u
#define SECURITY_TIMEOUT_MS 30000u
#define DISCOVERY_TIMEOUT_MS 45000u
#define ADDRESS_RESOLUTION_TIMEOUT_MS 3000u
#define DISCONNECT_TIMEOUT_MS 5000u
#define RETRY_BACKOFF_MS 1000u
#define MANAGER_RETRY_MS 250u
#define LED_TICK_MS 100u
#define TRANSPORT_TICK_MS 5u
#define FAILURE_DISPLAY_MS 20000u

#define BLE_SCAN_TYPE_ACTIVE 1u
#define BLE_SCAN_INTERVAL_UNITS 48u
#define BLE_SCAN_WINDOW_UNITS 48u

#define CONN_INTERVAL_MIN_UNITS 10u
#define CONN_INTERVAL_MAX_UNITS 12u
#define CONN_LATENCY_EVENTS 0u
#define CONN_TIMEOUT_UNITS 300u

#define BLE_COMPAT_MIN_KEY_SIZE 7
#define BLE_MAX_KEY_SIZE 16

#define TLV_TAG_XHKB ((((uint32_t)'X') << 24) | (((uint32_t)'H') << 16) | \
                      (((uint32_t)'K') << 8) | (uint32_t)'B')
#define TLV_TAG_XHMS ((((uint32_t)'X') << 24) | (((uint32_t)'H') << 16) | \
                      (((uint32_t)'M') << 8) | (uint32_t)'S')

typedef enum {
    DEVICE_IDLE = 0,
    DEVICE_CONNECTING,
    DEVICE_CONNECT_CANCELING,
    DEVICE_SECURING,
    DEVICE_PREFLIGHTING_HIDS,
    DEVICE_DISCOVERING,
    DEVICE_READY,
    DEVICE_DISCONNECTING,
    DEVICE_REPAIR_REQUIRED,
} device_state_t;

typedef struct {
    device_state_t state;
    hid_report_role_t role;
    bool enrollment;
    bool identity_valid;
    bool fixed_passkey_displayed;
    bool new_bond_uncommitted;
    bool enrollment_db_snapshot_valid;
    ble_appearance_hint_t appearance_hint;
    bd_addr_t target_address;
    bd_addr_type_t target_address_type;
    bd_addr_t identity_address;
    bd_addr_type_t identity_address_type;
    int device_db_index;
    int new_bond_db_index;
    bd_addr_t new_bond_identity;
    bd_addr_type_t new_bond_identity_type;
    uint32_t device_db_occupied_before;
    hci_con_handle_t connection_handle;
    uint16_t hids_cid;
    uint16_t attempt_token;
    uint8_t hids_service_count;
    uint8_t disconnect_retries;
    uint32_t generation;
    uint32_t retry_after_ms;
    device_state_t timer_state;
    uint16_t timer_attempt_token;
    btstack_timer_source_t operation_timer;
    hid_report_plan_t report_plan;
    hid_report_runtime_t report_runtime;
} device_context_t;

typedef struct {
    bool present;
    bool usable;
    pairing_role_record_t record;
    int device_db_index;
} saved_role_t;

typedef struct {
    bool pending;
    bool advertisement_has_hid;
    bd_addr_t address;
    bd_addr_type_t address_type;
    ble_appearance_hint_t appearance_hint;
    uint16_t token;
} resolution_request_t;

static device_context_t devices[DEVICE_CONTEXT_COUNT];
static saved_role_t saved_roles[DEVICE_CONTEXT_COUNT];
static uint8_t report_map_storage[SHARED_REPORT_MAP_STORAGE_SIZE];

static const btstack_tlv_t *tlv_impl;
static void *tlv_context;
static btstack_packet_callback_registration_t hci_event_registration;
static btstack_packet_callback_registration_t sm_event_registration;
static btstack_timer_source_t manager_timer;
static btstack_timer_source_t led_timer;
static btstack_timer_source_t transport_timer;
static btstack_timer_source_t resolution_timer;
static resolution_request_t resolution_request;

static int8_t active_operation = -1;
static uint16_t next_attempt_token;
static uint32_t resolution_retry_after_ms;
static bool scan_active;
static bool manager_timer_active;
static bool enrollment_open;
static bool enrollment_window_started;
static uint32_t enrollment_deadline_ms;
static uint32_t led_ticks;
static ble_failure_stage_t failure_stage;
static uint32_t failure_led_start_tick;
static uint32_t failure_display_until_ms;
static bool radio_working;
static bool radio_transition_pending;
static bool radio_restart_required;
static uint32_t radio_retry_after_ms;
static bool usb_transport_requested;

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size);
static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size);
static void hids_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size);
static void hids_preflight_packet_handler(uint8_t packet_type,
                                          uint16_t channel,
                                          uint8_t *packet, uint16_t size);
static void drive_connection_manager(void);
static void operation_timeout(btstack_timer_source_t *timer);
static void resolution_timeout(btstack_timer_source_t *timer);
static void manager_timeout(btstack_timer_source_t *timer);
static void led_timeout(btstack_timer_source_t *timer);
static void transport_timeout(btstack_timer_source_t *timer);
static void discard_uncommitted_bond(device_context_t *context);
static void quiesce_radio(void);

static ble_failure_stage_t failure_stage_for_state(device_state_t state) {
    switch (state) {
        case DEVICE_CONNECTING:
        case DEVICE_CONNECT_CANCELING:
            return BLE_FAILURE_STAGE_CONNECTION;
        case DEVICE_SECURING:
            return BLE_FAILURE_STAGE_SECURITY;
        case DEVICE_PREFLIGHTING_HIDS:
            return BLE_FAILURE_STAGE_HIDS_SERVICE;
        case DEVICE_DISCOVERING:
            return BLE_FAILURE_STAGE_REPORT_MAP;
        case DEVICE_READY:
            return BLE_FAILURE_STAGE_RUNTIME_REPORT;
        default:
            return BLE_FAILURE_STAGE_INTERNAL;
    }
}

static void record_failure(device_state_t state) {
    failure_stage = failure_stage_for_state(state);
    failure_led_start_tick = led_ticks;
    failure_display_until_ms =
        btstack_run_loop_get_time_ms() + FAILURE_DISPLAY_MS;
    BLE_LOG("Failure indicator stage %u\n", (unsigned int)failure_stage);
}

static uint32_t role_tag(hid_report_role_t role) {
    return role == HID_REPORT_ROLE_KEYBOARD ? TLV_TAG_XHKB : TLV_TAG_XHMS;
}

static pairing_role_t pairing_role(hid_report_role_t role) {
    return role == HID_REPORT_ROLE_KEYBOARD ? PAIRING_ROLE_KEYBOARD
                                            : PAIRING_ROLE_MOUSE;
}

static bridge_role_t mailbox_role(hid_report_role_t role) {
    return role == HID_REPORT_ROLE_KEYBOARD ? BRIDGE_ROLE_KEYBOARD
                                            : BRIDGE_ROLE_MOUSE;
}

static unsigned int role_index(hid_report_role_t role) {
    return role == HID_REPORT_ROLE_KEYBOARD ? 0u : 1u;
}

static const char *role_name(hid_report_role_t role) {
    switch (role) {
        case HID_REPORT_ROLE_KEYBOARD:
            return "keyboard";
        case HID_REPORT_ROLE_MOUSE:
            return "mouse";
        default:
            return "candidate";
    }
}

static bool deadline_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static bool address_is_rpa(bd_addr_type_t type, const bd_addr_t address) {
    return ((type & 1u) == BD_ADDR_TYPE_LE_RANDOM) &&
           ((address[0] & 0xc0u) == 0x40u);
}

static bool address_equal(bd_addr_type_t lhs_type, const bd_addr_t lhs,
                          bd_addr_type_t rhs_type, const bd_addr_t rhs) {
    return ((lhs_type & 1u) == (rhs_type & 1u)) &&
           (memcmp(lhs, rhs, sizeof(bd_addr_t)) == 0);
}

static bool advertisement_policy(const uint8_t *packet, uint16_t packet_size,
                                 ble_advertisement_policy_t *policy) {
    const uint8_t *data = gap_event_advertising_report_get_data(packet);
    const uint8_t length = gap_event_advertising_report_get_data_length(packet);
    if (packet_size < 12u || (uint16_t)length != packet_size - 12u) {
        return false;
    }
    return ble_bridge_parse_advertisement(data, length, policy);
}

static device_context_t *context_by_handle(hci_con_handle_t handle) {
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if ((devices[i].connection_handle == handle) &&
            (handle != HCI_CON_HANDLE_INVALID)) {
            return &devices[i];
        }
    }
    return NULL;
}

static device_context_t *context_by_cid(uint16_t cid) {
    if (cid == 0u) {
        return NULL;
    }
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if (devices[i].hids_cid == cid) {
            return &devices[i];
        }
    }
    return NULL;
}

static int context_index(const device_context_t *context) {
    if ((context < &devices[0]) ||
        (context >= &devices[DEVICE_CONTEXT_COUNT])) {
        return -1;
    }
    return (int)(context - &devices[0]);
}

static device_context_t *context_for_role(hid_report_role_t role) {
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if (devices[i].role == role) {
            return &devices[i];
        }
    }
    return NULL;
}

static device_context_t *candidate_context(uint32_t now) {
    if (enrollment_open && deadline_reached(now, enrollment_deadline_ms)) {
        enrollment_open = false;
        BLE_LOG("First-run enrollment window closed\n");
    }
    if (!enrollment_open) {
        return NULL;
    }
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if ((devices[i].role == HID_REPORT_ROLE_NONE) &&
            (devices[i].state == DEVICE_IDLE) &&
            deadline_reached(now, devices[i].retry_after_ms)) {
            return &devices[i];
        }
    }
    return NULL;
}

static bool enrollment_is_current(device_context_t *context) {
    const uint32_t now = btstack_run_loop_get_time_ms();
    if (context == NULL || !context->enrollment || !enrollment_open ||
        deadline_reached(now, enrollment_deadline_ms)) {
        enrollment_open = enrollment_open &&
                          !deadline_reached(now, enrollment_deadline_ms);
        if (context != NULL) {
            context->enrollment = false;
        }
        return false;
    }
    return true;
}

static uint16_t allocate_attempt_token(void) {
    next_attempt_token++;
    if (next_attempt_token == 0u) {
        next_attempt_token = 1u;
    }
    return next_attempt_token;
}

static void cancel_operation_timer(device_context_t *context) {
    if (context == NULL) {
        return;
    }
    (void)btstack_run_loop_remove_timer(&context->operation_timer);
    context->timer_state = DEVICE_IDLE;
    context->timer_attempt_token = 0u;
}

static void arm_operation_timer(device_context_t *context,
                                uint32_t timeout_ms) {
    cancel_operation_timer(context);
    context->timer_state = context->state;
    context->timer_attempt_token = context->attempt_token;
    btstack_run_loop_set_timer_handler(&context->operation_timer,
                                       operation_timeout);
    btstack_run_loop_set_timer_context(&context->operation_timer, context);
    btstack_run_loop_set_timer(&context->operation_timer, timeout_ms);
    btstack_run_loop_add_timer(&context->operation_timer);
}

static void schedule_manager(uint32_t delay_ms) {
    if (manager_timer_active) {
        return;
    }
    manager_timer_active = true;
    btstack_run_loop_set_timer_handler(&manager_timer, manager_timeout);
    btstack_run_loop_set_timer(&manager_timer, delay_ms);
    btstack_run_loop_add_timer(&manager_timer);
}

static void release_device(device_context_t *context) {
    if ((context->generation != 0u) &&
        (context->role != HID_REPORT_ROLE_NONE)) {
        bridge_role_release(mailbox_role(context->role), context->generation);
        context->generation = 0u;
    }
    memset(&context->report_runtime, 0, sizeof(context->report_runtime));
}

static void finish_disconnected(device_context_t *context) {
    const int index = context_index(context);
    cancel_operation_timer(context);
    release_device(context);
    discard_uncommitted_bond(context);
    context->connection_handle = HCI_CON_HANDLE_INVALID;
    context->hids_cid = 0u;
    context->enrollment = false;
    context->identity_valid = false;
    context->fixed_passkey_displayed = false;
    context->device_db_index = -1;
    context->new_bond_db_index = -1;
    context->enrollment_db_snapshot_valid = false;
    context->appearance_hint = BLE_APPEARANCE_HINT_NONE;
    context->hids_service_count = 0u;
    context->disconnect_retries = 0u;
    context->state = DEVICE_IDLE;
    const uint32_t now = btstack_run_loop_get_time_ms();
    context->retry_after_ms = now + RETRY_BACKOFF_MS;
    if (failure_stage != BLE_FAILURE_STAGE_NONE &&
        !deadline_reached(context->retry_after_ms,
                          failure_display_until_ms)) {
        context->retry_after_ms = failure_display_until_ms;
    }
    if ((index >= 0) && (active_operation == index)) {
        active_operation = -1;
    }
    schedule_manager(RETRY_BACKOFF_MS);
}

static void disconnect_device(device_context_t *context, const char *reason) {
    if ((context == NULL) || (context->state == DEVICE_DISCONNECTING)) {
        return;
    }
    BLE_LOG("Rejecting %s: %s\n", role_name(context->role), reason);
    record_failure(context->state);
    release_device(context);
    discard_uncommitted_bond(context);
    context->state = DEVICE_DISCONNECTING;
    context->disconnect_retries = 0u;
    cancel_operation_timer(context);
    if (context->connection_handle == HCI_CON_HANDLE_INVALID) {
        finish_disconnected(context);
        return;
    }
    (void)gap_disconnect(context->connection_handle);
    arm_operation_timer(context, DISCONNECT_TIMEOUT_MS);
}

static int find_device_db_identity(bd_addr_type_t type,
                                   const bd_addr_t identity) {
    const int maximum = le_device_db_max_count();
    for (int index = 0; index < maximum; ++index) {
        int stored_type = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t stored_address;
        le_device_db_info(index, &stored_type, stored_address, NULL);
        if ((stored_type != BD_ADDR_TYPE_UNKNOWN) &&
            address_equal((bd_addr_type_t)stored_type, stored_address,
                          type, identity)) {
            return index;
        }
    }
    return -1;
}

static uint32_t device_db_occupied_mask(void) {
    const int maximum = le_device_db_max_count();
    uint32_t mask = 0u;
    for (int index = 0; index < maximum && index < 32; ++index) {
        int stored_type = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t stored_address;
        le_device_db_info(index, &stored_type, stored_address, NULL);
        if (stored_type != BD_ADDR_TYPE_UNKNOWN) {
            mask |= UINT32_C(1) << (unsigned int)index;
        }
    }
    return mask;
}

static bool device_db_has_free_slot(uint32_t occupied_mask) {
    const int maximum = le_device_db_max_count();
    if (maximum <= 0 || maximum > 32) {
        return false;
    }
    const uint32_t all_slots = maximum == 32 ? UINT32_MAX :
        (UINT32_C(1) << (unsigned int)maximum) - UINT32_C(1);
    return (occupied_mask & all_slots) != all_slots;
}

static void discard_uncommitted_bond(device_context_t *context) {
    if (context == NULL || !context->enrollment_db_snapshot_valid) {
        return;
    }

    int candidate_index = -1;
    bd_addr_type_t candidate_type = BD_ADDR_TYPE_UNKNOWN;
    bd_addr_t candidate_address;
    if (context->new_bond_uncommitted) {
        candidate_index = context->new_bond_db_index;
        candidate_type = context->new_bond_identity_type;
        memcpy(candidate_address, context->new_bond_identity,
               sizeof(candidate_address));
    } else if (context->identity_valid) {
        /* Bounded fallback for stacks that omit SM_EVENT_IDENTITY_CREATED. */
        candidate_index = find_device_db_identity(
            context->identity_address_type, context->identity_address);
        candidate_type = context->identity_address_type;
        memcpy(candidate_address, context->identity_address,
               sizeof(candidate_address));
    }

    const int maximum = le_device_db_max_count();
    if (candidate_index >= 0 && candidate_index < maximum &&
        candidate_index < 32) {
        int stored_type = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t stored_address;
        le_device_db_info(candidate_index, &stored_type, stored_address, NULL);
        const bool slot_was_occupied =
            (context->device_db_occupied_before &
             (UINT32_C(1) << (unsigned int)candidate_index)) != 0u;
        if (stored_type != BD_ADDR_TYPE_UNKNOWN &&
            ble_bridge_bond_removal_allowed(
                slot_was_occupied, candidate_index, (uint8_t)candidate_type,
                candidate_address, candidate_index, (uint8_t)stored_type,
                stored_address)) {
            BLE_LOG("Removing exact rejected candidate bond at DB index %d\n",
                    candidate_index);
            le_device_db_remove(candidate_index);
            stored_type = BD_ADDR_TYPE_UNKNOWN;
            le_device_db_info(candidate_index, &stored_type, stored_address,
                              NULL);
            if (stored_type != BD_ADDR_TYPE_UNKNOWN) {
                BLE_LOG("Candidate bond removal verification failed at DB index %d\n",
                        candidate_index);
            }
        } else if (stored_type != BD_ADDR_TYPE_UNKNOWN) {
            BLE_LOG("Rejected candidate bond no longer matches DB index %d; preserving slot\n",
                    candidate_index);
        }
    }
    context->new_bond_uncommitted = false;
    context->new_bond_db_index = -1;
}

static bool device_db_security_ok(int index, hid_report_role_t role) {
    if (index < 0) {
        return false;
    }
    int key_size = 0;
    int authenticated = 0;
    int secure_connection = 0;
    le_device_db_encryption_get(index, NULL, NULL, NULL, &key_size,
                                &authenticated, NULL, &secure_connection);
    if (role != HID_REPORT_ROLE_KEYBOARD &&
        role != HID_REPORT_ROLE_MOUSE) {
        return false;
    }
    return ble_bridge_bond_security_allowed(
        role == HID_REPORT_ROLE_KEYBOARD, key_size, authenticated != 0,
        secure_connection != 0);
}

static bool device_db_authenticated(int index) {
    if (index < 0) {
        return false;
    }
    int authenticated = 0;
    le_device_db_encryption_get(index, NULL, NULL, NULL, NULL,
                                &authenticated, NULL, NULL);
    return authenticated != 0;
}

static bool context_security_ok(device_context_t *context,
                                hid_report_role_t role) {
    if (!context->identity_valid) {
        return false;
    }
    context->device_db_index =
        find_device_db_identity(context->identity_address_type,
                                context->identity_address);
    return device_db_security_ok(context->device_db_index, role);
}

static void initialize_device_contexts(void) {
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        cancel_operation_timer(&devices[i]);
    }
    memset(devices, 0, sizeof(devices));
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        devices[i].connection_handle = HCI_CON_HANDLE_INVALID;
        devices[i].device_db_index = -1;
        devices[i].new_bond_db_index = -1;
    }
}

static void load_saved_role(hid_report_role_t role) {
    const unsigned int index = role_index(role);
    saved_role_t *saved = &saved_roles[index];
    device_context_t *context = &devices[index];
    uint8_t bytes[PAIRING_STORE_RECORD_SIZE];
    memset(saved, 0, sizeof(*saved));
    saved->device_db_index = -1;

    if (tlv_impl == NULL) {
        context->role = role;
        context->state = DEVICE_REPAIR_REQUIRED;
        BLE_LOG("%s role cannot load: TLV unavailable\n", role_name(role));
        return;
    }

    const int length = tlv_impl->get_tag(tlv_context, role_tag(role), bytes,
                                         sizeof(bytes));
    if (length == 0) {
        return;
    }
    saved->present = true;
    context->role = role;
    if ((length != (int)sizeof(bytes)) ||
        (pairing_store_decode(bytes, sizeof(bytes), pairing_role(role),
                              &saved->record) != PAIRING_STORE_OK)) {
        context->state = DEVICE_REPAIR_REQUIRED;
        BLE_LOG("%s role record is corrupt; maintenance clear required\n",
                role_name(role));
        return;
    }

    saved->device_db_index = find_device_db_identity(
        (bd_addr_type_t)saved->record.address_type,
        saved->record.identity_address);
    if (!device_db_security_ok(saved->device_db_index, role)) {
        context->state = DEVICE_REPAIR_REQUIRED;
        BLE_LOG("%s role has no acceptable bond security record\n",
                role_name(role));
        return;
    }

    saved->usable = true;
    context->identity_valid = true;
    context->identity_address_type =
        (bd_addr_type_t)saved->record.address_type;
    memcpy(context->identity_address, saved->record.identity_address,
           sizeof(bd_addr_t));
    context->device_db_index = saved->device_db_index;
    BLE_LOG("Loaded %s identity %s\n", role_name(role),
            bd_addr_to_str(context->identity_address));
}

static bool store_enrolled_role(device_context_t *context,
                                hid_report_role_t role) {
    const unsigned int index = role_index(role);
    pairing_role_record_t record = {
        .role = pairing_role(role),
        .address_type = (pairing_address_type_t)(context->identity_address_type & 1u),
    };
    uint8_t encoded[PAIRING_STORE_RECORD_SIZE];
    uint8_t verify[PAIRING_STORE_RECORD_SIZE];
    pairing_role_record_t decoded;
    memcpy(record.identity_address, context->identity_address,
           sizeof(record.identity_address));

    if ((tlv_impl == NULL) || saved_roles[index].present ||
        (pairing_store_encode(&record, encoded) != PAIRING_STORE_OK)) {
        return false;
    }
    if (tlv_impl->store_tag(tlv_context, role_tag(role), encoded,
                            sizeof(encoded)) != 0) {
        return false;
    }
    const int length = tlv_impl->get_tag(tlv_context, role_tag(role), verify,
                                         sizeof(verify));
    if ((length != (int)sizeof(verify)) ||
        (pairing_store_decode(verify, sizeof(verify), pairing_role(role),
                              &decoded) != PAIRING_STORE_OK) ||
        (memcmp(decoded.identity_address, record.identity_address,
                sizeof(record.identity_address)) != 0)) {
        tlv_impl->delete_tag(tlv_context, role_tag(role));
        return false;
    }

    saved_roles[index].present = true;
    saved_roles[index].usable = true;
    saved_roles[index].record = record;
    saved_roles[index].device_db_index = context->device_db_index;
    context->new_bond_uncommitted = false;
    context->new_bond_db_index = -1;
    context->enrollment_db_snapshot_valid = false;
    BLE_LOG("Persisted enrolled %s identity %s\n", role_name(role),
            bd_addr_to_str(context->identity_address));
    return true;
}

static bool known_role_for_identity(bd_addr_type_t type,
                                    const bd_addr_t identity,
                                    hid_report_role_t *role) {
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if (!saved_roles[i].usable) {
            continue;
        }
        if (address_equal((bd_addr_type_t)saved_roles[i].record.address_type,
                          saved_roles[i].record.identity_address,
                          type, identity)) {
            *role = i == 0u ? HID_REPORT_ROLE_KEYBOARD
                            : HID_REPORT_ROLE_MOUSE;
            return true;
        }
    }
    return false;
}

static bool context_can_connect(const device_context_t *context, uint32_t now) {
    return (context != NULL) && (context->state == DEVICE_IDLE) &&
           deadline_reached(now, context->retry_after_ms);
}

static void cancel_resolution_request(void) {
    (void)btstack_run_loop_remove_timer(&resolution_timer);
    resolution_request.pending = false;
}

static void start_connection(device_context_t *context,
                             bd_addr_type_t address_type,
                             const bd_addr_t address, bool enrollment,
                             ble_appearance_hint_t appearance_hint) {
    const int index = context_index(context);
    if ((index < 0) || (active_operation >= 0)) {
        return;
    }

    if (scan_active) {
        gap_stop_scan();
        scan_active = false;
    }
    cancel_resolution_request();
    memcpy(context->target_address, address, sizeof(bd_addr_t));
    context->target_address_type = (bd_addr_type_t)(address_type & 1u);
    context->enrollment = enrollment;
    context->appearance_hint = appearance_hint;
    context->state = DEVICE_CONNECTING;
    context->connection_handle = HCI_CON_HANDLE_INVALID;
    context->hids_cid = 0u;
    context->attempt_token = allocate_attempt_token();
    context->fixed_passkey_displayed = false;
    context->new_bond_uncommitted = false;
    context->new_bond_db_index = -1;
    context->enrollment_db_snapshot_valid = false;
    active_operation = (int8_t)index;

    BLE_LOG("Connecting %s to %s\n", role_name(context->role),
            bd_addr_to_str(address));
    const uint8_t status = gap_connect(address, context->target_address_type);
    if (status != ERROR_CODE_SUCCESS) {
        BLE_LOG("gap_connect rejected request, status 0x%02x\n", status);
        finish_disconnected(context);
        return;
    }
    arm_operation_timer(context, CONNECT_TIMEOUT_MS);
}

static void handle_advertisement(const uint8_t *packet, uint16_t size) {
    if ((active_operation >= 0) || !scan_active) {
        return;
    }

    const uint32_t now = btstack_run_loop_get_time_ms();
    bd_addr_t address;
    ble_advertisement_policy_t policy;
    gap_event_advertising_report_get_address(packet, address);
    const bd_addr_type_t type = (bd_addr_type_t)(
        gap_event_advertising_report_get_address_type(packet) & 1u);
    if (!advertisement_policy(packet, size, &policy)) {
        return;
    }

    if (!address_is_rpa(type, address)) {
        hid_report_role_t known_role = HID_REPORT_ROLE_NONE;
        if (known_role_for_identity(type, address, &known_role)) {
            device_context_t *context = context_for_role(known_role);
            if (context_can_connect(context, now)) {
                start_connection(context, type, address, false,
                                 policy.appearance);
            }
            return;
        }
        device_context_t *context = candidate_context(now);
        if (policy.advertises_hid && (context != NULL)) {
            start_connection(context, type, address, true, policy.appearance);
        }
        return;
    }

    if (!resolution_request.pending &&
        deadline_reached(now, resolution_retry_after_ms)) {
        bool known_role_waiting = false;
        for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
            if (saved_roles[i].usable &&
                context_can_connect(context_for_role(
                    i == 0u ? HID_REPORT_ROLE_KEYBOARD : HID_REPORT_ROLE_MOUSE),
                    now)) {
                known_role_waiting = true;
            }
        }
        if (known_role_waiting) {
            resolution_request.advertisement_has_hid = policy.advertises_hid;
            resolution_request.address_type = type;
            resolution_request.appearance_hint = policy.appearance;
            memcpy(resolution_request.address, address, sizeof(bd_addr_t));
            const int status = sm_address_resolution_lookup(type, address);
            if (status != ERROR_CODE_SUCCESS) {
                BLE_LOG("Address resolution could not start, status 0x%02x\n",
                        status);
                resolution_retry_after_ms = now + RETRY_BACKOFF_MS;
                schedule_manager(RETRY_BACKOFF_MS);
                return;
            }
            resolution_request.pending = true;
            resolution_request.token = allocate_attempt_token();
            btstack_run_loop_set_timer_handler(&resolution_timer,
                                               resolution_timeout);
            btstack_run_loop_set_timer(&resolution_timer,
                                       ADDRESS_RESOLUTION_TIMEOUT_MS);
            btstack_run_loop_add_timer(&resolution_timer);
            return;
        }
    }

    device_context_t *context = candidate_context(now);
    if (policy.advertises_hid && (context != NULL) &&
        !resolution_request.pending) {
        start_connection(context, type, address, true, policy.appearance);
    }
}

static void handle_resolution_success(const uint8_t *packet) {
    if (!resolution_request.pending) {
        return;
    }
    bd_addr_t observed;
    sm_event_identity_resolving_succeeded_get_address(packet, observed);
    const bd_addr_type_t observed_type = (bd_addr_type_t)(
        sm_event_identity_resolving_succeeded_get_addr_type(packet) & 1u);
    if (!address_equal(observed_type, observed,
                       resolution_request.address_type,
                       resolution_request.address)) {
        return;
    }

    bd_addr_t identity;
    sm_event_identity_resolving_succeeded_get_identity_address(packet, identity);
    const bd_addr_type_t identity_type = (bd_addr_type_t)(
        sm_event_identity_resolving_succeeded_get_identity_addr_type(packet) & 1u);
    const bd_addr_type_t request_type = resolution_request.address_type;
    const bool advertised_hid = resolution_request.advertisement_has_hid;
    const ble_appearance_hint_t appearance_hint =
        resolution_request.appearance_hint;
    cancel_resolution_request();

    hid_report_role_t role = HID_REPORT_ROLE_NONE;
    if (known_role_for_identity(identity_type, identity, &role)) {
        device_context_t *context = context_for_role(role);
        if (context_can_connect(context, btstack_run_loop_get_time_ms())) {
            memcpy(context->identity_address, identity, sizeof(bd_addr_t));
            context->identity_address_type = identity_type;
            context->identity_valid = true;
            start_connection(context, request_type, observed, false,
                             appearance_hint);
        }
        return;
    }

    device_context_t *context = candidate_context(btstack_run_loop_get_time_ms());
    if (advertised_hid && (context != NULL)) {
        memcpy(context->identity_address, identity, sizeof(bd_addr_t));
        context->identity_address_type = identity_type;
        context->identity_valid = true;
        start_connection(context, request_type, observed, true,
                         appearance_hint);
    }
}

static void handle_resolution_failure(const uint8_t *packet) {
    if (!resolution_request.pending) {
        return;
    }
    bd_addr_t observed;
    sm_event_identity_resolving_failed_get_address(packet, observed);
    const bd_addr_type_t observed_type = (bd_addr_type_t)(
        sm_event_identity_resolving_failed_get_addr_type(packet) & 1u);
    if (!address_equal(observed_type, observed,
                       resolution_request.address_type,
                       resolution_request.address)) {
        return;
    }
    const bd_addr_type_t request_type = resolution_request.address_type;
    const bool advertised_hid = resolution_request.advertisement_has_hid;
    const ble_appearance_hint_t appearance_hint =
        resolution_request.appearance_hint;
    cancel_resolution_request();
    device_context_t *context = candidate_context(btstack_run_loop_get_time_ms());
    if (advertised_hid && (context != NULL)) {
        start_connection(context, request_type, observed, true,
                         appearance_hint);
    }
}

static void begin_hids_client_discovery(device_context_t *context) {
    context->state = DEVICE_DISCOVERING;
    const uint8_t status = hids_client_connect(
        context->connection_handle, hids_packet_handler,
        HID_PROTOCOL_MODE_REPORT, &context->hids_cid);
    if (status != ERROR_CODE_SUCCESS) {
        disconnect_device(context, "HIDS discovery could not start");
        return;
    }
    arm_operation_timer(context, DISCOVERY_TIMEOUT_MS);
}

static void begin_hids_preflight(device_context_t *context) {
    const hid_report_role_t security_role =
        context->role == HID_REPORT_ROLE_KEYBOARD
            ? HID_REPORT_ROLE_KEYBOARD
            : HID_REPORT_ROLE_MOUSE;
    if (!context_security_ok(context, security_role)) {
        disconnect_device(context,
                          "bond does not meet role security requirements");
        return;
    }
    (void)gap_request_connection_parameter_update(
        context->connection_handle, CONN_INTERVAL_MIN_UNITS,
        CONN_INTERVAL_MAX_UNITS, CONN_LATENCY_EVENTS, CONN_TIMEOUT_UNITS);
    context->state = DEVICE_PREFLIGHTING_HIDS;
    context->hids_service_count = 0u;
    const int index = context_index(context);
    const uint8_t status =
        gatt_client_discover_primary_services_by_uuid16_with_context(
            hids_preflight_packet_handler, context->connection_handle,
            ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE,
            context->attempt_token, (uint16_t)(index + 1));
    if (status != ERROR_CODE_SUCCESS) {
        disconnect_device(context, "HIDS service preflight could not start");
        return;
    }
    arm_operation_timer(context, DISCOVERY_TIMEOUT_MS);
}

static void handle_connection_complete(const uint8_t *packet) {
    const uint8_t status = gap_subevent_le_connection_complete_get_status(packet);
    const hci_con_handle_t handle =
        gap_subevent_le_connection_complete_get_connection_handle(packet);
    if (active_operation < 0) {
        if (status == ERROR_CODE_SUCCESS) {
            (void)gap_disconnect(handle);
        }
        return;
    }

    device_context_t *context = &devices[(unsigned int)active_operation];
    bd_addr_t peer_address;
    bd_addr_t peer_rpa;
    const bd_addr_type_t peer_type = (bd_addr_type_t)(
        gap_subevent_le_connection_complete_get_peer_address_type(packet) & 1u);
    gap_subevent_le_connection_complete_get_peer_address(packet, peer_address);
    bool target_matches = address_equal(context->target_address_type,
                                        context->target_address,
                                        peer_type, peer_address);
    if (!target_matches &&
        address_is_rpa(context->target_address_type, context->target_address)) {
        gap_subevent_le_connection_complete_get_peer_resolvable_private_address(
            packet, peer_rpa);
        target_matches = address_equal(context->target_address_type,
                                       context->target_address,
                                       BD_ADDR_TYPE_LE_RANDOM, peer_rpa);
    }
    if (!target_matches ||
        (context->state != DEVICE_CONNECTING &&
         context->state != DEVICE_CONNECT_CANCELING)) {
        if (status == ERROR_CODE_SUCCESS) {
            (void)gap_disconnect(handle);
        }
        return;
    }

    cancel_operation_timer(context);
    if (context->state == DEVICE_CONNECT_CANCELING) {
        if (status == ERROR_CODE_SUCCESS) {
            context->connection_handle = handle;
            context->state = DEVICE_DISCONNECTING;
            context->disconnect_retries = 0u;
            (void)gap_disconnect(handle);
            arm_operation_timer(context, DISCONNECT_TIMEOUT_MS);
        } else {
            finish_disconnected(context);
        }
        return;
    }
    if (status != ERROR_CODE_SUCCESS) {
        BLE_LOG("Connection attempt %u failed, status 0x%02x\n",
                context->attempt_token, status);
        finish_disconnected(context);
        return;
    }

    context->connection_handle = handle;
    context->state = DEVICE_SECURING;
    if (!context->identity_valid &&
        !address_is_rpa(context->target_address_type,
                        context->target_address)) {
        memcpy(context->identity_address, context->target_address,
               sizeof(bd_addr_t));
        context->identity_address_type = context->target_address_type;
        context->identity_valid = true;
    }
    if (context->enrollment) {
        context->device_db_occupied_before = device_db_occupied_mask();
        context->enrollment_db_snapshot_valid = true;
        if (!device_db_has_free_slot(context->device_db_occupied_before)) {
            disconnect_device(
                context,
                "LE bond database is full; valid bonds are protected");
            return;
        }
    }
    sm_request_pairing(handle);
    arm_operation_timer(context, SECURITY_TIMEOUT_MS);
}

static void handle_disconnection(const uint8_t *packet) {
    const hci_con_handle_t handle =
        hci_event_disconnection_complete_get_connection_handle(packet);
    device_context_t *context = context_by_handle(handle);
    if (context == NULL) {
        return;
    }
    BLE_LOG("%s disconnected, reason 0x%02x\n", role_name(context->role),
            hci_event_disconnection_complete_get_reason(packet));
    finish_disconnected(context);
}

static void handle_identity_created(const uint8_t *packet) {
    device_context_t *context = context_by_handle(
        sm_event_identity_created_get_handle(packet));
    if (context == NULL) {
        return;
    }
    sm_event_identity_created_get_identity_address(packet,
                                                   context->identity_address);
    context->identity_address_type = (bd_addr_type_t)(
        sm_event_identity_created_get_identity_addr_type(packet) & 1u);
    context->identity_valid = true;
    context->device_db_index = sm_event_identity_created_get_index(packet);
    if (context->enrollment && context->enrollment_db_snapshot_valid &&
        context->device_db_index >= 0 &&
        context->device_db_index < 32 &&
        (context->device_db_occupied_before &
         (UINT32_C(1) << (unsigned int)context->device_db_index)) == 0u) {
        context->new_bond_uncommitted = true;
        context->new_bond_db_index = context->device_db_index;
        context->new_bond_identity_type = context->identity_address_type;
        memcpy(context->new_bond_identity, context->identity_address,
               sizeof(bd_addr_t));
    }
}

static void handle_pairing_complete(const uint8_t *packet) {
    device_context_t *context = context_by_handle(
        sm_event_pairing_complete_get_handle(packet));
    if ((context == NULL) || (context->state != DEVICE_SECURING)) {
        return;
    }
    const uint8_t status = sm_event_pairing_complete_get_status(packet);
    if (!context->enrollment || (status != ERROR_CODE_SUCCESS) ||
        !enrollment_is_current(context)) {
        disconnect_device(context, context->enrollment
                                       ? "pairing failed"
                                       : "pairing was not authorized or enrollment expired");
        return;
    }
    begin_hids_preflight(context);
}

static void handle_reencryption_complete(const uint8_t *packet) {
    device_context_t *context = context_by_handle(
        sm_event_reencryption_complete_get_handle(packet));
    if ((context == NULL) || (context->state != DEVICE_SECURING)) {
        return;
    }
    if (context->enrollment ||
        (sm_event_reencryption_complete_get_status(packet) !=
         ERROR_CODE_SUCCESS)) {
        disconnect_device(context, "saved bond re-encryption failed");
        return;
    }
    begin_hids_preflight(context);
}

static void publish_normalized(device_context_t *context,
                               const hid_normalized_report_t *normalized) {
    if (normalized->role == HID_REPORT_ROLE_KEYBOARD) {
        bridge_keyboard_report_t report;
        report.modifier = normalized->data.keyboard.modifier;
        report.reserved = 0u;
        memcpy(report.keycode, normalized->data.keyboard.keys,
               sizeof(report.keycode));
        (void)bridge_keyboard_publish(context->generation, &report);
    } else if (normalized->role == HID_REPORT_ROLE_MOUSE) {
        const bridge_mouse_publish_result_t publish_result =
            bridge_mouse_publish(context->generation,
                                 normalized->data.mouse.buttons,
                                 normalized->data.mouse.x,
                                 normalized->data.mouse.y,
                                 normalized->data.mouse.wheel,
                                 normalized->data.mouse.pan);
        if (publish_result == BRIDGE_MOUSE_PUBLISH_OVERFLOW) {
            disconnect_device(context,
                              "mouse mailbox accumulator overflowed");
        }
    }
}

static void handle_hids_connected(const uint8_t *packet) {
    device_context_t *context = context_by_cid(
        gattservice_subevent_hid_service_connected_get_hids_cid(packet));
    if ((context == NULL) || (context->state != DEVICE_DISCOVERING)) {
        return;
    }
    if ((gattservice_subevent_hid_service_connected_get_status(packet) !=
         ERROR_CODE_SUCCESS) ||
        (gattservice_subevent_hid_service_connected_get_num_instances(packet) !=
         1u) || (context->hids_service_count != 1u)) {
        disconnect_device(context, "requires exactly one valid HIDS service");
        return;
    }

    /*
     * BTstack 501e6d2 exposes neither its discovered input-characteristic
     * table nor value handles through the public HIDS API. Event framing,
     * service index, Report ID, and descriptor lengths are checked below,
     * but characteristic-to-ID uniqueness cannot be preflighted here.
     */

    const uint16_t map_length =
        hids_client_descriptor_storage_get_descriptor_len(context->hids_cid, 0);
    const uint8_t *map =
        hids_client_descriptor_storage_get_descriptor_data(context->hids_cid, 0);
    if ((map == NULL) || (map_length == 0u) ||
        (map_length > HID_REPORT_MAP_MAX_SIZE)) {
        disconnect_device(context, "Report Map is empty or exceeds 2048 bytes");
        return;
    }

    hid_compile_result_t result;
    if (context->role == HID_REPORT_ROLE_NONE) {
        result = hid_report_compile(map, map_length, &context->report_plan);
    } else {
        result = hid_report_compile_for_role(map, map_length, context->role,
                                             &context->report_plan);
    }
    if (result != HID_COMPILE_OK) {
        disconnect_device(context, "Report Map rejected by bounded compiler");
        return;
    }

    const hid_report_role_t compiled_role = context->report_plan.role;
    if (!ble_bridge_appearance_allows_role(context->appearance_hint,
                                           (uint8_t)compiled_role)) {
        disconnect_device(context,
                          "specific GAP Appearance conflicts with Report Map");
        return;
    }
    if (context->role == HID_REPORT_ROLE_NONE) {
        if (!enrollment_is_current(context)) {
            disconnect_device(context,
                              "enrollment expired before authorization commit");
            return;
        }
        if ((compiled_role == HID_REPORT_ROLE_NONE) ||
            saved_roles[role_index(compiled_role)].present ||
            (context_for_role(compiled_role) != NULL)) {
            disconnect_device(context, "reported role is unavailable");
            return;
        }
        const bool secure_bond_ok =
            context_security_ok(context, compiled_role);
        const bool authenticated =
            device_db_authenticated(context->device_db_index);
        if (!ble_bridge_enrollment_security_allowed(
                compiled_role == HID_REPORT_ROLE_KEYBOARD, secure_bond_ok,
                authenticated, context->fixed_passkey_displayed)) {
            disconnect_device(
                context,
                compiled_role == HID_REPORT_ROLE_KEYBOARD
                    ? "keyboard did not complete authenticated fixed-passkey pairing"
                    : "mouse bond is not encrypted with a supported key size");
            return;
        }
        if (!store_enrolled_role(context, compiled_role)) {
            disconnect_device(context, "could not verify and commit role record");
            return;
        }
        context->role = compiled_role;
    }

    hid_report_runtime_init(&context->report_runtime, &context->report_plan);
    context->generation = bridge_role_activate(mailbox_role(context->role));
    context->state = DEVICE_READY;
    context->enrollment = false;
    if (active_operation == context_index(context)) {
        active_operation = -1;
    }
    cancel_operation_timer(context);
    BLE_LOG("%s ready (Report Map %u bytes)\n", role_name(context->role),
            map_length);

    if (saved_roles[0].present && saved_roles[1].present) {
        enrollment_open = false;
    }
    drive_connection_manager();
}

static void handle_hids_report(const uint8_t *packet) {
    device_context_t *context = context_by_cid(
        gattservice_subevent_hid_report_get_hids_cid(packet));
    if ((context == NULL) || (context->state != DEVICE_READY)) {
        return;
    }
    if (gattservice_subevent_hid_report_get_service_index(packet) != 0u) {
        disconnect_device(context, "unexpected HIDS service index");
        return;
    }

    hid_normalized_report_t normalized;
    const hid_normalize_result_t result = hid_report_normalize(
        &context->report_plan, &context->report_runtime,
        gattservice_subevent_hid_report_get_report_id(packet),
        gattservice_subevent_hid_report_get_report(packet),
        gattservice_subevent_hid_report_get_report_len(packet), &normalized);
    if (result == HID_NORMALIZE_IGNORED) {
        return;
    }
    if (result != HID_NORMALIZE_OK) {
        disconnect_device(context, "structurally invalid HID input report");
        return;
    }

    publish_normalized(context, &normalized);
}

static void drive_connection_manager(void) {
    const uint32_t now = btstack_run_loop_get_time_ms();
    if (!radio_working ||
        !__atomic_load_n(&usb_transport_requested, __ATOMIC_ACQUIRE)) {
        return;
    }
    if (enrollment_open && deadline_reached(now, enrollment_deadline_ms)) {
        enrollment_open = false;
        BLE_LOG("First-run enrollment window closed\n");
        for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
            device_context_t *context = &devices[i];
            if (!context->enrollment) {
                continue;
            }
            context->enrollment = false;
            if (context->state == DEVICE_CONNECTING) {
                cancel_operation_timer(context);
                (void)gap_connect_cancel();
                context->state = DEVICE_CONNECT_CANCELING;
                arm_operation_timer(context, CONNECT_TIMEOUT_MS);
            } else if (context->state != DEVICE_CONNECT_CANCELING &&
                       context->state != DEVICE_DISCONNECTING) {
                disconnect_device(context,
                                  "enrollment expired during operation");
            }
        }
    }
    if (active_operation >= 0) {
        return;
    }

    bool need_scan = false;
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if ((devices[i].state == DEVICE_IDLE) &&
            deadline_reached(now, devices[i].retry_after_ms) &&
            ((devices[i].role != HID_REPORT_ROLE_NONE) || enrollment_open)) {
            need_scan = true;
        }
    }
    if (need_scan && !scan_active) {
        /*
         * Request scan responses while enrolling. Some HID peripherals put
         * their service UUID, Appearance, or identifying name in the scan
         * response rather than the primary advertising packet. BTstack reports
         * both packet types through GAP_EVENT_ADVERTISING_REPORT, and the
         * existing parser applies the same bounded validation to either one.
         */
        gap_set_scan_parameters(BLE_SCAN_TYPE_ACTIVE,
                                BLE_SCAN_INTERVAL_UNITS,
                                BLE_SCAN_WINDOW_UNITS);
        gap_start_scan();
        scan_active = true;
        BLE_LOG("Scanning for authorized HID peripherals%s\n",
                enrollment_open ? " and first-run candidates" : "");
    } else if (!need_scan && scan_active) {
        gap_stop_scan();
        scan_active = false;
    }
}

static void operation_timeout(btstack_timer_source_t *timer) {
    device_context_t *context =
        (device_context_t *)btstack_run_loop_get_timer_context(timer);
    if (context == NULL || context_index(context) < 0 ||
        context->timer_state != context->state ||
        context->timer_attempt_token != context->attempt_token) {
        return;
    }
    context->timer_state = DEVICE_IDLE;
    context->timer_attempt_token = 0u;
    if (context->state == DEVICE_CONNECTING) {
        BLE_LOG("Connection attempt %u timed out; cancel pending\n",
                context->attempt_token);
        (void)gap_connect_cancel();
        context->state = DEVICE_CONNECT_CANCELING;
        arm_operation_timer(context, CONNECT_TIMEOUT_MS);
        return;
    }
    if (context->state == DEVICE_CONNECT_CANCELING) {
        BLE_LOG("Connection cancellation timed out; restarting BLE radio\n");
        quiesce_radio();
        return;
    }
    if (context->state == DEVICE_DISCONNECTING) {
        if (context->disconnect_retries == 0u) {
            context->disconnect_retries = 1u;
            BLE_LOG("Disconnect completion timed out; retrying once\n");
            (void)gap_disconnect(context->connection_handle);
            arm_operation_timer(context, DISCONNECT_TIMEOUT_MS);
        } else {
            BLE_LOG("Disconnect retry timed out; restarting BLE radio\n");
            quiesce_radio();
        }
        return;
    }
    disconnect_device(context, "security or discovery timeout");
}

static void resolution_timeout(btstack_timer_source_t *timer) {
    (void)timer;
    if (!resolution_request.pending) {
        return;
    }
    BLE_LOG("Address resolution request %u timed out\n",
            resolution_request.token);
    resolution_request.pending = false;
    resolution_retry_after_ms =
        btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
    schedule_manager(RETRY_BACKOFF_MS);
}

static void manager_timeout(btstack_timer_source_t *timer) {
    (void)timer;
    manager_timer_active = false;
    drive_connection_manager();
    if (enrollment_open) {
        schedule_manager(MANAGER_RETRY_MS);
    }
}

static void led_timeout(btstack_timer_source_t *timer) {
    bool keyboard_ready = false;
    bool mouse_ready = false;
    bool busy = active_operation >= 0;
    bool passkey_prompt = false;
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        passkey_prompt |= devices[i].state == DEVICE_SECURING &&
                          devices[i].fixed_passkey_displayed;
        if (devices[i].state != DEVICE_READY) {
            continue;
        }
        keyboard_ready |= devices[i].role == HID_REPORT_ROLE_KEYBOARD;
        mouse_ready |= devices[i].role == HID_REPORT_ROLE_MOUSE;
    }

    const uint32_t now = btstack_run_loop_get_time_ms();
    if (failure_stage != BLE_FAILURE_STAGE_NONE &&
        deadline_reached(now, failure_display_until_ms)) {
        failure_stage = BLE_FAILURE_STAGE_NONE;
    }

    const bool on = failure_stage != BLE_FAILURE_STAGE_NONE
                        ? ble_bridge_failure_led_on(
                              failure_stage, led_ticks - failure_led_start_tick)
                        : ble_bridge_led_on(keyboard_ready, mouse_ready, busy,
                                            passkey_prompt, led_ticks);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    led_ticks++;
    btstack_run_loop_set_timer(timer, LED_TICK_MS);
    btstack_run_loop_add_timer(timer);
}

static void reset_radio_contexts(void) {
    if (scan_active) {
        gap_stop_scan();
        scan_active = false;
    }
    cancel_resolution_request();
    (void)btstack_run_loop_remove_timer(&manager_timer);
    manager_timer_active = false;
    active_operation = -1;

    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        device_context_t *context = &devices[i];
        cancel_operation_timer(context);
        release_device(context);
        discard_uncommitted_bond(context);
        if (context->hids_cid != 0u) {
            (void)hids_client_disconnect(context->hids_cid);
            context->hids_cid = 0u;
        }
        if (context->connection_handle != HCI_CON_HANDLE_INVALID) {
            (void)gap_disconnect(context->connection_handle);
        }
    }
    initialize_device_contexts();
}

static void quiesce_radio(void) {
    if (radio_transition_pending) {
        return;
    }
    radio_restart_required = true;
    reset_radio_contexts();
    enrollment_open = false;
    radio_transition_pending = true;
    BLE_LOG("Quiescing BLE scanning, links, and radio\n");
    const int status = hci_power_control(HCI_POWER_OFF);
    if (status != ERROR_CODE_SUCCESS) {
        radio_transition_pending = false;
        radio_retry_after_ms =
            btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
        BLE_LOG("BLE radio power-off request failed, status 0x%02x\n",
                status);
    }
}

static void transport_timeout(btstack_timer_source_t *timer) {
    const bool requested =
        __atomic_load_n(&usb_transport_requested, __ATOMIC_ACQUIRE);
    const uint32_t now = btstack_run_loop_get_time_ms();
    const ble_radio_action_t action = ble_bridge_radio_next_action(
        requested, radio_working, radio_transition_pending,
        radio_restart_required, deadline_reached(now, radio_retry_after_ms));
    if (action == BLE_RADIO_ACTION_POWER_OFF) {
        quiesce_radio();
    } else if (action == BLE_RADIO_ACTION_POWER_ON) {
        radio_transition_pending = true;
        BLE_LOG("Starting BLE radio after USB activity\n");
        const int status = hci_power_control(HCI_POWER_ON);
        if (status != ERROR_CODE_SUCCESS) {
            radio_transition_pending = false;
            radio_retry_after_ms = now + RETRY_BACKOFF_MS;
            BLE_LOG("BLE radio power-on request failed, status 0x%02x\n",
                    status);
        }
    }
    btstack_run_loop_set_timer(timer, TRANSPORT_TICK_MS);
    btstack_run_loop_add_timer(timer);
}

void ble_bridge_set_usb_active(bool active) {
    __atomic_store_n(&usb_transport_requested, active, __ATOMIC_RELEASE);
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET ||
        !ble_bridge_event_frame_valid(packet, size, 2u)) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (size < 3u) {
                break;
            }
            if (btstack_event_state_get_state(packet) == HCI_STATE_OFF) {
                radio_working = false;
                radio_transition_pending = false;
                radio_restart_required = false;
                reset_radio_contexts();
                if (__atomic_load_n(&usb_transport_requested,
                                    __ATOMIC_ACQUIRE)) {
                    radio_transition_pending = true;
                    const int status = hci_power_control(HCI_POWER_ON);
                    if (status != ERROR_CODE_SUCCESS) {
                        radio_transition_pending = false;
                        radio_retry_after_ms =
                            btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
                        BLE_LOG("BLE radio restart failed, status 0x%02x\n",
                                status);
                    }
                }
                break;
            }
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
                break;
            }
            radio_working = true;
            radio_transition_pending = false;
            radio_restart_required = false;
            radio_retry_after_ms = 0u;
            if (!__atomic_load_n(&usb_transport_requested,
                                 __ATOMIC_ACQUIRE)) {
                quiesce_radio();
                break;
            }
            initialize_device_contexts();
            memset(saved_roles, 0, sizeof(saved_roles));
            btstack_tlv_get_instance(&tlv_impl, &tlv_context);
            load_saved_role(HID_REPORT_ROLE_KEYBOARD);
            load_saved_role(HID_REPORT_ROLE_MOUSE);
            const bool enrollment_needed =
                !saved_roles[0].present || !saved_roles[1].present;
            const uint32_t now = btstack_run_loop_get_time_ms();
            if (enrollment_needed && !enrollment_window_started) {
                enrollment_window_started = true;
                enrollment_deadline_ms = now + ENROLLMENT_WINDOW_MS;
            }
            enrollment_open = enrollment_needed &&
                              !deadline_reached(now, enrollment_deadline_ms);
            if (enrollment_open) {
                BLE_LOG("First-run enrollment open for %u seconds; put only intended devices in pairing mode\n",
                        (unsigned int)ENROLLMENT_WINDOW_SECONDS);
                schedule_manager(MANAGER_RETRY_MS);
            }
            drive_connection_manager();
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
            if (size >= 12u && radio_working) {
                handle_advertisement(packet, size);
            }
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (size >= 6u) {
                handle_disconnection(packet);
            }
            break;
        case HCI_EVENT_META_GAP:
            if (size >= 36u &&
                hci_event_gap_meta_get_subevent_code(packet) ==
                GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
                handle_connection_complete(packet);
            }
            break;
        default:
            break;
    }
}

static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET ||
        !ble_bridge_event_frame_valid(packet, size, 2u)) {
        return;
    }

    hci_con_handle_t handle;
    device_context_t *context;
    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
            /* index occupies bytes 18..19 in the pinned BTstack event. */
            if (size >= 20u) {
                handle_resolution_success(packet);
            }
            break;
        case SM_EVENT_IDENTITY_RESOLVING_FAILED:
            if (size >= 11u) {
                handle_resolution_failure(packet);
            }
            break;
        case SM_EVENT_IDENTITY_CREATED:
            /* identity address and database index extend through byte 19. */
            if (size >= 20u) {
                handle_identity_created(packet);
            }
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            /* Includes peer address and secure-connection flag through byte 11. */
            if (size < 12u) {
                if (size >= 4u) {
                    sm_bonding_decline(
                        sm_event_just_works_request_get_handle(packet));
                }
                break;
            }
            handle = sm_event_just_works_request_get_handle(packet);
            context = context_by_handle(handle);
            if ((context != NULL) &&
                (context->state == DEVICE_SECURING) &&
                enrollment_is_current(context)) {
                sm_just_works_confirm(handle);
            } else {
                sm_bonding_decline(handle);
                if (context != NULL) {
                    disconnect_device(context,
                                      "Just Works request was not within enrollment window");
                }
            }
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            if (size >= 4u) {
                sm_bonding_decline(
                    sm_event_numeric_comparison_request_get_handle(packet));
            }
            break;
        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            if (size < 16u) {
                if (size >= 4u) {
                    sm_bonding_decline(
                        sm_event_passkey_display_number_get_handle(packet));
                }
                break;
            }
            handle = sm_event_passkey_display_number_get_handle(packet);
            context = context_by_handle(handle);
            if (ble_bridge_passkey_display_allowed(
                    context != NULL && context->state == DEVICE_SECURING,
                    context != NULL && enrollment_is_current(context),
                    sm_event_passkey_display_number_get_secure_connection(
                        packet) != 0u,
                    sm_event_passkey_display_number_get_passkey(packet))) {
                context->fixed_passkey_displayed = true;
                /* Start the visible three-pulse prompt immediately. */
                led_ticks = 0u;
                BLE_LOG("Passkey pairing for %s: type %06" PRIu32
                        " on the keyboard, then press Enter\n",
                        role_name(context->role),
                        (uint32_t)BLE_BRIDGE_PAIRING_PASSKEY);
                arm_operation_timer(context, SECURITY_TIMEOUT_MS);
            } else {
                sm_bonding_decline(handle);
                if (context != NULL) {
                    disconnect_device(
                        context,
                        "Passkey request was invalid or outside enrollment window");
                }
            }
            break;
        case SM_EVENT_PASSKEY_INPUT_NUMBER:
            if (size >= 4u) {
                sm_bonding_decline(
                    sm_event_passkey_input_number_get_handle(packet));
            }
            break;
        case SM_EVENT_PAIRING_COMPLETE:
            if (size >= 13u) {
                handle_pairing_complete(packet);
            }
            break;
        case SM_EVENT_REENCRYPTION_COMPLETE:
            if (size >= 12u) {
                handle_reencryption_complete(packet);
            }
            break;
        default:
            break;
    }
}

static device_context_t *preflight_context(const uint8_t *packet,
                                           bool query_complete) {
    const uint16_t connection_id = query_complete ?
        gatt_event_query_complete_get_connection_id(packet) :
        gatt_event_service_query_result_get_connection_id(packet);
    const uint16_t service_id = query_complete ?
        gatt_event_query_complete_get_service_id(packet) :
        gatt_event_service_query_result_get_service_id(packet);
    const hci_con_handle_t handle = query_complete ?
        gatt_event_query_complete_get_handle(packet) :
        gatt_event_service_query_result_get_handle(packet);

    if (connection_id == 0u || connection_id > DEVICE_CONTEXT_COUNT) {
        return NULL;
    }
    device_context_t *context = &devices[connection_id - 1u];
    if (context->state != DEVICE_PREFLIGHTING_HIDS ||
        context->connection_handle != handle ||
        context->attempt_token != service_id ||
        active_operation != context_index(context)) {
        return NULL;
    }
    return context;
}

static void hids_preflight_packet_handler(uint8_t packet_type,
                                          uint16_t channel,
                                          uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET ||
        !ble_bridge_event_frame_valid(packet, size, 2u)) {
        return;
    }

    if (hci_event_packet_get_type(packet) == GATT_EVENT_SERVICE_QUERY_RESULT) {
        if (size < 8u) {
            return;
        }
        device_context_t *context = preflight_context(packet, false);
        if (context != NULL && context->hids_service_count < 2u) {
            context->hids_service_count++;
        }
        return;
    }
    if (hci_event_packet_get_type(packet) == GATT_EVENT_QUERY_COMPLETE) {
        if (size < 9u) {
            return;
        }
        device_context_t *context = preflight_context(packet, true);
        if (context == NULL) {
            return;
        }
        cancel_operation_timer(context);
        if (gatt_event_query_complete_get_att_status(packet) !=
                ATT_ERROR_SUCCESS ||
            context->hids_service_count != 1u) {
            disconnect_device(context,
                              "preflight requires exactly one HIDS service");
            return;
        }
        begin_hids_client_discovery(context);
    }
}

static void reject_malformed_hids_report(const uint8_t *packet, uint16_t size,
                                         const char *reason) {
    if (packet == NULL || size < 5u) {
        return;
    }
    device_context_t *context = context_by_cid(
        gattservice_subevent_hid_report_get_hids_cid(packet));
    if (context != NULL && context->state == DEVICE_READY) {
        disconnect_device(context, reason);
    }
}

static void hids_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    if (!ble_bridge_hids_packet_type_allowed(
            packet_type, HCI_EVENT_PACKET, HCI_EVENT_GATTSERVICE_META) ||
        packet == NULL || size < 3u ||
        hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) {
        return;
    }
    if (!ble_bridge_event_frame_valid(packet, size, 3u)) {
        if (packet[2] == GATTSERVICE_SUBEVENT_HID_REPORT) {
            reject_malformed_hids_report(
                packet, size, "invalid HIDS event length encoding");
        }
        return;
    }

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            if (size < 8u) {
                break;
            }
            const uint16_t cid =
                gattservice_subevent_hid_service_connected_get_hids_cid(packet);
            if (!ble_bridge_hids_callback_route_valid(
                    packet_type, channel, cid, HCI_EVENT_PACKET,
                    HCI_EVENT_GATTSERVICE_META)) {
                break;
            }
            handle_hids_connected(packet);
            break;
        }
        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            if (size < 10u) {
                reject_malformed_hids_report(
                    packet, size, "truncated HIDS report event");
                break;
            }
            const uint16_t cid =
                gattservice_subevent_hid_report_get_hids_cid(packet);
            const uint16_t report_length =
                gattservice_subevent_hid_report_get_report_len(packet);
            if (!ble_bridge_hids_callback_route_valid(
                    packet_type, channel, cid, HCI_EVENT_PACKET,
                    HCI_EVENT_GATTSERVICE_META) ||
                report_length == 0u ||
                report_length > HID_REPORT_INPUT_MAX_SIZE + 1u ||
                report_length != size - 9u) {
                reject_malformed_hids_report(
                    packet, size, "invalid HIDS callback framing or CID");
                break;
            }
            handle_hids_report(packet);
            break;
        }
        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED: {
            if (size < 5u) {
                break;
            }
            const uint16_t cid =
                gattservice_subevent_hid_service_disconnected_get_hids_cid(
                    packet);
            if (!ble_bridge_hids_callback_route_valid(
                    packet_type, channel, cid, HCI_EVENT_PACKET,
                    HCI_EVENT_GATTSERVICE_META)) {
                break;
            }
            device_context_t *context = context_by_cid(
                cid);
            if (context != NULL) {
                release_device(context);
                context->hids_cid = 0u;
            }
            break;
        }
        default:
            break;
    }
}

int btstack_main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    initialize_device_contexts();
    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
    sm_use_fixed_passkey_in_display_role(BLE_BRIDGE_PAIRING_PASSKEY);
    sm_set_encryption_key_size_range(BLE_COMPAT_MIN_KEY_SIZE,
                                     BLE_MAX_KEY_SIZE);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION |
                                       SM_AUTHREQ_BONDING);
    gatt_client_init();
    att_server_init(profile_data, NULL, NULL);
    hids_client_init(report_map_storage, sizeof(report_map_storage));

    hci_event_registration.callback = packet_handler;
    hci_add_event_handler(&hci_event_registration);
    sm_event_registration.callback = sm_packet_handler;
    sm_add_event_handler(&sm_event_registration);

    btstack_run_loop_set_timer_handler(&led_timer, led_timeout);
    btstack_run_loop_set_timer(&led_timer, LED_TICK_MS);
    btstack_run_loop_add_timer(&led_timer);
    btstack_run_loop_set_timer_handler(&transport_timer, transport_timeout);
    btstack_run_loop_set_timer(&transport_timer, TRANSPORT_TICK_MS);
    btstack_run_loop_add_timer(&transport_timer);
    return 0;
}

void ble_host_main(void) {
    if (ble_bridge_bt_example_init() != 0) {
        BLE_LOG("CYW43 initialization failed; Bluetooth core stopped\n");
        return;
    }
    ble_bridge_bt_example_main();
    btstack_run_loop_execute();
}
