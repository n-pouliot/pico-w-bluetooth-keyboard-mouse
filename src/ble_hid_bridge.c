/*
 * Dual HID-over-GATT central for xbox-pico.
 *
 * The event-loop structure is derived from BlueKitchen's hog_host_demo.c and
 * remains subject to that file's non-commercial 3-clause BSD-style license.
 * Project-specific state management and HID translation are original work.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/le_device_db.h"

#include "bridge.h"
#include "bt_example_common.h"
#include "bridge_log.h"
#include "bridge_mailbox.h"
#include "hid_report_parser.h"
#include "pairing_store.h"
#include "pico/cyw43_arch.h"

#define DEVICE_CONTEXT_COUNT 2u
#define SHARED_REPORT_MAP_STORAGE_SIZE 4096u
#define ENROLLMENT_WINDOW_MS 120000u
#define CONNECT_TIMEOUT_MS 10000u
#define SECURITY_TIMEOUT_MS 30000u
#define DISCOVERY_TIMEOUT_MS 45000u
#define RETRY_BACKOFF_MS 1000u
#define MANAGER_RETRY_MS 250u
#define LED_TICK_MS 100u
#define MALFORMED_REPORT_LIMIT 8u

#define CONN_INTERVAL_MIN_UNITS 10u
#define CONN_INTERVAL_MAX_UNITS 12u
#define CONN_LATENCY_EVENTS 0u
#define CONN_TIMEOUT_UNITS 300u

#define TLV_TAG_XHKB ((((uint32_t)'X') << 24) | (((uint32_t)'H') << 16) | \
                      (((uint32_t)'K') << 8) | (uint32_t)'B')
#define TLV_TAG_XHMS ((((uint32_t)'X') << 24) | (((uint32_t)'H') << 16) | \
                      (((uint32_t)'M') << 8) | (uint32_t)'S')

typedef enum {
    DEVICE_IDLE = 0,
    DEVICE_CONNECTING,
    DEVICE_SECURING,
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
    bd_addr_t target_address;
    bd_addr_type_t target_address_type;
    bd_addr_t identity_address;
    bd_addr_type_t identity_address_type;
    int device_db_index;
    hci_con_handle_t connection_handle;
    uint16_t hids_cid;
    uint32_t generation;
    uint32_t retry_after_ms;
    uint8_t malformed_reports;
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
} resolution_request_t;

static device_context_t devices[DEVICE_CONTEXT_COUNT];
static saved_role_t saved_roles[DEVICE_CONTEXT_COUNT];
static uint8_t report_map_storage[SHARED_REPORT_MAP_STORAGE_SIZE];

static const btstack_tlv_t *tlv_impl;
static void *tlv_context;
static btstack_packet_callback_registration_t hci_event_registration;
static btstack_packet_callback_registration_t sm_event_registration;
static btstack_timer_source_t operation_timer;
static btstack_timer_source_t manager_timer;
static btstack_timer_source_t led_timer;
static resolution_request_t resolution_request;

static int8_t active_operation = -1;
static bool scan_active;
static bool manager_timer_active;
static bool enrollment_open;
static uint32_t enrollment_deadline_ms;
static uint32_t led_ticks;

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size);
static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size);
static void hids_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size);
static void drive_connection_manager(void);
static void operation_timeout(btstack_timer_source_t *timer);
static void manager_timeout(btstack_timer_source_t *timer);
static void led_timeout(btstack_timer_source_t *timer);

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

static bool advertisement_has_hid(const uint8_t *packet) {
    const uint8_t *data = gap_event_advertising_report_get_data(packet);
    const uint8_t length = gap_event_advertising_report_get_data_length(packet);
    if (ad_data_contains_uuid16(length, data,
                                ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE)) {
        return true;
    }

    uint16_t offset = 0;
    while (offset < length) {
        const uint8_t item_length = data[offset];
        if (item_length == 0u) {
            break;
        }
        if (((uint16_t)offset + 1u + item_length) > length) {
            break;
        }
        if ((item_length >= 3u) && (data[offset + 1u] == 0x19u)) {
            const uint16_t appearance =
                (uint16_t)data[offset + 2u] |
                ((uint16_t)data[offset + 3u] << 8);
            if ((appearance >= 0x03c0u) && (appearance <= 0x03cfu)) {
                return true;
            }
        }
        offset = (uint16_t)(offset + 1u + item_length);
    }
    return false;
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

static void cancel_operation_timer(void) {
    btstack_run_loop_remove_timer(&operation_timer);
}

static void arm_operation_timer(uint32_t timeout_ms) {
    cancel_operation_timer();
    btstack_run_loop_set_timer_handler(&operation_timer, operation_timeout);
    btstack_run_loop_set_timer(&operation_timer, timeout_ms);
    btstack_run_loop_add_timer(&operation_timer);
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
    context->malformed_reports = 0u;
}

static void finish_disconnected(device_context_t *context) {
    const int index = context_index(context);
    const bool owned_operation_timer =
        (context->state == DEVICE_DISCONNECTING) ||
        ((index >= 0) && (active_operation == index));
    release_device(context);
    context->connection_handle = HCI_CON_HANDLE_INVALID;
    context->hids_cid = 0u;
    context->enrollment = false;
    context->identity_valid = false;
    context->device_db_index = -1;
    context->state = DEVICE_IDLE;
    context->retry_after_ms = btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
    if ((index >= 0) && (active_operation == index)) {
        active_operation = -1;
    }
    if (owned_operation_timer) {
        cancel_operation_timer();
    }
    schedule_manager(RETRY_BACKOFF_MS);
}

static void disconnect_device(device_context_t *context, const char *reason) {
    if ((context == NULL) || (context->state == DEVICE_DISCONNECTING)) {
        return;
    }
    BLE_LOG("Rejecting %s: %s\n", role_name(context->role), reason);
    release_device(context);
    context->state = DEVICE_DISCONNECTING;
    cancel_operation_timer();
    if (context->connection_handle == HCI_CON_HANDLE_INVALID) {
        finish_disconnected(context);
        return;
    }
    (void)gap_disconnect(context->connection_handle);
    arm_operation_timer(CONNECT_TIMEOUT_MS);
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

static bool device_db_security_ok(int index) {
    if (index < 0) {
        return false;
    }
    int key_size = 0;
    int secure_connection = 0;
    le_device_db_encryption_get(index, NULL, NULL, NULL, &key_size, NULL, NULL,
                                &secure_connection);
    return (key_size == 16) && (secure_connection != 0);
}

static bool context_security_ok(device_context_t *context) {
    if (!context->identity_valid) {
        return false;
    }
    context->device_db_index =
        find_device_db_identity(context->identity_address_type,
                                context->identity_address);
    return device_db_security_ok(context->device_db_index);
}

static void initialize_device_contexts(void) {
    memset(devices, 0, sizeof(devices));
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        devices[i].connection_handle = HCI_CON_HANDLE_INVALID;
        devices[i].device_db_index = -1;
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
    if (!device_db_security_ok(saved->device_db_index)) {
        context->state = DEVICE_REPAIR_REQUIRED;
        BLE_LOG("%s role has no valid 16-byte Secure Connections bond\n",
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
        return false;
    }

    saved_roles[index].present = true;
    saved_roles[index].usable = true;
    saved_roles[index].record = record;
    saved_roles[index].device_db_index = context->device_db_index;
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

static void start_connection(device_context_t *context,
                             bd_addr_type_t address_type,
                             const bd_addr_t address, bool enrollment) {
    const int index = context_index(context);
    if ((index < 0) || (active_operation >= 0)) {
        return;
    }

    if (scan_active) {
        gap_stop_scan();
        scan_active = false;
    }
    resolution_request.pending = false;
    memcpy(context->target_address, address, sizeof(bd_addr_t));
    context->target_address_type = (bd_addr_type_t)(address_type & 1u);
    context->enrollment = enrollment;
    context->state = DEVICE_CONNECTING;
    context->connection_handle = HCI_CON_HANDLE_INVALID;
    context->hids_cid = 0u;
    active_operation = (int8_t)index;

    BLE_LOG("Connecting %s to %s\n", role_name(context->role),
            bd_addr_to_str(address));
    const uint8_t status = gap_connect(address, context->target_address_type);
    if (status != ERROR_CODE_SUCCESS) {
        BLE_LOG("gap_connect rejected request, status 0x%02x\n", status);
        active_operation = -1;
        context->state = DEVICE_IDLE;
        context->retry_after_ms = btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
        schedule_manager(RETRY_BACKOFF_MS);
        return;
    }
    arm_operation_timer(CONNECT_TIMEOUT_MS);
}

static void handle_advertisement(const uint8_t *packet) {
    if ((active_operation >= 0) || !scan_active) {
        return;
    }

    const uint32_t now = btstack_run_loop_get_time_ms();
    bd_addr_t address;
    gap_event_advertising_report_get_address(packet, address);
    const bd_addr_type_t type = (bd_addr_type_t)(
        gap_event_advertising_report_get_address_type(packet) & 1u);
    const bool has_hid = advertisement_has_hid(packet);

    if (!address_is_rpa(type, address)) {
        hid_report_role_t known_role = HID_REPORT_ROLE_NONE;
        if (known_role_for_identity(type, address, &known_role)) {
            device_context_t *context = context_for_role(known_role);
            if (context_can_connect(context, now)) {
                start_connection(context, type, address, false);
            }
            return;
        }
        device_context_t *context = candidate_context(now);
        if (has_hid && (context != NULL)) {
            start_connection(context, type, address, true);
        }
        return;
    }

    if (!resolution_request.pending) {
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
            resolution_request.pending = true;
            resolution_request.advertisement_has_hid = has_hid;
            resolution_request.address_type = type;
            memcpy(resolution_request.address, address, sizeof(bd_addr_t));
            sm_address_resolution_lookup(type, address);
            return;
        }
    }

    device_context_t *context = candidate_context(now);
    if (has_hid && (context != NULL) && !resolution_request.pending) {
        start_connection(context, type, address, true);
    }
}

static void handle_resolution_success(const uint8_t *packet) {
    if (!resolution_request.pending) {
        return;
    }
    bd_addr_t observed;
    sm_event_identity_resolving_succeeded_get_address(packet, observed);
    if (memcmp(observed, resolution_request.address, sizeof(bd_addr_t)) != 0) {
        return;
    }

    bd_addr_t identity;
    sm_event_identity_resolving_succeeded_get_identity_address(packet, identity);
    const bd_addr_type_t identity_type = (bd_addr_type_t)(
        sm_event_identity_resolving_succeeded_get_identity_addr_type(packet) & 1u);
    resolution_request.pending = false;

    hid_report_role_t role = HID_REPORT_ROLE_NONE;
    if (known_role_for_identity(identity_type, identity, &role)) {
        device_context_t *context = context_for_role(role);
        if (context_can_connect(context, btstack_run_loop_get_time_ms())) {
            memcpy(context->identity_address, identity, sizeof(bd_addr_t));
            context->identity_address_type = identity_type;
            context->identity_valid = true;
            start_connection(context, resolution_request.address_type,
                             observed, false);
        }
        return;
    }

    device_context_t *context = candidate_context(btstack_run_loop_get_time_ms());
    if (resolution_request.advertisement_has_hid && (context != NULL)) {
        memcpy(context->identity_address, identity, sizeof(bd_addr_t));
        context->identity_address_type = identity_type;
        context->identity_valid = true;
        start_connection(context, resolution_request.address_type, observed, true);
    }
}

static void handle_resolution_failure(const uint8_t *packet) {
    if (!resolution_request.pending) {
        return;
    }
    bd_addr_t observed;
    sm_event_identity_resolving_failed_get_address(packet, observed);
    if (memcmp(observed, resolution_request.address, sizeof(bd_addr_t)) != 0) {
        return;
    }
    resolution_request.pending = false;
    device_context_t *context = candidate_context(btstack_run_loop_get_time_ms());
    if (resolution_request.advertisement_has_hid && (context != NULL)) {
        start_connection(context, resolution_request.address_type, observed, true);
    }
}

static void begin_hids_discovery(device_context_t *context) {
    if (!context_security_ok(context)) {
        disconnect_device(context,
                          "bond is not 16-byte LE Secure Connections");
        return;
    }
    (void)gap_request_connection_parameter_update(
        context->connection_handle, CONN_INTERVAL_MIN_UNITS,
        CONN_INTERVAL_MAX_UNITS, CONN_LATENCY_EVENTS, CONN_TIMEOUT_UNITS);
    context->state = DEVICE_DISCOVERING;
    const uint8_t status = hids_client_connect(
        context->connection_handle, hids_packet_handler,
        HID_PROTOCOL_MODE_REPORT, &context->hids_cid);
    if (status != ERROR_CODE_SUCCESS) {
        disconnect_device(context, "HIDS discovery could not start");
        return;
    }
    arm_operation_timer(DISCOVERY_TIMEOUT_MS);
}

static void handle_connection_complete(const uint8_t *packet) {
    const uint8_t status = gap_subevent_le_connection_complete_get_status(packet);
    const hci_con_handle_t handle =
        gap_subevent_le_connection_complete_get_connection_handle(packet);
    if ((active_operation < 0) ||
        (devices[(unsigned int)active_operation].state != DEVICE_CONNECTING)) {
        if (status == ERROR_CODE_SUCCESS) {
            (void)gap_disconnect(handle);
        }
        return;
    }

    device_context_t *context = &devices[(unsigned int)active_operation];
    cancel_operation_timer();
    if (status != ERROR_CODE_SUCCESS) {
        BLE_LOG("Connection failed, status 0x%02x\n", status);
        active_operation = -1;
        context->state = DEVICE_IDLE;
        context->retry_after_ms = btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
        schedule_manager(RETRY_BACKOFF_MS);
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
    sm_request_pairing(handle);
    arm_operation_timer(SECURITY_TIMEOUT_MS);
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
}

static void handle_pairing_complete(const uint8_t *packet) {
    device_context_t *context = context_by_handle(
        sm_event_pairing_complete_get_handle(packet));
    if ((context == NULL) || (context->state != DEVICE_SECURING)) {
        return;
    }
    const uint8_t status = sm_event_pairing_complete_get_status(packet);
    if (!context->enrollment || (status != ERROR_CODE_SUCCESS)) {
        disconnect_device(context, context->enrollment
                                       ? "pairing failed"
                                       : "unexpected new pairing for saved role");
        return;
    }
    begin_hids_discovery(context);
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
    begin_hids_discovery(context);
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
        (void)bridge_mouse_publish(context->generation,
                                   normalized->data.mouse.buttons,
                                   normalized->data.mouse.x,
                                   normalized->data.mouse.y,
                                   normalized->data.mouse.wheel,
                                   normalized->data.mouse.pan);
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
         1u)) {
        disconnect_device(context, "requires exactly one valid HIDS service");
        return;
    }

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
    if (context->role == HID_REPORT_ROLE_NONE) {
        if ((compiled_role == HID_REPORT_ROLE_NONE) ||
            saved_roles[role_index(compiled_role)].present ||
            (context_for_role(compiled_role) != NULL)) {
            disconnect_device(context, "reported role is unavailable");
            return;
        }
        if (!context_security_ok(context) ||
            !store_enrolled_role(context, compiled_role)) {
            disconnect_device(context, "could not verify and commit role record");
            return;
        }
        context->role = compiled_role;
    }

    hid_report_runtime_init(&context->report_runtime, &context->report_plan);
    context->generation = bridge_role_activate(mailbox_role(context->role));
    context->malformed_reports = 0u;
    context->state = DEVICE_READY;
    context->enrollment = false;
    active_operation = -1;
    cancel_operation_timer();
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
        if (context->malformed_reports < UINT8_MAX) {
            context->malformed_reports++;
        }
        if (context->malformed_reports >= MALFORMED_REPORT_LIMIT) {
            disconnect_device(context, "repeated malformed HID reports");
        }
        return;
    }

    context->malformed_reports = 0u;
    publish_normalized(context, &normalized);
}

static void drive_connection_manager(void) {
    const uint32_t now = btstack_run_loop_get_time_ms();
    if (enrollment_open && deadline_reached(now, enrollment_deadline_ms)) {
        enrollment_open = false;
        BLE_LOG("First-run enrollment window closed\n");
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
        gap_set_scan_parameters(0, 48, 48);
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
    (void)timer;
    if (active_operation < 0) {
        return;
    }
    device_context_t *context = &devices[(unsigned int)active_operation];
    if (context->state == DEVICE_CONNECTING) {
        BLE_LOG("Connection attempt timed out\n");
        (void)gap_connect_cancel();
        active_operation = -1;
        context->state = DEVICE_IDLE;
        context->retry_after_ms = btstack_run_loop_get_time_ms() + RETRY_BACKOFF_MS;
        schedule_manager(RETRY_BACKOFF_MS);
        return;
    }
    if (context->state == DEVICE_DISCONNECTING) {
        BLE_LOG("Disconnect completion timed out; waiting for controller event\n");
        return;
    }
    disconnect_device(context, "security or discovery timeout");
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
    for (unsigned int i = 0; i < DEVICE_CONTEXT_COUNT; ++i) {
        if (devices[i].state != DEVICE_READY) {
            continue;
        }
        keyboard_ready |= devices[i].role == HID_REPORT_ROLE_KEYBOARD;
        mouse_ready |= devices[i].role == HID_REPORT_ROLE_MOUSE;
    }

    bool on;
    if (keyboard_ready && mouse_ready) {
        on = true;
    } else if (busy) {
        on = (led_ticks % 4u) < 2u;
    } else if (keyboard_ready) {
        on = (led_ticks % 20u) == 0u;
    } else if (mouse_ready) {
        const uint32_t phase = led_ticks % 20u;
        on = (phase == 0u) || (phase == 2u);
    } else {
        on = (led_ticks % 10u) < 5u;
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
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
                break;
            }
            btstack_tlv_get_instance(&tlv_impl, &tlv_context);
            load_saved_role(HID_REPORT_ROLE_KEYBOARD);
            load_saved_role(HID_REPORT_ROLE_MOUSE);
            enrollment_open = !saved_roles[0].present || !saved_roles[1].present;
            enrollment_deadline_ms =
                btstack_run_loop_get_time_ms() + ENROLLMENT_WINDOW_MS;
            if (enrollment_open) {
                BLE_LOG("First-run enrollment open for 120 seconds; put only intended devices in pairing mode\n");
                schedule_manager(MANAGER_RETRY_MS);
            }
            drive_connection_manager();
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
            handle_advertisement(packet);
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            handle_disconnection(packet);
            break;
        case HCI_EVENT_META_GAP:
            if (hci_event_gap_meta_get_subevent_code(packet) ==
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
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    hci_con_handle_t handle;
    device_context_t *context;
    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
            handle_resolution_success(packet);
            break;
        case SM_EVENT_IDENTITY_RESOLVING_FAILED:
            handle_resolution_failure(packet);
            break;
        case SM_EVENT_IDENTITY_CREATED:
            handle_identity_created(packet);
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            handle = sm_event_just_works_request_get_handle(packet);
            context = context_by_handle(handle);
            if ((context != NULL) && context->enrollment &&
                (context->state == DEVICE_SECURING)) {
                sm_just_works_confirm(handle);
            } else {
                sm_bonding_decline(handle);
            }
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_bonding_decline(
                sm_event_numeric_comparison_request_get_handle(packet));
            break;
        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            sm_bonding_decline(
                sm_event_passkey_display_number_get_handle(packet));
            break;
        case SM_EVENT_PASSKEY_INPUT_NUMBER:
            sm_bonding_decline(
                sm_event_passkey_input_number_get_handle(packet));
            break;
        case SM_EVENT_PAIRING_COMPLETE:
            handle_pairing_complete(packet);
            break;
        case SM_EVENT_REENCRYPTION_COMPLETE:
            handle_reencryption_complete(packet);
            break;
        default:
            break;
    }
}

static void hids_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if ((packet_type != HCI_EVENT_PACKET) ||
        (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META)) {
        return;
    }

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED:
            handle_hids_connected(packet);
            break;
        case GATTSERVICE_SUBEVENT_HID_REPORT:
            handle_hids_report(packet);
            break;
        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED: {
            device_context_t *context = context_by_cid(
                gattservice_subevent_hid_service_disconnected_get_hids_cid(
                    packet));
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
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_encryption_key_size_range(16, 16);
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
    hci_power_control(HCI_POWER_ON);
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
