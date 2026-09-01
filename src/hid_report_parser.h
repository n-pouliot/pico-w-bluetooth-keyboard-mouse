#ifndef XBOX_PICO_HID_REPORT_PARSER_H
#define XBOX_PICO_HID_REPORT_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_REPORT_MAP_MAX_SIZE 2048u
#define HID_REPORT_INPUT_MAX_SIZE 64u
#define HID_REPORT_MAX_IDS 8u
#define HID_REPORT_MAX_APPLICATION_COLLECTIONS 8u
#define HID_REPORT_MAX_COLLECTION_DEPTH 8u
#define HID_REPORT_MAX_GLOBAL_PUSH_DEPTH 4u
#define HID_REPORT_MAX_FIELDS 256u
#define HID_REPORT_KEYBOARD_BYTES 8u
#define HID_REPORT_KEYBOARD_KEYS 6u

typedef enum {
    HID_REPORT_ROLE_NONE = 0,
    HID_REPORT_ROLE_KEYBOARD = 1,
    HID_REPORT_ROLE_MOUSE = 2
} hid_report_role_t;

typedef enum {
    HID_COMPILE_OK = 0,
    HID_COMPILE_BAD_ARGUMENT,
    HID_COMPILE_EMPTY,
    HID_COMPILE_MAP_TOO_LARGE,
    HID_COMPILE_TRUNCATED,
    HID_COMPILE_LONG_ITEM,
    HID_COMPILE_MALFORMED,
    HID_COMPILE_LIMIT_EXCEEDED,
    HID_COMPILE_UNSUPPORTED_ROLE,
    HID_COMPILE_COMBO_ROLE,
    HID_COMPILE_ROLE_MISMATCH
} hid_compile_result_t;

typedef enum {
    HID_NORMALIZE_OK = 0,
    HID_NORMALIZE_IGNORED = 1,
    HID_NORMALIZE_BAD_ARGUMENT = -1,
    HID_NORMALIZE_BAD_PREFIX = -2,
    HID_NORMALIZE_BAD_LENGTH = -3,
    HID_NORMALIZE_VALUE_OUT_OF_RANGE = -4,
    HID_NORMALIZE_STATE_MISMATCH = -5,
    HID_NORMALIZE_ARITHMETIC_OVERFLOW = -6,
    HID_NORMALIZE_UNKNOWN_REPORT_ID = -7
} hid_normalize_result_t;

typedef enum {
    HID_FIELD_KEY_ARRAY = 1,
    HID_FIELD_KEY_BIT,
    HID_FIELD_MOUSE_BUTTON,
    HID_FIELD_MOUSE_X,
    HID_FIELD_MOUSE_Y,
    HID_FIELD_MOUSE_WHEEL,
    HID_FIELD_MOUSE_PAN
} hid_report_field_kind_t;

typedef struct {
    uint16_t bit_offset;
    uint16_t count;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint16_t usage;
    uint8_t report_index;
    uint8_t bit_width;
    uint8_t kind;
} hid_report_field_t;

typedef struct {
    uint16_t bit_length;
    uint8_t report_id;
    uint8_t payload_length;
    uint8_t role_relevant;
} hid_report_definition_t;

typedef struct {
    hid_report_role_t role;
    uint16_t field_count;
    uint8_t report_count;
    uint8_t application_collection_count;
    hid_report_definition_t reports[HID_REPORT_MAX_IDS];
    hid_report_field_t fields[HID_REPORT_MAX_FIELDS];
} hid_report_plan_t;

typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[HID_REPORT_KEYBOARD_KEYS];
} hid_keyboard_state_t;

typedef struct {
    uint8_t buttons;
    int32_t x;
    int32_t y;
    int32_t wheel;
    int32_t pan;
} hid_mouse_state_t;

typedef struct {
    hid_report_role_t role;
    union {
        hid_keyboard_state_t keyboard;
        hid_mouse_state_t mouse;
    } data;
} hid_normalized_report_t;

/*
 * Runtime storage is deliberately caller-owned.  Each Report ID has an
 * independent contribution so an update on one ID cannot release state held
 * by another ID.
 */
typedef struct {
    uint32_t cookie;
    hid_report_role_t role;
    uint8_t report_count;
    uint8_t report_ids[HID_REPORT_MAX_IDS];
    uint8_t keyboard_modifiers[HID_REPORT_MAX_IDS];
    uint8_t keyboard_keys[HID_REPORT_MAX_IDS][32];
    uint8_t mouse_buttons[HID_REPORT_MAX_IDS];
} hid_report_runtime_t;

hid_compile_result_t hid_report_compile(const uint8_t *report_map,
                                        size_t report_map_length,
                                        hid_report_plan_t *out_plan);

hid_compile_result_t hid_report_compile_for_role(const uint8_t *report_map,
                                                 size_t report_map_length,
                                                 hid_report_role_t expected_role,
                                                 hid_report_plan_t *out_plan);

void hid_report_runtime_init(hid_report_runtime_t *runtime,
                             const hid_report_plan_t *plan);

hid_normalize_result_t hid_report_normalize(
    const hid_report_plan_t *plan,
    hid_report_runtime_t *runtime,
    uint8_t event_report_id,
    const uint8_t *btstack_report,
    size_t btstack_report_length,
    hid_normalized_report_t *out_report);

#ifdef __cplusplus
}
#endif

#endif
