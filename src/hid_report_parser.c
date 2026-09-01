#include "hid_report_parser.h"

#include <stdbool.h>
#include <limits.h>
#include <string.h>

#define HID_RUNTIME_COOKIE UINT32_C(0x58485052)

enum {
    ITEM_TYPE_MAIN = 0,
    ITEM_TYPE_GLOBAL = 1,
    ITEM_TYPE_LOCAL = 2,
    MAIN_INPUT = 8,
    MAIN_OUTPUT = 9,
    MAIN_COLLECTION = 10,
    MAIN_FEATURE = 11,
    MAIN_END_COLLECTION = 12,
    GLOBAL_USAGE_PAGE = 0,
    GLOBAL_LOGICAL_MINIMUM = 1,
    GLOBAL_LOGICAL_MAXIMUM = 2,
    GLOBAL_PHYSICAL_MINIMUM = 3,
    GLOBAL_PHYSICAL_MAXIMUM = 4,
    GLOBAL_UNIT_EXPONENT = 5,
    GLOBAL_UNIT = 6,
    GLOBAL_REPORT_SIZE = 7,
    GLOBAL_REPORT_ID = 8,
    GLOBAL_REPORT_COUNT = 9,
    GLOBAL_PUSH = 10,
    GLOBAL_POP = 11,
    LOCAL_USAGE = 0,
    LOCAL_USAGE_MINIMUM = 1,
    LOCAL_USAGE_MAXIMUM = 2,
    LOCAL_DELIMITER = 10,
    COLLECTION_APPLICATION = 1,
    USAGE_PAGE_GENERIC_DESKTOP = 0x01,
    USAGE_PAGE_KEYBOARD = 0x07,
    USAGE_PAGE_BUTTON = 0x09,
    USAGE_PAGE_CONSUMER = 0x0c,
    USAGE_MOUSE = 0x02,
    USAGE_KEYBOARD = 0x06,
    USAGE_KEYPAD = 0x07,
    USAGE_X = 0x30,
    USAGE_Y = 0x31,
    USAGE_WHEEL = 0x38,
    USAGE_CONSUMER_CONTROL = 0x01,
    USAGE_AC_PAN = 0x0238,
    INPUT_CONSTANT = 0x01,
    INPUT_VARIABLE = 0x02,
    INPUT_RELATIVE = 0x04,
    ROLE_MASK_KEYBOARD = 0x01,
    ROLE_MASK_MOUSE = 0x02
};

typedef enum {
    APP_OTHER = 0,
    APP_KEYBOARD,
    APP_MOUSE,
    APP_CONSUMER
} application_kind_t;

typedef struct {
    uint16_t usage_page;
    int32_t logical_minimum;
    uint32_t logical_maximum_raw;
    int32_t physical_minimum;
    uint32_t physical_maximum_raw;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t logical_maximum_size;
    uint8_t physical_maximum_size;
    uint8_t report_id;
    bool usage_page_set;
    bool logical_minimum_set;
    bool logical_maximum_set;
    bool physical_minimum_set;
    bool physical_maximum_set;
    bool report_size_set;
    bool report_count_set;
} global_state_t;

typedef struct {
    uint32_t usages[HID_REPORT_MAX_FIELDS];
    uint16_t usage_count;
    uint32_t usage_minimum;
    uint32_t usage_maximum;
    bool usage_minimum_set;
    bool usage_maximum_set;
} local_state_t;

typedef struct {
    application_kind_t application;
} collection_state_t;

typedef struct {
    hid_report_plan_t *plan;
    global_state_t globals;
    global_state_t global_stack[HID_REPORT_MAX_GLOBAL_PUSH_DEPTH];
    local_state_t locals;
    collection_state_t collections[HID_REPORT_MAX_COLLECTION_DEPTH];
    uint8_t global_depth;
    uint8_t collection_depth;
    uint8_t declared_report_ids[HID_REPORT_MAX_IDS];
    uint8_t declared_report_id_count;
    bool saw_explicit_report_id;
    bool saw_implicit_input;
    bool keyboard_application_seen;
    bool mouse_application_seen;
    bool keyboard_invalid;
    bool mouse_invalid;
    bool mouse_x_seen;
    bool mouse_y_seen;
    uint16_t keyboard_field_count;
} parser_t;

static bool checked_add_size(size_t left, size_t right, size_t *out)
{
    if (left > SIZE_MAX - right) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool checked_mul_size(size_t left, size_t right, size_t *out)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }
    *out = left * right;
    return true;
}

static uint32_t read_unsigned(const uint8_t *bytes, uint8_t size)
{
    uint32_t value = 0u;
    uint8_t index;

    for (index = 0u; index < size; ++index) {
        value |= (uint32_t)bytes[index] << (8u * index);
    }
    return value;
}

static bool decode_signed(uint32_t raw, uint8_t size, int32_t *out)
{
    uint32_t sign_bit;
    uint32_t mask;

    if (size == 0u || size > 4u) {
        return false;
    }
    sign_bit = UINT32_C(1) << ((uint32_t)size * 8u - 1u);
    mask = size == 4u ? UINT32_MAX
                      : (UINT32_C(1) << ((uint32_t)size * 8u)) - 1u;
    raw &= mask;
    if ((raw & sign_bit) == 0u) {
        if (raw > (uint32_t)INT32_MAX) {
            return false;
        }
        *out = (int32_t)raw;
    } else if (raw == UINT32_C(0x80000000)) {
        *out = INT32_MIN;
    } else {
        const uint32_t magnitude = ((~raw) & mask) + 1u;
        if (magnitude > (uint32_t)INT32_MAX) {
            return false;
        }
        *out = -(int32_t)magnitude;
    }
    return true;
}

