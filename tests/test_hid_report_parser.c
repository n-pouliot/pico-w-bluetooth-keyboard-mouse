#include "hid_report_parser.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,     \
                          __LINE__, #condition);                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static const uint8_t boot_keyboard_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xa1, 0x01,       /* Collection (Application) */
    0x05, 0x07,       /* Usage Page (Keyboard) */
    0x19, 0xe0,       /* Usage Minimum (Left Control) */
    0x29, 0xe7,       /* Usage Maximum (Right GUI) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x08,       /* Report Size (8) */
    0x81, 0x01,       /* Input (Constant) */
    0x95, 0x06,       /* Report Count (6) */
    0x75, 0x08,       /* Report Size (8) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x65,       /* Logical Maximum (101) */
    0x19, 0x00,       /* Usage Minimum (Reserved) */
    0x29, 0x65,       /* Usage Maximum (Keyboard Application) */
    0x81, 0x00,       /* Input (Data, Array, Absolute) */
    0xc0              /* End Collection */
};

static const uint8_t split_keyboard_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x85, 0x02,
    0x15, 0x00, 0x25, 0x65, 0x75, 0x08, 0x95, 0x06,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xc0
};

static const uint8_t nkro_keyboard_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
    0x05, 0x07, 0x19, 0x04, 0x29, 0x0a,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x07, 0x81, 0x02,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x01,
    0xc0
};

/*
 * Empirical MX Mechanical keyboard-interface descriptor captured through its
 * Logi Bolt receiver. It is not proof that the BLE Report Map is identical,
 * but it exercises the model's unusual 128-bit one-key-per-bit input layout.
 * Source: https://github.com/hrvach/deskhop/issues/47
 */
static const uint8_t mx_mechanical_keyboard_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x05, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
    0x95, 0x70, 0x75, 0x01, 0x05, 0x07, 0x19, 0x04, 0x29, 0x73,
    0x81, 0x02,
    0x95, 0x05, 0x19, 0x87, 0x29, 0x8b, 0x81, 0x02,
    0x95, 0x03, 0x19, 0x90, 0x29, 0x92, 0x81, 0x02,
    0xc0
};

static const uint8_t consumer_keyboard_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x06, 0x15, 0x00, 0x25, 0x65,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xc0,
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01,
    0x85, 0x03, 0x0a, 0xcd, 0x00,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x95, 0x01, 0x81, 0x01,
    0xc0
};

static const uint8_t mouse_3_button_map[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01,
    0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xc0, 0xc0
};

static const uint8_t mouse_5_button_id_map[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x07,
    0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
    0xc0, 0xc0
};

static const uint8_t mouse_16_bit_pan_map[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x04,
    0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x16, 0x01, 0x80, 0x26, 0xff, 0x7f,
    0x75, 0x10, 0x95, 0x03, 0x81, 0x06,
    0x05, 0x0c, 0x0a, 0x38, 0x02,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x06,
    0xc0, 0xc0
};

static int test_boot_keyboard_no_id(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t pressed[] = {0x00, 0x05, 0x00, 0x04, 0x05,
                               0x00, 0x00, 0x00, 0x00};
    const uint8_t released[] = {0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00};

    CHECK(hid_report_compile(boot_keyboard_map, ARRAY_LENGTH(boot_keyboard_map),
                             &plan) == HID_COMPILE_OK);
    CHECK(plan.role == HID_REPORT_ROLE_KEYBOARD);
    CHECK(plan.report_count == 1u);
    CHECK(plan.reports[0].report_id == 0u);
    CHECK(plan.reports[0].payload_length == 8u);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, pressed,
                               ARRAY_LENGTH(pressed), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0x05u);
    CHECK(output.data.keyboard.reserved == 0u);
    CHECK(output.data.keyboard.keys[0] == 0x04u);
    CHECK(output.data.keyboard.keys[1] == 0x05u);
    CHECK(output.data.keyboard.keys[2] == 0u);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, released,
                               ARRAY_LENGTH(released), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0u);
    CHECK(output.data.keyboard.keys[0] == 0u);
    return 0;
}

