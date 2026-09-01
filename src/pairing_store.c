#include "pairing_store.h"

#include <stdbool.h>

enum {
    OFFSET_MAGIC = 0,
    OFFSET_VERSION = 4,
    OFFSET_LENGTH = 6,
    OFFSET_ROLE = 8,
    OFFSET_ADDRESS_TYPE = 9,
    OFFSET_RESERVED_0 = 10,
    OFFSET_ADDRESS = 12,
    OFFSET_RESERVED_1 = 18,
    OFFSET_CRC = 20,
    CRC_INPUT_LENGTH = 20
};

static bool valid_role(pairing_role_t role)
{
    return role == PAIRING_ROLE_KEYBOARD || role == PAIRING_ROLE_MOUSE;
}

static bool valid_address_type(pairing_address_type_t address_type)
{
    return address_type == PAIRING_ADDRESS_PUBLIC_IDENTITY ||
           address_type == PAIRING_ADDRESS_RANDOM_IDENTITY;
}

static bool valid_address(const uint8_t address[6])
{
    size_t index;
    bool all_zero = true;
    bool all_ff = true;

    for (index = 0u; index < 6u; ++index) {
        if (address[index] != 0u) {
            all_zero = false;
        }
        if (address[index] != UINT8_MAX) {
            all_ff = false;
        }
    }
    return !all_zero && !all_ff;
}

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(0x00ff));
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & UINT32_C(0x000000ff));
    bytes[1] = (uint8_t)((value >> 8) & UINT32_C(0x000000ff));
    bytes[2] = (uint8_t)((value >> 16) & UINT32_C(0x000000ff));
    bytes[3] = (uint8_t)(value >> 24);
}

uint32_t pairing_store_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    if (data == NULL && length != 0u) {
        return 0u;
    }

    for (index = 0u; index < length; ++index) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)(0u - (crc & 1u));
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

pairing_store_result_t pairing_store_encode(
    const pairing_role_record_t *record,
    uint8_t out_bytes[PAIRING_STORE_RECORD_SIZE])
{
    size_t index;
    uint32_t crc;

    if (record == NULL || out_bytes == NULL) {
        return PAIRING_STORE_BAD_ARGUMENT;
    }
    if (!valid_role(record->role)) {
        return PAIRING_STORE_BAD_ROLE;
    }
    if (!valid_address_type(record->address_type)) {
        return PAIRING_STORE_BAD_ADDRESS_TYPE;
    }
    if (!valid_address(record->identity_address)) {
        return PAIRING_STORE_BAD_ADDRESS;
    }

    for (index = 0u; index < PAIRING_STORE_RECORD_SIZE; ++index) {
        out_bytes[index] = 0u;
    }
    out_bytes[OFFSET_MAGIC + 0] = (uint8_t)'X';
    out_bytes[OFFSET_MAGIC + 1] = (uint8_t)'H';
    out_bytes[OFFSET_MAGIC + 2] = (uint8_t)'P';
    out_bytes[OFFSET_MAGIC + 3] = (uint8_t)'1';
    write_le16(&out_bytes[OFFSET_VERSION], PAIRING_STORE_SCHEMA_VERSION);
    write_le16(&out_bytes[OFFSET_LENGTH], PAIRING_STORE_RECORD_SIZE);
    out_bytes[OFFSET_ROLE] = (uint8_t)record->role;
    out_bytes[OFFSET_ADDRESS_TYPE] = (uint8_t)record->address_type;
    for (index = 0u; index < 6u; ++index) {
        out_bytes[OFFSET_ADDRESS + index] = record->identity_address[index];
    }
    crc = pairing_store_crc32(out_bytes, CRC_INPUT_LENGTH);
    write_le32(&out_bytes[OFFSET_CRC], crc);
    return PAIRING_STORE_OK;
}

pairing_store_result_t pairing_store_decode(
    const uint8_t *bytes,
    size_t length,
    pairing_role_t expected_role,
    pairing_role_record_t *out_record)
{
    pairing_role_t role;
    pairing_address_type_t address_type;
    uint8_t address[6];
    uint32_t stored_crc;
    uint32_t computed_crc;
    size_t index;

    if (bytes == NULL || out_record == NULL || !valid_role(expected_role)) {
        return PAIRING_STORE_BAD_ARGUMENT;
    }
    if (length != PAIRING_STORE_RECORD_SIZE) {
        return PAIRING_STORE_BAD_LENGTH;
    }
    if (bytes[OFFSET_MAGIC + 0] != (uint8_t)'X' ||
        bytes[OFFSET_MAGIC + 1] != (uint8_t)'H' ||
        bytes[OFFSET_MAGIC + 2] != (uint8_t)'P' ||
        bytes[OFFSET_MAGIC + 3] != (uint8_t)'1') {
        return PAIRING_STORE_BAD_MAGIC;
    }
    if (read_le16(&bytes[OFFSET_VERSION]) != PAIRING_STORE_SCHEMA_VERSION) {
        return PAIRING_STORE_BAD_VERSION;
    }
    if (read_le16(&bytes[OFFSET_LENGTH]) != PAIRING_STORE_RECORD_SIZE) {
        return PAIRING_STORE_BAD_EMBEDDED_LENGTH;
    }

    role = (pairing_role_t)bytes[OFFSET_ROLE];
    if (!valid_role(role)) {
        return PAIRING_STORE_BAD_ROLE;
    }
    if (role != expected_role) {
        return PAIRING_STORE_ROLE_MISMATCH;
    }

    address_type = (pairing_address_type_t)bytes[OFFSET_ADDRESS_TYPE];
    if (!valid_address_type(address_type)) {
        return PAIRING_STORE_BAD_ADDRESS_TYPE;
    }
    if (bytes[OFFSET_RESERVED_0] != 0u ||
        bytes[OFFSET_RESERVED_0 + 1] != 0u ||
        bytes[OFFSET_RESERVED_1] != 0u ||
        bytes[OFFSET_RESERVED_1 + 1] != 0u) {
        return PAIRING_STORE_BAD_RESERVED;
    }

    for (index = 0u; index < 6u; ++index) {
        address[index] = bytes[OFFSET_ADDRESS + index];
    }
    if (!valid_address(address)) {
        return PAIRING_STORE_BAD_ADDRESS;
    }

    stored_crc = read_le32(&bytes[OFFSET_CRC]);
    computed_crc = pairing_store_crc32(bytes, CRC_INPUT_LENGTH);
    if (stored_crc != computed_crc) {
        return PAIRING_STORE_BAD_CRC;
    }

    out_record->role = role;
    out_record->address_type = address_type;
    for (index = 0u; index < 6u; ++index) {
        out_record->identity_address[index] = address[index];
    }
    return PAIRING_STORE_OK;
}