static bool decode_maximum(uint32_t raw, uint8_t size, int32_t minimum,
                           int32_t *out)
{
    if (minimum < 0) {
        return decode_signed(raw, size, out);
    }
    if (raw > (uint32_t)INT32_MAX) {
        return false;
    }
    *out = (int32_t)raw;
    return true;
}

static uint32_t make_usage(uint16_t global_page, uint32_t raw, uint8_t size)
{
    if (size == 4u) {
        return raw;
    }
    return ((uint32_t)global_page << 16) | (raw & UINT32_C(0xffff));
}

static uint16_t usage_page(uint32_t usage)
{
    return (uint16_t)(usage >> 16);
}

static uint16_t usage_id(uint32_t usage)
{
    return (uint16_t)(usage & UINT32_C(0xffff));
}

static void clear_locals(local_state_t *locals)
{
    memset(locals, 0, sizeof(*locals));
}

static hid_compile_result_t validate_locals(const local_state_t *locals)
{
    if (locals->usage_minimum_set != locals->usage_maximum_set) {
        return HID_COMPILE_MALFORMED;
    }
    if (locals->usage_minimum_set) {
        if (locals->usage_count != 0u ||
            usage_page(locals->usage_minimum) !=
                usage_page(locals->usage_maximum) ||
            usage_id(locals->usage_minimum) > usage_id(locals->usage_maximum)) {
            return HID_COMPILE_MALFORMED;
        }
    }
    return HID_COMPILE_OK;
}

static bool resolve_usage(const local_state_t *locals, uint32_t index,
                          uint32_t *out_usage)
{
    if (locals->usage_count != 0u) {
        const uint32_t selected =
            index < locals->usage_count ? index : locals->usage_count - 1u;
        *out_usage = locals->usages[selected];
        return true;
    }
    if (locals->usage_minimum_set) {
        const uint32_t minimum = usage_id(locals->usage_minimum);
        const uint32_t maximum = usage_id(locals->usage_maximum);
        uint32_t selected = minimum + index;
        if (selected > maximum) {
            selected = maximum;
        }
        *out_usage = ((uint32_t)usage_page(locals->usage_minimum) << 16) |
                     selected;
        return true;
    }
    return false;
}

static application_kind_t current_application(const parser_t *parser)
{
    if (parser->collection_depth == 0u) {
        return APP_OTHER;
    }
    return parser->collections[parser->collection_depth - 1u].application;
}

static hid_compile_result_t find_or_add_report(parser_t *parser,
                                               uint8_t report_id,
                                               uint8_t *out_index)
{
    uint8_t index;

    for (index = 0u; index < parser->plan->report_count; ++index) {
        if (parser->plan->reports[index].report_id == report_id) {
            *out_index = index;
            return HID_COMPILE_OK;
        }
    }
    if (parser->plan->report_count >= HID_REPORT_MAX_IDS) {
        return HID_COMPILE_LIMIT_EXCEEDED;
    }
    index = parser->plan->report_count;
    parser->plan->report_count++;
    parser->plan->reports[index].report_id = report_id;
    parser->plan->reports[index].bit_length = 0u;
    parser->plan->reports[index].payload_length = 0u;
    parser->plan->reports[index].role_relevant = 0u;
    *out_index = index;
    return HID_COMPILE_OK;
}

static hid_compile_result_t add_field(parser_t *parser,
                                      uint8_t report_index,
                                      hid_report_field_kind_t kind,
                                      size_t bit_offset,
                                      uint32_t bit_width,
                                      uint32_t count,
                                      uint16_t usage,
                                      int32_t logical_minimum,
                                      int32_t logical_maximum)
{
    hid_report_field_t *field;

    if (parser->plan->field_count >= HID_REPORT_MAX_FIELDS) {
        return HID_COMPILE_LIMIT_EXCEEDED;
    }
    if (bit_offset > UINT16_MAX || bit_width == 0u || bit_width > 16u ||
        count == 0u || count > UINT16_MAX) {
        return HID_COMPILE_LIMIT_EXCEEDED;
    }
    field = &parser->plan->fields[parser->plan->field_count];
    parser->plan->field_count++;
    field->bit_offset = (uint16_t)bit_offset;
    field->count = (uint16_t)count;
    field->logical_minimum = logical_minimum;
    field->logical_maximum = logical_maximum;
    field->usage = usage;
    field->report_index = report_index;
    field->bit_width = (uint8_t)bit_width;
    field->kind = (uint8_t)kind;
    return HID_COMPILE_OK;
}

static bool mouse_axis_semantics(uint32_t flags, uint32_t width,
                                 int32_t logical_minimum,
                                 int32_t logical_maximum)
{
    return (flags & INPUT_VARIABLE) != 0u &&
           (flags & INPUT_RELATIVE) != 0u &&
           (width == 8u || width == 16u) && logical_minimum < 0 &&
           logical_maximum > 0;
}