static int test_split_report_keyboard(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t modifiers[] = {0x01, 0x02};
    const uint8_t keys[] = {0x02, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00};
    const uint8_t keys_released[] = {0x02, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00};
    const uint8_t modifiers_released[] = {0x01, 0x00};

    CHECK(hid_report_compile(split_keyboard_map,
                             ARRAY_LENGTH(split_keyboard_map), &plan) ==
          HID_COMPILE_OK);
    CHECK(plan.report_count == 2u);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 1u, modifiers,
                               ARRAY_LENGTH(modifiers), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0x02u);
    CHECK(hid_report_normalize(&plan, &runtime, 2u, keys, ARRAY_LENGTH(keys),
                               &output) == HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0x02u);
    CHECK(output.data.keyboard.keys[0] == 0x04u);
    CHECK(output.data.keyboard.keys[1] == 0x06u);
    CHECK(hid_report_normalize(&plan, &runtime, 2u, keys_released,
                               ARRAY_LENGTH(keys_released), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0x02u);
    CHECK(output.data.keyboard.keys[0] == 0u);
    CHECK(hid_report_normalize(&plan, &runtime, 1u, modifiers_released,
                               ARRAY_LENGTH(modifiers_released), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0u);
    return 0;
}

static int test_nkro_rollover_and_release(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t seven_keys[] = {0x00, 0x7f};
    const uint8_t six_keys[] = {0x00, 0x3f};
    const uint8_t released[] = {0x00, 0x00};
    size_t index;

    CHECK(hid_report_compile(nkro_keyboard_map,
                             ARRAY_LENGTH(nkro_keyboard_map), &plan) ==
          HID_COMPILE_OK);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, seven_keys,
                               ARRAY_LENGTH(seven_keys), &output) ==
          HID_NORMALIZE_OK);
    for (index = 0u; index < HID_REPORT_KEYBOARD_KEYS; ++index) {
        CHECK(output.data.keyboard.keys[index] == 0x01u);
    }
    CHECK(hid_report_normalize(&plan, &runtime, 0u, six_keys,
                               ARRAY_LENGTH(six_keys), &output) ==
          HID_NORMALIZE_OK);
    for (index = 0u; index < HID_REPORT_KEYBOARD_KEYS; ++index) {
        CHECK(output.data.keyboard.keys[index] == (uint8_t)(index + 4u));
    }
    CHECK(hid_report_normalize(&plan, &runtime, 0u, released,
                               ARRAY_LENGTH(released), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.keys[0] == 0u);
    return 0;
}

static int test_mx_mechanical_empirical_descriptor(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    uint8_t pressed[17] = {0};
    uint8_t released[17] = {0};

    CHECK(hid_report_compile(mx_mechanical_keyboard_map,
                             ARRAY_LENGTH(mx_mechanical_keyboard_map),
                             &plan) == HID_COMPILE_OK);
    CHECK(plan.role == HID_REPORT_ROLE_KEYBOARD);
    CHECK(plan.report_count == 1u);
    CHECK(plan.reports[0].report_id == 0u);
    CHECK(plan.reports[0].payload_length == 16u);

    /* Prefix byte, modifiers, A, Z, F12, and usage 0x87. */
    pressed[1] = 0x05u;
    pressed[2] = 0x01u;
    pressed[5] = 0x02u;
    pressed[10] = 0x02u;
    pressed[16] = 0x01u;

    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, pressed,
                               ARRAY_LENGTH(pressed), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0x05u);
    CHECK(output.data.keyboard.keys[0] == 0x04u);
    CHECK(output.data.keyboard.keys[1] == 0x1du);
    CHECK(output.data.keyboard.keys[2] == 0x45u);
    CHECK(output.data.keyboard.keys[3] == 0x87u);
    CHECK(output.data.keyboard.keys[4] == 0u);

    CHECK(hid_report_normalize(&plan, &runtime, 0u, released,
                               ARRAY_LENGTH(released), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.keyboard.modifier == 0u);
    CHECK(output.data.keyboard.keys[0] == 0u);
    return 0;
}

