#ifndef XBOX_PICO_PAIRING_STORE_H
#define XBOX_PICO_PAIRING_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAIRING_STORE_RECORD_SIZE 24u
#define PAIRING_STORE_SCHEMA_VERSION 1u

typedef enum {
    PAIRING_ROLE_KEYBOARD = 1,
    PAIRING_ROLE_MOUSE = 2
} pairing_role_t;

/* Values match BTstack's public/random LE identity address representation. */
typedef enum {
    PAIRING_ADDRESS_PUBLIC_IDENTITY = 0,
    PAIRING_ADDRESS_RANDOM_IDENTITY = 1
} pairing_address_type_t;

typedef struct {
    pairing_role_t role;
    pairing_address_type_t address_type;
    uint8_t identity_address[6];
} pairing_role_record_t;

typedef enum {
    PAIRING_STORE_OK = 0,
    PAIRING_STORE_BAD_ARGUMENT,
    PAIRING_STORE_BAD_LENGTH,
    PAIRING_STORE_BAD_MAGIC,
    PAIRING_STORE_BAD_VERSION,
    PAIRING_STORE_BAD_EMBEDDED_LENGTH,
    PAIRING_STORE_BAD_ROLE,
    PAIRING_STORE_ROLE_MISMATCH,
    PAIRING_STORE_BAD_ADDRESS_TYPE,
    PAIRING_STORE_BAD_RESERVED,
    PAIRING_STORE_BAD_ADDRESS,
    PAIRING_STORE_BAD_CRC
} pairing_store_result_t;

uint32_t pairing_store_crc32(const uint8_t *data, size_t length);

pairing_store_result_t pairing_store_encode(
    const pairing_role_record_t *record,
    uint8_t out_bytes[PAIRING_STORE_RECORD_SIZE]);

pairing_store_result_t pairing_store_decode(
    const uint8_t *bytes,
    size_t length,
    pairing_role_t expected_role,
    pairing_role_record_t *out_record);

#ifdef __cplusplus
}
#endif

#endif