static hid_compile_result_t process_keyboard_input(
    parser_t *parser, uint8_t report_index, size_t base_offset,
    uint32_t flags, uint32_t width, uint32_t count,
    int32_t logical_minimum, int32_t logical_maximum)
{
    uint32_t field_index;

    if ((flags & INPUT_VARIABLE) == 0u) {
        uint32_t usage;
        uint32_t index;

        if ((flags & INPUT_RELATIVE) != 0u || width != 8u ||
            logical_minimum < 0 || logical_maximum > 255 ||
            !resolve_usage(&parser->locals, 0u, &usage)) {
            parser->keyboard_invalid = true;
            return HID_COMPILE_OK;
        }
        for (index = 0u; index < count; ++index) {
            uint32_t item_usage;
            if (!resolve_usage(&parser->locals, index, &item_usage) ||
                usage_page(item_usage) != USAGE_PAGE_KEYBOARD) {
                parser->keyboard_invalid = true;
                return HID_COMPILE_OK;
            }
        }
        parser->keyboard_field_count++;
        parser->plan->reports[report_index].role_relevant |= ROLE_MASK_KEYBOARD;
        return add_field(parser, report_index, HID_FIELD_KEY_ARRAY,
                         base_offset, width, count, 0u,
                         logical_minimum, logical_maximum);
    }

    for (field_index = 0u; field_index < count; ++field_index) {
        uint32_t usage;
        size_t relative_offset;
        size_t bit_offset;
        hid_compile_result_t result;

        if (!resolve_usage(&parser->locals, field_index, &usage)) {
            if (parser->globals.usage_page == USAGE_PAGE_KEYBOARD) {
                parser->keyboard_invalid = true;
            }
            continue;
        }
        if (usage_page(usage) != USAGE_PAGE_KEYBOARD) {
            continue;
        }
        if ((flags & INPUT_RELATIVE) != 0u || width != 1u ||
            logical_minimum != 0 || logical_maximum != 1 ||
            usage_id(usage) > UINT8_MAX) {
            parser->keyboard_invalid = true;
            continue;
        }
        if (usage_id(usage) == 0u) {
            continue;
        }
        if (!checked_mul_size(field_index, width, &relative_offset) ||
            !checked_add_size(base_offset, relative_offset, &bit_offset)) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        result = add_field(parser, report_index, HID_FIELD_KEY_BIT,
                           bit_offset, width, 1u, usage_id(usage),
                           logical_minimum, logical_maximum);
        if (result != HID_COMPILE_OK) {
            return result;
        }
        parser->keyboard_field_count++;
        parser->plan->reports[report_index].role_relevant |= ROLE_MASK_KEYBOARD;
    }
    return HID_COMPILE_OK;
}

static hid_compile_result_t process_mouse_input(
    parser_t *parser, uint8_t report_index, size_t base_offset,
    uint32_t flags, uint32_t width, uint32_t count,
    int32_t logical_minimum, int32_t logical_maximum,
    application_kind_t application)
{
    uint32_t field_index;

    for (field_index = 0u; field_index < count; ++field_index) {
        uint32_t usage;
        uint16_t page;
        uint16_t id;
        hid_report_field_kind_t kind = HID_FIELD_MOUSE_BUTTON;
        bool add = false;
        size_t relative_offset;
        size_t bit_offset;
        hid_compile_result_t result;

        if (!resolve_usage(&parser->locals, field_index, &usage)) {
            continue;
        }
        page = usage_page(usage);
        id = usage_id(usage);

        if (application == APP_MOUSE && page == USAGE_PAGE_BUTTON &&
            id >= 1u && id <= 5u) {
            if ((flags & INPUT_VARIABLE) == 0u ||
                (flags & INPUT_RELATIVE) != 0u || width != 1u ||
                logical_minimum != 0 || logical_maximum != 1) {
                parser->mouse_invalid = true;
                continue;
            }
            kind = HID_FIELD_MOUSE_BUTTON;
            add = true;
        } else if (application == APP_MOUSE &&
                   page == USAGE_PAGE_GENERIC_DESKTOP &&
                   (id == USAGE_X || id == USAGE_Y || id == USAGE_WHEEL)) {
            if (!mouse_axis_semantics(flags, width, logical_minimum,
                                      logical_maximum)) {
                parser->mouse_invalid = true;
                continue;
            }
            if (id == USAGE_X) {
                kind = HID_FIELD_MOUSE_X;
                parser->mouse_x_seen = true;
            } else if (id == USAGE_Y) {
                kind = HID_FIELD_MOUSE_Y;
                parser->mouse_y_seen = true;
            } else {
                kind = HID_FIELD_MOUSE_WHEEL;
            }
            add = true;
        } else if ((application == APP_MOUSE || application == APP_CONSUMER) &&
                   page == USAGE_PAGE_CONSUMER && id == USAGE_AC_PAN) {
            if (!mouse_axis_semantics(flags, width, logical_minimum,
                                      logical_maximum)) {
                parser->mouse_invalid = true;
                continue;
            }
            kind = HID_FIELD_MOUSE_PAN;
            add = true;
        }

        if (!add) {
            continue;
        }
        if (!checked_mul_size(field_index, width, &relative_offset) ||
            !checked_add_size(base_offset, relative_offset, &bit_offset)) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        result = add_field(parser, report_index, kind, bit_offset, width, 1u,
                           id, logical_minimum, logical_maximum);
        if (result != HID_COMPILE_OK) {
            return result;
        }
        parser->plan->reports[report_index].role_relevant |= ROLE_MASK_MOUSE;
    }
    return HID_COMPILE_OK;
}

