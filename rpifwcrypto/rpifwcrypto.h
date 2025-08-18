#ifndef RPI_FW_CRYPTO_H
#define RPI_FW_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

#define ARM_CRYPTO_KEY_STATUS_TYPE_DEVICE_PRIVATE_KEY (1 << 0)
#define ARM_CRYPTO_KEY_STATUS_LOCKED                  (1 << 8)

#define RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE 2048

/* Crypto-related mailbox tags */
typedef enum {
    TAG_GET_CRYPTO_LAST_ERROR      = 0x0003008e,    // Get last error code
    TAG_GET_CRYPTO_NUM_OTP_KEYS    = 0x0003008f,    // Get number of available OTP keys
    TAG_GET_CRYPTO_KEY_STATUS      = 0x00030090,    // Get key status
    TAG_SET_CRYPTO_KEY_STATUS      = 0x00038090,    // Set key status
    TAG_GET_CRYPTO_ECDSA_SIGN      = 0x00030091,    // Sign data using ECDSA
    TAG_GET_CRYPTO_HMAC_SHA256     = 0x00030092,    // Compute HMAC-SHA256
} RPI_FW_CRYPTO_TAG;

/* Error codes */
typedef enum {
    RPI_FW_CRYPTO_SUCCESS = 0,
    RPI_FW_CRYPTO_ERROR_UNKNOWN = 1,       // Unknown error
    RPI_FW_CRYPTO_EINVAL = 2,              // Invalid argument errors e.g. zero length etc
    RPI_FW_CRYPTO_KEY_NOT_FOUND = 3,       // No key for the given key_id
    RPI_FW_CRYPTO_KEY_LOCKED = 4,          // Requested operation for that key is locked
    RPI_FW_CRYPTO_KEY_OTP_ERROR = 5,       // OTP read error
    RPI_FW_CRYPTO_KEY_NOT_SET = 6,         // Key is all zeros
    RPI_FW_CRYPTO_KEY_INVALID = 7,         // Invalid key type/format
    RPI_FW_CRYPTO_NOT_SUPPORTED = 8,       // Requested operation is not supported
    RPI_FW_CRYPTO_OPERATION_FAILED = 9     // Crypto algorithm error
} RPI_FW_CRYPTO_STATUS;

/**
 * Get the number of OTP keys available in firmware
 *
 * @return The number of available OTP keys, or negative value on error
 */
int rpi_fw_crypto_get_num_otp_keys(void);

/**
 * Get the status of a specific OTP key
 *
 * @param key_id The ID of the key to query
 * @param status Pointer to store the key status
 * @return 0 on success, negative error code on failure
 */
int rpi_fw_crypto_get_key_status(uint32_t key_id, uint32_t *status);

/**
 * Set the status of a specific OTP key
 *
 * @param key_id The ID of the key to set
 * @param status The new key status value
 * @return 0 on success, negative error code on failure
 */
int rpi_fw_crypto_set_key_status(uint32_t key_id, uint32_t status);

/**
 * Get the last error code from the firmware crypto subsystem
 *
 * @return The last error code (see RPI_FW_CRYPTO_STATUS)
 */
int rpi_fw_crypto_get_last_error(void);

/**
 * Translate a firmware crypto error status to a human-readable string
 *
 * @param status The error code (see RPI_FW_CRYPTO_STATUS)
 * @return A constant string describing the error
 */
const char *rpi_fw_crypto_strerror(int status);

/**
 * Request an ECDSA signature from the firmware
 *
 * @param flags    Flags for the signing operation
 * @param key_id   The ID of the key to use for signing
 * @param hash     Pointer to the hash to sign (must be 32 bytes for SHA256)
 * @param hash_len Length of the hash (should be 32)
 * @param sig      Output buffer for the signature
 * @param sig_max_len Size of the output buffer
 * @param sig_len  Pointer to store the actual signature length
 * @return 0 on success, negative error code on failure
 */
int rpi_fw_crypto_ecdsa_sign(uint32_t flags, uint32_t key_id, const uint8_t *hash, size_t hash_len,
                             uint8_t *sig, size_t sig_max_len, size_t *sig_len);

/**
 * Calculate HMAC-SHA256 using a key in OTP
 *
 * @param key_id The ID of the key to use
 * @param flags Operation flags (currently unused, set to 0)
 * @param message Pointer to the message to HMAC
 * @param message_len Length of the message
 * @param hmac Output buffer for the HMAC (must be 32 bytes)
 * @return 0 on success, negative error code on failure
 */
int rpi_fw_crypto_hmac_sha256(uint32_t key_id, uint32_t flags, const uint8_t *message, size_t message_len,
                             uint8_t *hmac);

const char *rpi_fw_crypto_key_status_str(uint32_t key_status);

#ifdef __cplusplus
}
#endif

#endif /* RPI_FW_CRYPTO_H */