static int test_consumer_collection_is_ignored(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t consumer[] = {0x03, 0x01};
    const uint8_t consumer_short[] = {0x03};

    CHECK(hid_report_compile(consumer_keyboard_map,
                             ARRAY_LENGTH(consumer_keyboard_map), &plan) ==
          HID_COMPILE_OK);
    CHECK(plan.role == HID_REPORT_ROLE_KEYBOARD);
    CHECK(plan.application_collection_count == 2u);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 3u, consumer,
                               ARRAY_LENGTH(consumer), &output) ==
          HID_NORMALIZE_IGNORED);
    CHECK(hid_report_normalize(&plan, &runtime, 3u, consumer_short,
                               ARRAY_LENGTH(consumer_short), &output) ==
          HID_NORMALIZE_BAD_LENGTH);
    return 0;
}

static int test_mouse_reports(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t mouse3[] = {0x00, 0x05, 0x0a, 0xec, 0x01};
    const uint8_t mouse5[] = {0x07, 0x15, 0x7f, 0x81};

    CHECK(hid_report_compile(mouse_3_button_map,
                             ARRAY_LENGTH(mouse_3_button_map), &plan) ==
          HID_COMPILE_OK);
    CHECK(plan.role == HID_REPORT_ROLE_MOUSE);
    CHECK(plan.reports[0].payload_length == 4u);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, mouse3,
                               ARRAY_LENGTH(mouse3), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.mouse.buttons == 0x05u);
    CHECK(output.data.mouse.x == 10);
    CHECK(output.data.mouse.y == -20);
    CHECK(output.data.mouse.wheel == 1);
    CHECK(output.data.mouse.pan == 0);

    CHECK(hid_report_compile(mouse_5_button_id_map,
                             ARRAY_LENGTH(mouse_5_button_id_map), &plan) ==
          HID_COMPILE_OK);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 7u, mouse5,
                               ARRAY_LENGTH(mouse5), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.mouse.buttons == 0x15u);
    CHECK(output.data.mouse.x == 127);
    CHECK(output.data.mouse.y == -127);
    return 0;
}

static int test_mouse_16_bit_motion_and_pan(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t report[] = {
        0x04, 0x11, 0x2c, 0x01, 0xd4, 0xfe, 0xe8, 0x03, 0x18, 0xfc
    };

    CHECK(hid_report_compile(mouse_16_bit_pan_map,
                             ARRAY_LENGTH(mouse_16_bit_pan_map), &plan) ==
          HID_COMPILE_OK);
    CHECK(plan.reports[0].payload_length == 9u);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 4u, report,
                               ARRAY_LENGTH(report), &output) ==
          HID_NORMALIZE_OK);
    CHECK(output.data.mouse.buttons == 0x11u);
    CHECK(output.data.mouse.x == 300);
    CHECK(output.data.mouse.y == -300);
    CHECK(output.data.mouse.wheel == 1000);
    CHECK(output.data.mouse.pan == -1000);
    return 0;
}