static hid_compile_result_t process_input(parser_t *parser, uint32_t flags)
{
    size_t input_bits;
    size_t new_bit_length;
    size_t base_offset;
    int32_t logical_maximum;
    int32_t physical_maximum;
    uint8_t report_index;
    hid_compile_result_t result;
    application_kind_t application;

    if (!parser->globals.report_size_set ||
        !parser->globals.report_count_set ||
        parser->globals.report_size == 0u ||
        parser->globals.report_size > 16u ||
        parser->globals.report_count == 0u) {
        return HID_COMPILE_MALFORMED;
    }
    if ((flags & INPUT_CONSTANT) == 0u) {
        if (!parser->globals.logical_minimum_set ||
            !parser->globals.logical_maximum_set ||
            !decode_maximum(parser->globals.logical_maximum_raw,
                            parser->globals.logical_maximum_size,
                            parser->globals.logical_minimum,
                            &logical_maximum) ||
            parser->globals.logical_minimum > logical_maximum) {
            return HID_COMPILE_MALFORMED;
        }
    } else {
        logical_maximum = 0;
    }
    if (parser->globals.physical_minimum_set !=
        parser->globals.physical_maximum_set) {
        return HID_COMPILE_MALFORMED;
    }
    if (parser->globals.physical_minimum_set &&
        (!decode_maximum(parser->globals.physical_maximum_raw,
                         parser->globals.physical_maximum_size,
                         parser->globals.physical_minimum,
                         &physical_maximum) ||
         parser->globals.physical_minimum > physical_maximum)) {
        return HID_COMPILE_MALFORMED;
    }

    if (parser->globals.report_id == 0u) {
        if (parser->saw_explicit_report_id) {
            return HID_COMPILE_MALFORMED;
        }
        parser->saw_implicit_input = true;
    }
    result = find_or_add_report(parser, parser->globals.report_id,
                                &report_index);
    if (result != HID_COMPILE_OK) {
        return result;
    }
    if (!checked_mul_size(parser->globals.report_size,
                          parser->globals.report_count, &input_bits)) {
        return HID_COMPILE_LIMIT_EXCEEDED;
    }
    base_offset = parser->plan->reports[report_index].bit_length;
    if (!checked_add_size(base_offset, input_bits, &new_bit_length) ||
        new_bit_length > HID_REPORT_INPUT_MAX_SIZE * 8u) {
        return HID_COMPILE_LIMIT_EXCEEDED;
    }

    application = current_application(parser);
    if ((flags & INPUT_CONSTANT) == 0u && application == APP_KEYBOARD) {
        result = process_keyboard_input(
            parser, report_index, base_offset, flags,
            parser->globals.report_size, parser->globals.report_count,
            parser->globals.logical_minimum, logical_maximum);
        if (result != HID_COMPILE_OK) {
            return result;
        }
    } else if ((flags & INPUT_CONSTANT) == 0u &&
               (application == APP_MOUSE || application == APP_CONSUMER)) {
        result = process_mouse_input(
            parser, report_index, base_offset, flags,
            parser->globals.report_size, parser->globals.report_count,
            parser->globals.logical_minimum, logical_maximum, application);
        if (result != HID_COMPILE_OK) {
            return result;
        }
    }

    parser->plan->reports[report_index].bit_length =
        (uint16_t)new_bit_length;
    return HID_COMPILE_OK;
}

static hid_compile_result_t validate_non_input_main(const parser_t *parser)
{
    size_t ignored_bits;

    if (!parser->globals.report_size_set ||
        !parser->globals.report_count_set ||
        parser->globals.report_size == 0u ||
        parser->globals.report_count == 0u ||
        !checked_mul_size(parser->globals.report_size,
                          parser->globals.report_count, &ignored_bits)) {
        return HID_COMPILE_MALFORMED;
    }
    (void)ignored_bits;
    return HID_COMPILE_OK;
}

static application_kind_t classify_application(uint32_t usage)
{
    if (usage_page(usage) == USAGE_PAGE_GENERIC_DESKTOP) {
        if (usage_id(usage) == USAGE_KEYBOARD ||
            usage_id(usage) == USAGE_KEYPAD) {
            return APP_KEYBOARD;
        }
        if (usage_id(usage) == USAGE_MOUSE) {
            return APP_MOUSE;
        }
    }
    if (usage_page(usage) == USAGE_PAGE_CONSUMER &&
        usage_id(usage) == USAGE_CONSUMER_CONTROL) {
        return APP_CONSUMER;
    }
    return APP_OTHER;
}

static hid_compile_result_t process_collection(parser_t *parser,
                                               uint32_t collection_type)
{
    uint32_t usage;
    application_kind_t application = current_application(parser);

    if (collection_type > 6u && collection_type < 0x80u) {
        return HID_COMPILE_MALFORMED;
    }
    if (!resolve_usage(&parser->locals, 0u, &usage)) {
        return HID_COMPILE_MALFORMED;
    }
    if (parser->collection_depth >= HID_REPORT_MAX_COLLECTION_DEPTH) {
        return HID_COMPILE_LIMIT_EXCEEDED;
    }
    if (collection_type == COLLECTION_APPLICATION) {
        if (parser->plan->application_collection_count >=
            HID_REPORT_MAX_APPLICATION_COLLECTIONS) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        parser->plan->application_collection_count++;
        if (parser->collection_depth == 0u) {
            application = classify_application(usage);
            if (application == APP_KEYBOARD) {
                parser->keyboard_application_seen = true;
            } else if (application == APP_MOUSE) {
                parser->mouse_application_seen = true;
            }
        }
    } else if (parser->collection_depth == 0u) {
        application = APP_OTHER;
    }
    parser->collections[parser->collection_depth].application = application;
    parser->collection_depth++;
    return HID_COMPILE_OK;
}

