#include "pairing_store.h"

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

static void write_crc(uint8_t bytes[PAIRING_STORE_RECORD_SIZE])
{
    const uint32_t crc = pairing_store_crc32(bytes, 20u);
    bytes[20] = (uint8_t)(crc & UINT32_C(0xff));
    bytes[21] = (uint8_t)((crc >> 8) & UINT32_C(0xff));
    bytes[22] = (uint8_t)((crc >> 16) & UINT32_C(0xff));
    bytes[23] = (uint8_t)(crc >> 24);
}

static pairing_role_record_t keyboard_record(void)
{
    pairing_role_record_t record;
    size_t index;

    record.role = PAIRING_ROLE_KEYBOARD;
    record.address_type = PAIRING_ADDRESS_RANDOM_IDENTITY;
    for (index = 0u; index < ARRAY_LENGTH(record.identity_address); ++index) {
        record.identity_address[index] = (uint8_t)(index + 1u);
    }
    return record;
}

static int test_crc_reference(void)
{
    static const uint8_t reference[] = {
        (uint8_t)'1', (uint8_t)'2', (uint8_t)'3',
        (uint8_t)'4', (uint8_t)'5', (uint8_t)'6',
        (uint8_t)'7', (uint8_t)'8', (uint8_t)'9'
    };

    CHECK(pairing_store_crc32(reference, ARRAY_LENGTH(reference)) ==
          UINT32_C(0xcbf43926));
    CHECK(pairing_store_crc32(NULL, 0u) == 0u);
    return 0;
}

static int test_round_trip_and_layout(void)
{
    pairing_role_record_t source = keyboard_record();
    pairing_role_record_t decoded;
    uint8_t bytes[PAIRING_STORE_RECORD_SIZE];
    size_t index;

    CHECK(pairing_store_encode(&source, bytes) == PAIRING_STORE_OK);
    CHECK(bytes[0] == (uint8_t)'X');
    CHECK(bytes[1] == (uint8_t)'H');
    CHECK(bytes[2] == (uint8_t)'P');
    CHECK(bytes[3] == (uint8_t)'1');
    CHECK(bytes[4] == 0x01u && bytes[5] == 0x00u);
    CHECK(bytes[6] == 0x18u && bytes[7] == 0x00u);
    CHECK(bytes[8] == 0x01u);
    CHECK(bytes[9] == 0x01u);
    CHECK(bytes[10] == 0u && bytes[11] == 0u);
    CHECK(bytes[18] == 0u && bytes[19] == 0u);
    for (index = 0u; index < ARRAY_LENGTH(source.identity_address); ++index) {
        CHECK(bytes[12u + index] == source.identity_address[index]);
    }
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_OK);
    CHECK(decoded.role == source.role);
    CHECK(decoded.address_type == source.address_type);
    CHECK(memcmp(decoded.identity_address, source.identity_address,
                 sizeof(decoded.identity_address)) == 0);
    return 0;
}