static int test_packet_framing_errors(void)
{
    hid_report_plan_t plan;
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    const uint8_t correct[] = {0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00};
    uint8_t too_long[10] = {0};
    const uint8_t unknown[] = {0x09, 0xaa};
    hid_report_runtime_t wrong_runtime;

    CHECK(hid_report_compile(boot_keyboard_map, ARRAY_LENGTH(boot_keyboard_map),
                             &plan) == HID_COMPILE_OK);
    hid_report_runtime_init(&runtime, &plan);
    CHECK(hid_report_normalize(&plan, &runtime, 1u, correct,
                               ARRAY_LENGTH(correct), &output) ==
          HID_NORMALIZE_BAD_PREFIX);
    CHECK(hid_report_normalize(&plan, &runtime, 9u, unknown,
                               ARRAY_LENGTH(unknown), &output) ==
          HID_NORMALIZE_UNKNOWN_REPORT_ID);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, correct,
                               ARRAY_LENGTH(correct) - 1u, &output) ==
          HID_NORMALIZE_BAD_LENGTH);
    CHECK(hid_report_normalize(&plan, &runtime, 0u, too_long,
                               ARRAY_LENGTH(too_long), &output) ==
          HID_NORMALIZE_BAD_LENGTH);
    memset(&wrong_runtime, 0, sizeof(wrong_runtime));
    CHECK(hid_report_normalize(&plan, &wrong_runtime, 0u, correct,
                               ARRAY_LENGTH(correct), &output) ==
          HID_NORMALIZE_STATE_MISMATCH);
    return 0;
}

static int test_malformed_and_truncated_maps(void)
{
    hid_report_plan_t plan;
    size_t length;
    const uint8_t long_item[] = {0xfe, 0x01, 0x00, 0x00};
    const uint8_t truncated_1[] = {0x05};
    const uint8_t truncated_2[] = {0x16, 0x00};
    const uint8_t truncated_4[] = {0x27, 0x00, 0x00, 0x00};
    const uint8_t collection_underflow[] = {0xc0};
    const uint8_t pop_underflow[] = {0xb4};
    const uint8_t reversed_usage[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
        0x05, 0x07, 0x19, 0x08, 0x29, 0x04,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05, 0x81, 0x02,
        0xc0
    };
    const uint8_t missing_globals[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
        0x05, 0x07, 0x09, 0x04, 0x81, 0x02, 0xc0
    };

    for (length = 0u; length < ARRAY_LENGTH(boot_keyboard_map); ++length) {
        CHECK(hid_report_compile(boot_keyboard_map, length, &plan) !=
              HID_COMPILE_OK);
    }
    CHECK(hid_report_compile(long_item, ARRAY_LENGTH(long_item), &plan) ==
          HID_COMPILE_LONG_ITEM);
    CHECK(hid_report_compile(truncated_1, ARRAY_LENGTH(truncated_1), &plan) ==
          HID_COMPILE_TRUNCATED);
    CHECK(hid_report_compile(truncated_2, ARRAY_LENGTH(truncated_2), &plan) ==
          HID_COMPILE_TRUNCATED);
    CHECK(hid_report_compile(truncated_4, ARRAY_LENGTH(truncated_4), &plan) ==
          HID_COMPILE_TRUNCATED);
    CHECK(hid_report_compile(collection_underflow,
                             ARRAY_LENGTH(collection_underflow), &plan) ==
          HID_COMPILE_MALFORMED);
    CHECK(hid_report_compile(pop_underflow, ARRAY_LENGTH(pop_underflow),
                             &plan) == HID_COMPILE_MALFORMED);
    CHECK(hid_report_compile(reversed_usage, ARRAY_LENGTH(reversed_usage),
                             &plan) == HID_COMPILE_MALFORMED);
    CHECK(hid_report_compile(missing_globals, ARRAY_LENGTH(missing_globals),
                             &plan) == HID_COMPILE_MALFORMED);
    return 0;
}