static hid_compile_result_t process_main_item(parser_t *parser, uint8_t tag,
                                              uint32_t value,
                                              uint8_t data_size)
{
    hid_compile_result_t result = validate_locals(&parser->locals);

    if (result != HID_COMPILE_OK) {
        return result;
    }
    switch (tag) {
    case MAIN_INPUT:
        if (data_size == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        result = process_input(parser, value);
        break;
    case MAIN_OUTPUT:
    case MAIN_FEATURE:
        if (data_size == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        result = validate_non_input_main(parser);
        break;
    case MAIN_COLLECTION:
        if (data_size != 1u) {
            return HID_COMPILE_MALFORMED;
        }
        result = process_collection(parser, value & UINT32_C(0xff));
        break;
    case MAIN_END_COLLECTION:
        if (data_size != 0u || parser->collection_depth == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        parser->collection_depth--;
        result = HID_COMPILE_OK;
        break;
    default:
        return HID_COMPILE_MALFORMED;
    }
    clear_locals(&parser->locals);
    return result;
}

static bool add_declared_report_id(parser_t *parser, uint8_t report_id)
{
    uint8_t index;

    for (index = 0u; index < parser->declared_report_id_count; ++index) {
        if (parser->declared_report_ids[index] == report_id) {
            return true;
        }
    }
    if (parser->declared_report_id_count >= HID_REPORT_MAX_IDS) {
        return false;
    }
    parser->declared_report_ids[parser->declared_report_id_count] = report_id;
    parser->declared_report_id_count++;
    return true;
}

static hid_compile_result_t process_global_item(parser_t *parser, uint8_t tag,
                                                uint32_t value,
                                                uint8_t data_size)
{
    int32_t signed_value;

    switch (tag) {
    case GLOBAL_USAGE_PAGE:
        if (data_size == 0u || value > UINT16_MAX) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.usage_page = (uint16_t)value;
        parser->globals.usage_page_set = true;
        break;
    case GLOBAL_LOGICAL_MINIMUM:
        if (!decode_signed(value, data_size, &signed_value)) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.logical_minimum = signed_value;
        parser->globals.logical_minimum_set = true;
        break;
    case GLOBAL_LOGICAL_MAXIMUM:
        if (data_size == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.logical_maximum_raw = value;
        parser->globals.logical_maximum_size = data_size;
        parser->globals.logical_maximum_set = true;
        break;
    case GLOBAL_PHYSICAL_MINIMUM:
        if (!decode_signed(value, data_size, &signed_value)) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.physical_minimum = signed_value;
        parser->globals.physical_minimum_set = true;
        break;
    case GLOBAL_PHYSICAL_MAXIMUM:
        if (data_size == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.physical_maximum_raw = value;
        parser->globals.physical_maximum_size = data_size;
        parser->globals.physical_maximum_set = true;
        break;
    case GLOBAL_UNIT_EXPONENT:
    case GLOBAL_UNIT:
        if (data_size == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        break;
    case GLOBAL_REPORT_SIZE:
        if (data_size == 0u || value == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.report_size = value;
        parser->globals.report_size_set = true;
        break;
    case GLOBAL_REPORT_ID:
        if (data_size != 1u || value == 0u || value > UINT8_MAX ||
            parser->saw_implicit_input) {
            return HID_COMPILE_MALFORMED;
        }
        if (!add_declared_report_id(parser, (uint8_t)value)) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        parser->globals.report_id = (uint8_t)value;
        parser->saw_explicit_report_id = true;
        break;
    case GLOBAL_REPORT_COUNT:
        if (data_size == 0u || value == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        parser->globals.report_count = value;
        parser->globals.report_count_set = true;
        break;
    case GLOBAL_PUSH:
        if (data_size != 0u) {
            return HID_COMPILE_MALFORMED;
        }
        if (parser->global_depth >= HID_REPORT_MAX_GLOBAL_PUSH_DEPTH) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        parser->global_stack[parser->global_depth] = parser->globals;
        parser->global_depth++;
        break;
    case GLOBAL_POP:
        if (data_size != 0u || parser->global_depth == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        parser->global_depth--;
        parser->globals = parser->global_stack[parser->global_depth];
        break;
    default:
        return HID_COMPILE_MALFORMED;
    }
    return HID_COMPILE_OK;
}

static hid_compile_result_t process_local_item(parser_t *parser, uint8_t tag,
                                               uint32_t value,
                                               uint8_t data_size)
{
    uint32_t usage;

    switch (tag) {
    case LOCAL_USAGE:
        if (data_size == 0u || !parser->globals.usage_page_set) {
            return HID_COMPILE_MALFORMED;
        }
        if (parser->locals.usage_count >= HID_REPORT_MAX_FIELDS) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        usage = make_usage(parser->globals.usage_page, value, data_size);
        parser->locals.usages[parser->locals.usage_count] = usage;
        parser->locals.usage_count++;
        break;
    case LOCAL_USAGE_MINIMUM:
        if (data_size == 0u || !parser->globals.usage_page_set ||
            parser->locals.usage_minimum_set) {
            return HID_COMPILE_MALFORMED;
        }
        parser->locals.usage_minimum =
            make_usage(parser->globals.usage_page, value, data_size);
        parser->locals.usage_minimum_set = true;
        break;
    case LOCAL_USAGE_MAXIMUM:
        if (data_size == 0u || !parser->globals.usage_page_set ||
            parser->locals.usage_maximum_set) {
            return HID_COMPILE_MALFORMED;
        }
        parser->locals.usage_maximum =
            make_usage(parser->globals.usage_page, value, data_size);
        parser->locals.usage_maximum_set = true;
        break;
    case LOCAL_DELIMITER:
        return HID_COMPILE_MALFORMED;
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 9:
        if (data_size == 0u) {
            return HID_COMPILE_MALFORMED;
        }
        break;
    default:
        return HID_COMPILE_MALFORMED;
    }
    return HID_COMPILE_OK;
}

static bool field_matches_role(const hid_report_field_t *field,
                               hid_report_role_t role)
{
    if (role == HID_REPORT_ROLE_KEYBOARD) {
        return field->kind == HID_FIELD_KEY_ARRAY ||
               field->kind == HID_FIELD_KEY_BIT;
    }
    return field->kind >= HID_FIELD_MOUSE_BUTTON &&
           field->kind <= HID_FIELD_MOUSE_PAN;
}

static hid_compile_result_t finalize_plan(parser_t *parser)
{
    uint16_t read_index;
    uint16_t write_index = 0u;
    uint8_t report_index;

    if (parser->collection_depth != 0u || parser->global_depth != 0u ||
        validate_locals(&parser->locals) != HID_COMPILE_OK) {
        return HID_COMPILE_MALFORMED;
    }
    if (parser->keyboard_application_seen && parser->mouse_application_seen) {
        return HID_COMPILE_COMBO_ROLE;
    }
    if (parser->keyboard_application_seen) {
        if (parser->keyboard_invalid || parser->keyboard_field_count == 0u) {
            return HID_COMPILE_UNSUPPORTED_ROLE;
        }
        parser->plan->role = HID_REPORT_ROLE_KEYBOARD;
    } else if (parser->mouse_application_seen) {
        if (parser->mouse_invalid || !parser->mouse_x_seen ||
            !parser->mouse_y_seen) {
            return HID_COMPILE_UNSUPPORTED_ROLE;
        }
        parser->plan->role = HID_REPORT_ROLE_MOUSE;
    } else {
        return HID_COMPILE_UNSUPPORTED_ROLE;
    }
    if (parser->plan->report_count == 0u) {
        return HID_COMPILE_UNSUPPORTED_ROLE;
    }

    for (report_index = 0u; report_index < parser->plan->report_count;
         ++report_index) {
        hid_report_definition_t *report = &parser->plan->reports[report_index];
        const uint16_t bytes = (uint16_t)((report->bit_length + 7u) / 8u);
        if (bytes > HID_REPORT_INPUT_MAX_SIZE) {
            return HID_COMPILE_LIMIT_EXCEEDED;
        }
        report->payload_length = (uint8_t)bytes;
        report->role_relevant = 0u;
    }

    for (read_index = 0u; read_index < parser->plan->field_count;
         ++read_index) {
        const hid_report_field_t field = parser->plan->fields[read_index];
        if (field_matches_role(&field, parser->plan->role)) {
            parser->plan->fields[write_index] = field;
            parser->plan->reports[field.report_index].role_relevant = 1u;
            write_index++;
        }
    }
    parser->plan->field_count = write_index;
    return HID_COMPILE_OK;
}

static hid_compile_result_t compile_descriptor(const uint8_t *report_map,
                                               size_t report_map_length,
                                               hid_report_plan_t *plan)
{
    /*
     * Descriptor compilation is serialized by the Core-1 connection manager.
     * Keep the 1+ KiB bounded scratch area in BSS rather than on Core-1's
     * callback stack. This function is intentionally non-reentrant.
     */
    static parser_t parser;
    size_t offset = 0u;

    memset(&parser, 0, sizeof(parser));
    parser.plan = plan;

    while (offset < report_map_length) {
        uint8_t prefix = report_map[offset];
        uint8_t size_code;
        uint8_t data_size;
        uint8_t type;
        uint8_t tag;
        size_t item_end;
        uint32_t value;
        hid_compile_result_t result;

        offset++;
        if (prefix == UINT8_C(0xfe)) {
            return HID_COMPILE_LONG_ITEM;
        }
        size_code = prefix & UINT8_C(0x03);
        data_size = size_code == 3u ? 4u : size_code;
        type = (prefix >> 2) & UINT8_C(0x03);
        tag = prefix >> 4;
        if (type == 3u) {
            return HID_COMPILE_MALFORMED;
        }
        if (!checked_add_size(offset, data_size, &item_end) ||
            item_end > report_map_length) {
            return HID_COMPILE_TRUNCATED;
        }
        value = read_unsigned(&report_map[offset], data_size);
        offset = item_end;

        if (type == ITEM_TYPE_MAIN) {
            result = process_main_item(&parser, tag, value, data_size);
        } else if (type == ITEM_TYPE_GLOBAL) {
            result = process_global_item(&parser, tag, value, data_size);
        } else {
            result = process_local_item(&parser, tag, value, data_size);
        }
        if (result != HID_COMPILE_OK) {
            return result;
        }
    }
    return finalize_plan(&parser);
}

hid_compile_result_t hid_report_compile(const uint8_t *report_map,
                                        size_t report_map_length,
                                        hid_report_plan_t *out_plan)
{
    hid_compile_result_t result;

    if (out_plan == NULL || (report_map == NULL && report_map_length != 0u)) {
        return HID_COMPILE_BAD_ARGUMENT;
    }
    memset(out_plan, 0, sizeof(*out_plan));
    if (report_map_length == 0u) {
        return HID_COMPILE_EMPTY;
    }
    if (report_map_length > HID_REPORT_MAP_MAX_SIZE) {
        return HID_COMPILE_MAP_TOO_LARGE;
    }
    result = compile_descriptor(report_map, report_map_length, out_plan);
    if (result != HID_COMPILE_OK) {
        memset(out_plan, 0, sizeof(*out_plan));
    }
    return result;
}

hid_compile_result_t hid_report_compile_for_role(const uint8_t *report_map,
                                                 size_t report_map_length,
                                                 hid_report_role_t expected_role,
                                                 hid_report_plan_t *out_plan)
{
    hid_compile_result_t result;

    if (expected_role != HID_REPORT_ROLE_KEYBOARD &&
        expected_role != HID_REPORT_ROLE_MOUSE) {
        return HID_COMPILE_BAD_ARGUMENT;
    }
    result = hid_report_compile(report_map, report_map_length, out_plan);
    if (result == HID_COMPILE_OK && out_plan->role != expected_role) {
        memset(out_plan, 0, sizeof(*out_plan));
        return HID_COMPILE_ROLE_MISMATCH;
    }
    return result;
}

void hid_report_runtime_init(hid_report_runtime_t *runtime,
                             const hid_report_plan_t *plan)
{
    uint8_t index;

    if (runtime == NULL) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    if (plan == NULL || plan->report_count > HID_REPORT_MAX_IDS ||
        (plan->role != HID_REPORT_ROLE_KEYBOARD &&
         plan->role != HID_REPORT_ROLE_MOUSE)) {
        return;
    }
    runtime->cookie = HID_RUNTIME_COOKIE;
    runtime->role = plan->role;
    runtime->report_count = plan->report_count;
    for (index = 0u; index < plan->report_count; ++index) {
        runtime->report_ids[index] = plan->reports[index].report_id;
    }
}

static bool runtime_matches_plan(const hid_report_runtime_t *runtime,
                                 const hid_report_plan_t *plan)
{
    uint8_t index;

    if (runtime->cookie != HID_RUNTIME_COOKIE || runtime->role != plan->role ||
        runtime->report_count != plan->report_count) {
        return false;
    }
    for (index = 0u; index < plan->report_count; ++index) {
        if (runtime->report_ids[index] != plan->reports[index].report_id) {
            return false;
        }
    }
    return true;
}

static bool extract_raw(const uint8_t *payload, size_t payload_length,
                        size_t bit_offset, uint8_t bit_width,
                        uint32_t *out_value)
{
    size_t payload_bits;
    size_t field_end;
    uint32_t value = 0u;
    uint8_t bit;

    if (!checked_mul_size(payload_length, 8u, &payload_bits) ||
        !checked_add_size(bit_offset, bit_width, &field_end) ||
        field_end > payload_bits || bit_width == 0u || bit_width > 16u) {
        return false;
    }
    for (bit = 0u; bit < bit_width; ++bit) {
        const size_t source_bit = bit_offset + bit;
        const uint8_t source = payload[source_bit / 8u];
        if ((source & (uint8_t)(1u << (source_bit % 8u))) != 0u) {
            value |= UINT32_C(1) << bit;
        }
    }
    *out_value = value;
    return true;
}

static bool extract_value(const uint8_t *payload, size_t payload_length,
                          size_t bit_offset,
                          const hid_report_field_t *field,
                          int32_t *out_value)
{
    uint32_t raw;
    int32_t value;

    if (!extract_raw(payload, payload_length, bit_offset, field->bit_width,
                     &raw)) {
        return false;
    }
    if (field->logical_minimum < 0) {
        const uint32_t sign = UINT32_C(1) << (field->bit_width - 1u);
        if ((raw & sign) != 0u) {
            const uint32_t mask =
                (UINT32_C(1) << field->bit_width) - 1u;
            const uint32_t magnitude = ((~raw) & mask) + 1u;
            value = -(int32_t)magnitude;
        } else {
            value = (int32_t)raw;
        }
    } else {
        value = (int32_t)raw;
    }
    if (value < field->logical_minimum || value > field->logical_maximum) {
        return false;
    }
    *out_value = value;
    return true;
}

static void set_keyboard_usage(uint8_t keys[32], uint8_t *modifiers,
                               uint16_t usage)
{
    if (usage >= 0xe0u && usage <= 0xe7u) {
        *modifiers |= (uint8_t)(1u << (usage - 0xe0u));
    } else if (usage != 0u && usage <= UINT8_MAX) {
        keys[usage / 8u] |= (uint8_t)(1u << (usage % 8u));
    }
}

static hid_normalize_result_t normalize_keyboard(
    const hid_report_plan_t *plan, hid_report_runtime_t *runtime,
    uint8_t report_index, const uint8_t *payload, size_t payload_length,
    hid_normalized_report_t *out_report)
{
    uint8_t keys[32] = {0};
    uint8_t modifiers = 0u;
    uint16_t field_index;
    uint8_t report;
    uint8_t byte_index;
    uint16_t usage;
    uint8_t output_count = 0u;

    for (field_index = 0u; field_index < plan->field_count; ++field_index) {
        const hid_report_field_t *field = &plan->fields[field_index];
        if (field->report_index != report_index) {
            continue;
        }
        if (field->kind == HID_FIELD_KEY_ARRAY) {
            uint16_t element;
            for (element = 0u; element < field->count; ++element) {
                size_t relative_offset;
                size_t bit_offset;
                int32_t value;
                if (!checked_mul_size(element, field->bit_width,
                                      &relative_offset) ||
                    !checked_add_size(field->bit_offset, relative_offset,
                                      &bit_offset) ||
                    !extract_value(payload, payload_length, bit_offset, field,
                                   &value)) {
                    return HID_NORMALIZE_VALUE_OUT_OF_RANGE;
                }
                set_keyboard_usage(keys, &modifiers, (uint16_t)value);
            }
        } else if (field->kind == HID_FIELD_KEY_BIT) {
            int32_t value;
            if (!extract_value(payload, payload_length, field->bit_offset,
                               field, &value)) {
                return HID_NORMALIZE_VALUE_OUT_OF_RANGE;
            }
            if (value != 0) {
                set_keyboard_usage(keys, &modifiers, field->usage);
            }
        }
    }

    runtime->keyboard_modifiers[report_index] = modifiers;
    for (byte_index = 0u; byte_index < sizeof(keys); ++byte_index) {
        runtime->keyboard_keys[report_index][byte_index] = keys[byte_index];
    }
    out_report->role = HID_REPORT_ROLE_KEYBOARD;
    out_report->data.keyboard.modifier = 0u;
    out_report->data.keyboard.reserved = 0u;
    memset(out_report->data.keyboard.keys, 0,
           sizeof(out_report->data.keyboard.keys));

    for (report = 0u; report < plan->report_count; ++report) {
        out_report->data.keyboard.modifier |=
            runtime->keyboard_modifiers[report];
    }
    for (usage = 1u; usage <= UINT8_MAX; ++usage) {
        bool active = false;
        for (report = 0u; report < plan->report_count; ++report) {
            if ((runtime->keyboard_keys[report][usage / 8u] &
                 (uint8_t)(1u << (usage % 8u))) != 0u) {
                active = true;
                break;
            }
        }
        if (!active) {
            continue;
        }
        if (output_count < HID_REPORT_KEYBOARD_KEYS) {
            out_report->data.keyboard.keys[output_count] = (uint8_t)usage;
        }
        output_count++;
    }
    if (output_count > HID_REPORT_KEYBOARD_KEYS) {
        memset(out_report->data.keyboard.keys, 0x01,
               sizeof(out_report->data.keyboard.keys));
    }
    return HID_NORMALIZE_OK;
}

static bool checked_add_int32(int32_t left, int32_t right, int32_t *out)
{
    if ((right > 0 && left > INT32_MAX - right) ||
        (right < 0 && left < INT32_MIN - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static hid_normalize_result_t normalize_mouse(
    const hid_report_plan_t *plan, hid_report_runtime_t *runtime,
    uint8_t report_index, const uint8_t *payload, size_t payload_length,
    hid_normalized_report_t *out_report)
{
    uint8_t buttons = 0u;
    bool has_buttons = false;
    int32_t x = 0;
    int32_t y = 0;
    int32_t wheel = 0;
    int32_t pan = 0;
    uint16_t field_index;
    uint8_t report;

    for (field_index = 0u; field_index < plan->field_count; ++field_index) {
        const hid_report_field_t *field = &plan->fields[field_index];
        int32_t value;
        int32_t *accumulator = NULL;

        if (field->report_index != report_index) {
            continue;
        }
        if (!extract_value(payload, payload_length, field->bit_offset, field,
                           &value)) {
            return HID_NORMALIZE_VALUE_OUT_OF_RANGE;
        }
        switch (field->kind) {
        case HID_FIELD_MOUSE_BUTTON:
            has_buttons = true;
            if (value != 0) {
                buttons |= (uint8_t)(1u << (field->usage - 1u));
            }
            break;
        case HID_FIELD_MOUSE_X:
            accumulator = &x;
            break;
        case HID_FIELD_MOUSE_Y:
            accumulator = &y;
            break;
        case HID_FIELD_MOUSE_WHEEL:
            accumulator = &wheel;
            break;
        case HID_FIELD_MOUSE_PAN:
            accumulator = &pan;
            break;
        default:
            return HID_NORMALIZE_STATE_MISMATCH;
        }
        if (accumulator != NULL &&
            !checked_add_int32(*accumulator, value, accumulator)) {
            return HID_NORMALIZE_ARITHMETIC_OVERFLOW;
        }
    }

    if (has_buttons) {
        runtime->mouse_buttons[report_index] = buttons;
    }
    out_report->role = HID_REPORT_ROLE_MOUSE;
    out_report->data.mouse.buttons = 0u;
    for (report = 0u; report < plan->report_count; ++report) {
        out_report->data.mouse.buttons |= runtime->mouse_buttons[report];
    }
    out_report->data.mouse.x = x;
    out_report->data.mouse.y = y;
    out_report->data.mouse.wheel = wheel;
    out_report->data.mouse.pan = pan;
    return HID_NORMALIZE_OK;
}

hid_normalize_result_t hid_report_normalize(
    const hid_report_plan_t *plan,
    hid_report_runtime_t *runtime,
    uint8_t event_report_id,
    const uint8_t *btstack_report,
    size_t btstack_report_length,
    hid_normalized_report_t *out_report)
{
    uint8_t report_index;
    const hid_report_definition_t *report = NULL;

    if (plan == NULL || runtime == NULL || btstack_report == NULL ||
        out_report == NULL || plan->report_count > HID_REPORT_MAX_IDS ||
        plan->field_count > HID_REPORT_MAX_FIELDS) {
        return HID_NORMALIZE_BAD_ARGUMENT;
    }
    memset(out_report, 0, sizeof(*out_report));
    if (!runtime_matches_plan(runtime, plan)) {
        return HID_NORMALIZE_STATE_MISMATCH;
    }
    if (btstack_report_length == 0u ||
        btstack_report_length > HID_REPORT_INPUT_MAX_SIZE + 1u) {
        return HID_NORMALIZE_BAD_LENGTH;
    }
    if (btstack_report[0] != event_report_id) {
        return HID_NORMALIZE_BAD_PREFIX;
    }
    for (report_index = 0u; report_index < plan->report_count;
         ++report_index) {
        if (plan->reports[report_index].report_id == event_report_id) {
            report = &plan->reports[report_index];
            break;
        }
    }
    if (report == NULL) {
        return HID_NORMALIZE_UNKNOWN_REPORT_ID;
    }
    if (btstack_report_length != (size_t)report->payload_length + 1u) {
        return HID_NORMALIZE_BAD_LENGTH;
    }
    if (report->role_relevant == 0u) {
        return HID_NORMALIZE_IGNORED;
    }
    if (plan->role == HID_REPORT_ROLE_KEYBOARD) {
        return normalize_keyboard(plan, runtime, report_index,
                                  &btstack_report[1],
                                  report->payload_length, out_report);
    }
    if (plan->role == HID_REPORT_ROLE_MOUSE) {
        return normalize_mouse(plan, runtime, report_index,
                               &btstack_report[1], report->payload_length,
                               out_report);
    }
    return HID_NORMALIZE_STATE_MISMATCH;
}