static int test_every_bit_is_crc_protected(void)
{
    pairing_role_record_t source = keyboard_record();
    pairing_role_record_t decoded;
    uint8_t original[PAIRING_STORE_RECORD_SIZE];
    uint8_t mutated[PAIRING_STORE_RECORD_SIZE];
    size_t bit;

    CHECK(pairing_store_encode(&source, original) == PAIRING_STORE_OK);
    for (bit = 0u; bit < PAIRING_STORE_RECORD_SIZE * 8u; ++bit) {
        memcpy(mutated, original, sizeof(mutated));
        mutated[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        CHECK(pairing_store_decode(mutated, ARRAY_LENGTH(mutated),
                                   PAIRING_ROLE_KEYBOARD, &decoded) !=
              PAIRING_STORE_OK);
    }
    return 0;
}

static int test_strict_field_validation(void)
{
    pairing_role_record_t source = keyboard_record();
    pairing_role_record_t decoded;
    uint8_t valid[PAIRING_STORE_RECORD_SIZE];
    uint8_t bytes[PAIRING_STORE_RECORD_SIZE];
    size_t index;

    CHECK(pairing_store_encode(&source, valid) == PAIRING_STORE_OK);
    CHECK(pairing_store_decode(valid, ARRAY_LENGTH(valid) - 1u,
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_LENGTH);
    CHECK(pairing_store_decode(valid, ARRAY_LENGTH(valid) + 1u,
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_LENGTH);

    memcpy(bytes, valid, sizeof(bytes));
    bytes[0] = (uint8_t)'Y';
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_MAGIC);

    memcpy(bytes, valid, sizeof(bytes));
    bytes[4] = 2u;
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_VERSION);

    memcpy(bytes, valid, sizeof(bytes));
    bytes[6] = 23u;
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_EMBEDDED_LENGTH);

    memcpy(bytes, valid, sizeof(bytes));
    bytes[8] = 3u;
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_ROLE);
    CHECK(pairing_store_decode(valid, ARRAY_LENGTH(valid),
                               PAIRING_ROLE_MOUSE, &decoded) ==
          PAIRING_STORE_ROLE_MISMATCH);

    memcpy(bytes, valid, sizeof(bytes));
    bytes[9] = 2u;
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_ADDRESS_TYPE);

    for (index = 10u; index <= 11u; ++index) {
        memcpy(bytes, valid, sizeof(bytes));
        bytes[index] = 1u;
        write_crc(bytes);
        CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                                   PAIRING_ROLE_KEYBOARD, &decoded) ==
              PAIRING_STORE_BAD_RESERVED);
    }
    for (index = 18u; index <= 19u; ++index) {
        memcpy(bytes, valid, sizeof(bytes));
        bytes[index] = 1u;
        write_crc(bytes);
        CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                                   PAIRING_ROLE_KEYBOARD, &decoded) ==
              PAIRING_STORE_BAD_RESERVED);
    }

    memcpy(bytes, valid, sizeof(bytes));
    memset(&bytes[12], 0, 6u);
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_ADDRESS);
    memcpy(bytes, valid, sizeof(bytes));
    memset(&bytes[12], 0xff, 6u);
    write_crc(bytes);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_ADDRESS);

    memcpy(bytes, valid, sizeof(bytes));
    bytes[20] ^= 0x01u;
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_CRC);
    return 0;
}

static int test_encode_and_argument_validation(void)
{
    pairing_role_record_t record = keyboard_record();
    pairing_role_record_t decoded;
    uint8_t bytes[PAIRING_STORE_RECORD_SIZE];

    CHECK(pairing_store_encode(NULL, bytes) == PAIRING_STORE_BAD_ARGUMENT);
    CHECK(pairing_store_encode(&record, NULL) == PAIRING_STORE_BAD_ARGUMENT);
    record.role = (pairing_role_t)0;
    CHECK(pairing_store_encode(&record, bytes) == PAIRING_STORE_BAD_ROLE);
    record = keyboard_record();
    record.address_type = (pairing_address_type_t)2;
    CHECK(pairing_store_encode(&record, bytes) ==
          PAIRING_STORE_BAD_ADDRESS_TYPE);
    record = keyboard_record();
    memset(record.identity_address, 0, sizeof(record.identity_address));
    CHECK(pairing_store_encode(&record, bytes) == PAIRING_STORE_BAD_ADDRESS);
    memset(record.identity_address, 0xff, sizeof(record.identity_address));
    CHECK(pairing_store_encode(&record, bytes) == PAIRING_STORE_BAD_ADDRESS);

    record = keyboard_record();
    CHECK(pairing_store_encode(&record, bytes) == PAIRING_STORE_OK);
    CHECK(pairing_store_decode(NULL, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, &decoded) ==
          PAIRING_STORE_BAD_ARGUMENT);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               PAIRING_ROLE_KEYBOARD, NULL) ==
          PAIRING_STORE_BAD_ARGUMENT);
    CHECK(pairing_store_decode(bytes, ARRAY_LENGTH(bytes),
                               (pairing_role_t)0, &decoded) ==
          PAIRING_STORE_BAD_ARGUMENT);
    return 0;
}

int test_pairing_store(void)
{
    CHECK(test_crc_reference() == 0);
    CHECK(test_round_trip_and_layout() == 0);
    CHECK(test_every_bit_is_crc_protected() == 0);
    CHECK(test_strict_field_validation() == 0);
    CHECK(test_encode_and_argument_validation() == 0);
    return 0;
}