static int test_limits(void)
{
    hid_report_plan_t plan;
    uint8_t oversized[HID_REPORT_MAP_MAX_SIZE + 1u] = {0};
    uint8_t app_overflow[80] = {0};
    uint8_t depth_overflow[50] = {0};
    uint8_t id_overflow[100] = {0};
    size_t offset = 0u;
    size_t index;
    const uint8_t pushes[] = {0xa4, 0xa4, 0xa4, 0xa4, 0xa4};
    const uint8_t fields_overflow[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
        0x05, 0x07, 0x09, 0x04, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x96, 0x01, 0x01, 0x81, 0x02, 0xc0
    };
    const uint8_t report_overflow[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
        0x05, 0x07, 0x19, 0x00, 0x29, 0x41,
        0x15, 0x00, 0x25, 0x41, 0x75, 0x08, 0x95, 0x41, 0x81, 0x00,
        0xc0
    };
    const uint8_t width_overflow[] = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
        0x05, 0x07, 0x09, 0x04, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x11, 0x95, 0x01, 0x81, 0x02, 0xc0
    };

    CHECK(hid_report_compile(oversized, ARRAY_LENGTH(oversized), &plan) ==
          HID_COMPILE_MAP_TOO_LARGE);
    for (index = 0u; index < 9u; ++index) {
        const uint8_t application[] = {0x05, 0x0c, 0x09, 0x01,
                                       0xa1, 0x01, 0xc0};
        memcpy(&app_overflow[offset], application, sizeof(application));
        offset += sizeof(application);
    }
    CHECK(hid_report_compile(app_overflow, offset, &plan) ==
          HID_COMPILE_LIMIT_EXCEEDED);

    offset = 0u;
    depth_overflow[offset++] = 0x05;
    depth_overflow[offset++] = 0x01;
    for (index = 0u; index < 9u; ++index) {
        depth_overflow[offset++] = 0x09;
        depth_overflow[offset++] = index == 0u ? 0x06u : 0x01u;
        depth_overflow[offset++] = 0xa1;
        depth_overflow[offset++] = index == 0u ? 0x01u : 0x00u;
    }
    CHECK(hid_report_compile(depth_overflow, offset, &plan) ==
          HID_COMPILE_LIMIT_EXCEEDED);

    {
        const uint8_t id_header[] = {
            0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
            0x05, 0x07, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01
        };
        offset = 0u;
        memcpy(id_overflow, id_header, sizeof(id_header));
        offset += sizeof(id_header);
        for (index = 1u; index <= HID_REPORT_MAX_IDS + 1u; ++index) {
            id_overflow[offset++] = 0x85;
            id_overflow[offset++] = (uint8_t)index;
            id_overflow[offset++] = 0x09;
            id_overflow[offset++] = 0x04;
            id_overflow[offset++] = 0x81;
            id_overflow[offset++] = 0x02;
        }
        CHECK(hid_report_compile(id_overflow, offset, &plan) ==
              HID_COMPILE_LIMIT_EXCEEDED);
    }
    CHECK(hid_report_compile(pushes, ARRAY_LENGTH(pushes), &plan) ==
          HID_COMPILE_LIMIT_EXCEEDED);
    CHECK(hid_report_compile(fields_overflow, ARRAY_LENGTH(fields_overflow),
                             &plan) == HID_COMPILE_LIMIT_EXCEEDED);
    CHECK(hid_report_compile(report_overflow, ARRAY_LENGTH(report_overflow),
                             &plan) == HID_COMPILE_LIMIT_EXCEEDED);
    CHECK(hid_report_compile(width_overflow, ARRAY_LENGTH(width_overflow),
                             &plan) != HID_COMPILE_OK);
    return 0;
}

