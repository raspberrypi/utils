#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "rpifwcrypto.h"
#include <errno.h>

#define DEVICE_FILE_NAME "/dev/vcio"
#define MAJOR_NUM 100
#define IOCTL_MBOX_PROPERTY _IOWR(MAJOR_NUM, 0, char *)

#if 0
#define LOG_DEBUG(...) do { \
    fprintf(stderr, __VA_ARGS__); \
} while (0)
#else
#define LOG_DEBUG(...) do { } while (0)
#endif

/* Mailbox channel for property interface */
#define MBOX_CHAN_PROPERTY 8

/* VideoCore mailbox error flag */
#define VC_MAILBOX_ERROR 0x80000000

/* Crypto-related mailbox tags */
typedef enum {
    TAG_GET_CRYPTO_LAST_ERROR      = 0x0003008e,    // Get last error code
    TAG_GET_CRYPTO_NUM_OTP_KEYS    = 0x0003008f,    // Get number of available OTP keys
    TAG_GET_CRYPTO_KEY_STATUS      = 0x00030090,    // Get key status
    TAG_SET_CRYPTO_KEY_STATUS      = 0x00038090,    // Set key status
    TAG_GET_CRYPTO_ECDSA_SIGN      = 0x00030091,    // Sign data using ECDSA
    TAG_GET_CRYPTO_HMAC_SHA256     = 0x00030092,    // Compute HMAC-SHA256
    TAG_GET_CRYPTO_PUBLIC_KEY      = 0x00030093,    // Get public key
    TAG_GET_CRYPTO_PRIVATE_KEY     = 0x00030094,    // Get private key
    TAG_GET_CRYPTO_GEN_ECDSA_KEY   = 0x00030095,    // Generate ECDSA key
} RPI_FW_CRYPTO_TAG;

/* Common header structure for firmware mailbox messages */
struct firmware_msg_header {
    uint32_t buf_size;
    uint32_t code;
    uint32_t tag;
    uint32_t tag_buf_size;
    uint32_t tag_req_resp_size;
};

/* Standard message structure for firmware mailbox */
struct firmware_msg {
    struct firmware_msg_header hdr;
    uint32_t value[4];  /* Value buffer for request/response */
    uint32_t end_tag;
};

struct firmware_hmac_msg {
    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t flags;
            uint32_t key_id;
            uint32_t length;
            uint8_t message[RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE];
        } hmac;
        struct {
            uint32_t status;
            uint32_t length;
            uint8_t hmac[32];  // HMAC-SHA256 is always 32 bytes
        } resp;
    };
    uint32_t end_tag;
};

struct firmware_ecdsa_sign_msg {
    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t flags;
            uint32_t key_id;
            uint32_t length;
            uint8_t hash[32];
        } sign;
        struct {
            uint32_t status;
            uint32_t length;
            uint8_t sig[RPI_FW_CRYPTO_ECDSA_RESP_MAX_SIZE];
        } resp;
    };
    uint32_t end_tag;
};

struct firmware_pubkey_msg {
    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t flags;
            uint32_t key_id;
        } pubkey;
        struct {
            uint32_t status;
            uint32_t length;
            uint8_t pubkey[RPI_FW_CRYPTO_PUBLIC_KEY_MAX_SIZE];
        } resp;
    };
    uint32_t end_tag;
};

struct firmware_private_key_msg {
    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t flags;
            uint32_t key_id;
        } private_key;
        struct {
            uint32_t status;
            uint32_t length;
            uint8_t private_key[RPI_FW_CRYPTO_PRIVATE_KEY_MAX_SIZE];
        } resp;
    };
    uint32_t end_tag;
};

struct firmware_gen_ecdsa_key_msg {

    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t flags;
            uint32_t key_id;
        } gen_ecdsa_key;
    };
    uint32_t end_tag;
};

static int mbox_open(void)
{
    int file_desc = open(DEVICE_FILE_NAME, 0);
    if (file_desc < 0)
        fprintf(stderr, "Failed to open %s: %s\n", DEVICE_FILE_NAME, strerror(errno));
    return file_desc;
}

static void mbox_close(int file_desc)
{
    close(file_desc);
}

static int mbox_property(int file_desc, void *msg)
{
    struct firmware_msg_header *hdr = (struct firmware_msg_header *)msg;
    int rc = ioctl(file_desc, IOCTL_MBOX_PROPERTY, msg);
    if (rc < 0)
        fprintf(stderr, "ioctl_mbox_property failed: %d\n", rc);

    LOG_DEBUG("msg.hdr.code: %08x\n", hdr->code);
    LOG_DEBUG("msg.hdr.buf_size: %d\n", hdr->buf_size);
    LOG_DEBUG("msg.hdr.tag: %d\n", hdr->tag);
    LOG_DEBUG("msg.hdr.tag_buf_size: %d\n", hdr->tag_buf_size);
    LOG_DEBUG("msg.hdr.tag_req_resp_size: %d\n", hdr->tag_req_resp_size);

    if (!(hdr->code & VC_MAILBOX_ERROR) ||
        !(hdr->tag_req_resp_size & VC_MAILBOX_ERROR))
        return -1;

    return 0;
}

int rpi_fw_crypto_get_num_otp_keys(void)
{
    int mb;
    int rc;
    struct firmware_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_NUM_OTP_KEYS;
    msg.hdr.tag_buf_size = 4;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    return (rc < 0) ? rc : (int)msg.value[0];
}

int rpi_fw_crypto_get_key_status(uint32_t key_id, uint32_t *status)
{
    int mb;
    int rc;
    struct firmware_msg msg = {0};

    if (!status)
        return -RPI_FW_CRYPTO_EINVAL;

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_KEY_STATUS;
    msg.hdr.tag_buf_size = 4;
    msg.value[0] = key_id;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    if (rc < 0)
        return rc;

    if (msg.value[0] & VC_MAILBOX_ERROR)
        return -RPI_FW_CRYPTO_KEY_NOT_FOUND;

    *status = msg.value[0];
    return RPI_FW_CRYPTO_SUCCESS;
}

RPI_FW_CRYPTO_STATUS rpi_fw_crypto_get_last_error(void)
{
    int mb;
    RPI_FW_CRYPTO_STATUS rc;
    struct firmware_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_LAST_ERROR;
    msg.hdr.tag_buf_size = 4;
    msg.end_tag = 0;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    return (rc < 0) ? RPI_FW_CRYPTO_ERROR_UNKNOWN : (RPI_FW_CRYPTO_STATUS)msg.value[0];
}

const char *rpi_fw_crypto_strerror(RPI_FW_CRYPTO_STATUS status)
{
    switch (status) {
    case RPI_FW_CRYPTO_SUCCESS:
        return "Success";
    case RPI_FW_CRYPTO_ERROR_UNKNOWN:
        return "Unknown error";
    case RPI_FW_CRYPTO_EINVAL:
        return "Invalid argument";
    case RPI_FW_CRYPTO_KEY_NOT_FOUND:
        return "Key not found";
    case RPI_FW_CRYPTO_KEY_LOCKED:
        return "Key is locked";
    case RPI_FW_CRYPTO_KEY_OTP_ERROR:
        return "OTP read error";
    case RPI_FW_CRYPTO_KEY_NOT_SET:
        return "Key not set (all zeros)";
    case RPI_FW_CRYPTO_KEY_INVALID:
        return "Invalid key type/format";
    case RPI_FW_CRYPTO_NOT_SUPPORTED:
        return "Operation not supported";
    case RPI_FW_CRYPTO_OPERATION_FAILED:
        return "Crypto algorithm error";
    case RPI_FW_CRYPTO_KEY_NOT_BLANK:
        return "Key slot is not blank";
    default:
        return "Unrecognized error code";
    }
}

// Converts a key_status value to a human-readable string, e.g. "CUSTOMER LOCKED"
const char *rpi_fw_crypto_key_status_str(uint32_t key_status)
{
    static char buf[64];
    buf[0] = '\0';
    uint32_t known = 0;
    if (key_status & ARM_CRYPTO_KEY_STATUS_TYPE_DEVICE_PRIVATE_KEY) {
        strcat(buf, "DEVICE");
        known |= ARM_CRYPTO_KEY_STATUS_TYPE_DEVICE_PRIVATE_KEY;
    }
    if (key_status & ARM_CRYPTO_KEY_STATUS_LOCKED) {
        if (buf[0])
            strcat(buf, " ");
        strcat(buf, "LOCKED");
        known |= ARM_CRYPTO_KEY_STATUS_LOCKED;
    }
    if (key_status & ~known) {
        if (buf[0])
            strcat(buf, " ");
        strcat(buf, "UNKNOWN");
    }
    return buf;
}

// Implementation of ECDSA sign via firmware mailbox
int rpi_fw_crypto_ecdsa_sign(uint32_t flags, uint32_t key_id, const uint8_t *hash, size_t hash_len,
                             uint8_t *sig, size_t sig_max_len, size_t *sig_len)
{
    int mb;
    int rc;
    struct firmware_ecdsa_sign_msg msg = {0};

    if (!hash || !sig || !sig_len || hash_len != 32 || sig_max_len < 32)
        return -RPI_FW_CRYPTO_EINVAL;

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_ECDSA_SIGN;
    msg.hdr.tag_buf_size = 128;
    msg.sign.flags = flags;
    msg.sign.key_id = key_id;
    msg.sign.length = hash_len;
    memcpy(msg.sign.hash, hash, hash_len);
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);
    if (rc < 0)
        return rc;

    if (msg.resp.length > sig_max_len) {
        fprintf(stderr, "msg.length %d > sig_max_len %zd\n", msg.resp.length, sig_max_len);
        return -RPI_FW_CRYPTO_EINVAL;
    }

    memcpy(sig, msg.resp.sig, msg.resp.length);
    *sig_len = msg.resp.length;
    return RPI_FW_CRYPTO_SUCCESS;
}