static int test_id_zero_roles_and_absolute_mouse(void)
{
    hid_report_plan_t plan;
    uint8_t id_zero[ARRAY_LENGTH(split_keyboard_map)];
    uint8_t combo[ARRAY_LENGTH(boot_keyboard_map) +
                  ARRAY_LENGTH(mouse_3_button_map)];
    uint8_t absolute_mouse[ARRAY_LENGTH(mouse_3_button_map)];
    size_t index;

    memcpy(id_zero, split_keyboard_map, sizeof(id_zero));
    id_zero[7] = 0x00;
    CHECK(hid_report_compile(id_zero, ARRAY_LENGTH(id_zero), &plan) ==
          HID_COMPILE_MALFORMED);

    memcpy(combo, boot_keyboard_map, ARRAY_LENGTH(boot_keyboard_map));
    memcpy(&combo[ARRAY_LENGTH(boot_keyboard_map)], mouse_3_button_map,
           ARRAY_LENGTH(mouse_3_button_map));
    CHECK(hid_report_compile(combo, ARRAY_LENGTH(combo), &plan) ==
          HID_COMPILE_COMBO_ROLE);
    CHECK(hid_report_compile_for_role(boot_keyboard_map,
                                      ARRAY_LENGTH(boot_keyboard_map),
                                      HID_REPORT_ROLE_MOUSE, &plan) ==
          HID_COMPILE_ROLE_MISMATCH);

    memcpy(absolute_mouse, mouse_3_button_map, sizeof(absolute_mouse));
    for (index = ARRAY_LENGTH(absolute_mouse) - 1u; index > 0u; --index) {
        if (absolute_mouse[index - 1u] == 0x81u &&
            absolute_mouse[index] == 0x06u) {
            absolute_mouse[index] = 0x02u;
            break;
        }
    }
    CHECK(index != 0u);
    CHECK(hid_report_compile(absolute_mouse, ARRAY_LENGTH(absolute_mouse),
                             &plan) == HID_COMPILE_UNSUPPORTED_ROLE);
    return 0;
}

static void exercise_compiled_plan(const hid_report_plan_t *plan)
{
    hid_report_runtime_t runtime;
    hid_normalized_report_t output;
    uint8_t report[HID_REPORT_INPUT_MAX_SIZE + 1u] = {0};
    uint8_t index;

    hid_report_runtime_init(&runtime, plan);
    for (index = 0u; index < plan->report_count; ++index) {
        const size_t length = (size_t)plan->reports[index].payload_length + 1u;
        report[0] = plan->reports[index].report_id;
        (void)hid_report_normalize(plan, &runtime,
                                   plan->reports[index].report_id,
                                   report, length, &output);
    }
}

static int test_descriptor_mutation_smoke(void)
{
    uint8_t mutated[256];
    hid_report_plan_t plan;
    uint32_t random = UINT32_C(0x6d2b79f5);
    size_t byte;
    uint8_t bit;

    _Static_assert(ARRAY_LENGTH(boot_keyboard_map) <= ARRAY_LENGTH(mutated),
                   "mutation buffer must hold the canonical descriptor");
    for (byte = 0u; byte < ARRAY_LENGTH(boot_keyboard_map); ++byte) {
        for (bit = 0u; bit < 8u; ++bit) {
            memcpy(mutated, boot_keyboard_map,
                   ARRAY_LENGTH(boot_keyboard_map));
            mutated[byte] ^= (uint8_t)(1u << bit);
            if (hid_report_compile(mutated, ARRAY_LENGTH(boot_keyboard_map),
                                   &plan) == HID_COMPILE_OK) {
                exercise_compiled_plan(&plan);
            }
        }
    }

    for (byte = 0u; byte < 5000u; ++byte) {
        size_t index;
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        const size_t length = (size_t)(random & UINT32_C(0xff));
        for (index = 0u; index < length; ++index) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            mutated[index] = (uint8_t)random;
        }
        if (hid_report_compile(mutated, length, &plan) == HID_COMPILE_OK) {
            exercise_compiled_plan(&plan);
        }
    }
    return 0;
}

int test_hid_report_parser(void)
{
    CHECK(test_boot_keyboard_no_id() == 0);
    CHECK(test_split_report_keyboard() == 0);
    CHECK(test_nkro_rollover_and_release() == 0);
    CHECK(test_mx_mechanical_empirical_descriptor() == 0);
    CHECK(test_consumer_collection_is_ignored() == 0);
    CHECK(test_mouse_reports() == 0);
    CHECK(test_mouse_16_bit_motion_and_pan() == 0);
    CHECK(test_packet_framing_errors() == 0);
    CHECK(test_malformed_and_truncated_maps() == 0);
    CHECK(test_limits() == 0);
    CHECK(test_id_zero_roles_and_absolute_mouse() == 0);
    CHECK(test_descriptor_mutation_smoke() == 0);
    return 0;
}