int rpi_fw_crypto_hmac_sha256(uint32_t flags, uint32_t key_id, const uint8_t *message, size_t message_len, uint8_t *hmac)
{
    int mb;
    int rc;
    struct firmware_hmac_msg msg = {0};

    if (!message || !hmac || message_len > RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE)
        return -RPI_FW_CRYPTO_EINVAL;

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_HMAC_SHA256;
    msg.hdr.tag_buf_size = sizeof(msg.hmac); // Pass the largest possible message + response
    msg.hmac.flags = flags;
    msg.hmac.key_id = key_id;
    msg.hmac.length = message_len;
    memcpy(msg.hmac.message, message, message_len);
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0)
        return rc;

    if (msg.resp.status & VC_MAILBOX_ERROR)
        return -RPI_FW_CRYPTO_OPERATION_FAILED;

    memcpy(hmac, msg.resp.hmac, sizeof(msg.resp.hmac));
    return RPI_FW_CRYPTO_SUCCESS;
}

int rpi_fw_crypto_set_key_status(uint32_t key_id, uint32_t status)
{
    int mb;
    int rc;
    struct firmware_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_SET_CRYPTO_KEY_STATUS;
    msg.hdr.tag_buf_size = 8;
    msg.value[0] = key_id;
    msg.value[1] = status;
    msg.end_tag = 0;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    return (rc < 0) ? rc : RPI_FW_CRYPTO_SUCCESS;
}

int rpi_fw_crypto_get_pubkey(uint32_t flags, uint32_t key_id, uint8_t *pubkey, size_t pubkey_max_len, size_t *pubkey_len)
{
    int mb;
    int rc;
    struct firmware_pubkey_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_PUBLIC_KEY;
    msg.hdr.tag_buf_size = 4 + 4 + RPI_FW_CRYPTO_PUBLIC_KEY_MAX_SIZE; // flags + key_id + pubkey
    msg.pubkey.flags = flags;
    msg.pubkey.key_id = key_id;
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0)
        return rc;

    if (msg.resp.status & VC_MAILBOX_ERROR)
        return -RPI_FW_CRYPTO_OPERATION_FAILED;

    if (msg.resp.length > pubkey_max_len) {
        fprintf(stderr, "msg.length %d > pubkey_max_len %zd\n", msg.resp.length, pubkey_max_len);
        return -RPI_FW_CRYPTO_EINVAL;
    }

    memcpy(pubkey, msg.resp.pubkey, msg.resp.length);
    *pubkey_len = msg.resp.length;
    return RPI_FW_CRYPTO_SUCCESS;
}

int rpi_fw_crypto_get_private_key(uint32_t flags, uint32_t key_id, uint8_t *private_key, size_t private_key_max_len, size_t *private_key_len)
{
    int mb;
    int rc;
    struct firmware_private_key_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_PRIVATE_KEY;
    msg.hdr.tag_buf_size = 4 + 4 + RPI_FW_CRYPTO_PRIVATE_KEY_MAX_SIZE; // flags + key_id + private_key
    msg.private_key.flags = flags;
    msg.private_key.key_id = key_id;
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0)
        return rc;

    if (msg.resp.status & VC_MAILBOX_ERROR)
        return -RPI_FW_CRYPTO_OPERATION_FAILED;

    if (msg.resp.length > private_key_max_len) {
        fprintf(stderr, "msg.length %d > private_key_max_len %zd\n", msg.resp.length, private_key_max_len);
        return -RPI_FW_CRYPTO_EINVAL;
    }

    memcpy(private_key, msg.resp.private_key, msg.resp.length);
    *private_key_len = msg.resp.length;
    return RPI_FW_CRYPTO_SUCCESS;
}

int rpi_fw_crypto_gen_ecdsa_key(uint32_t flags, uint32_t key_id)
{
    int mb;
    int rc;
    struct firmware_gen_ecdsa_key_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_GEN_ECDSA_KEY;
    msg.hdr.tag_buf_size = 4 + 4; // flags + key_id
    msg.gen_ecdsa_key.flags = flags;
    msg.gen_ecdsa_key.key_id = key_id;
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    return (rc < 0) ? rc : RPI_FW_CRYPTO_SUCCESS;
}