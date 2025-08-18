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
};


// HMAC message
#define RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE 2048
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
};

// ECDSA sign message
#define ECDSA_RESP_MAX_SIZE 128
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
            uint8_t sig[ECDSA_RESP_MAX_SIZE];
        } resp;
    };
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

static int mbox_property(int file_desc, struct firmware_msg *msg)
{
    int rc = ioctl(file_desc, IOCTL_MBOX_PROPERTY, msg);
    if (rc < 0)
        fprintf(stderr, "ioctl_mbox_property failed: %d\n", rc);

    LOG_DEBUG("msg.hdr.code: %d\n", msg->hdr.code);
    LOG_DEBUG("msg.hdr.tag: %d\n", msg->hdr.tag);
    LOG_DEBUG("msg.hdr.tag_buf_size: %d\n", msg->hdr.tag_buf_size);
    LOG_DEBUG("msg.hdr.tag_req_resp_size: %d\n", msg->hdr.tag_req_resp_size);

    if (!(msg->hdr.code & VC_MAILBOX_ERROR) ||
        !(msg->hdr.tag_req_resp_size & VC_MAILBOX_ERROR))
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

int rpi_fw_crypto_get_last_error(void)
{
    int mb;
    int rc;
    struct firmware_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return -RPI_FW_CRYPTO_ERROR_UNKNOWN;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_CRYPTO_LAST_ERROR;
    msg.hdr.tag_buf_size = 4;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    return (rc < 0) ? rc : (int)msg.value[0];
}

const char *rpi_fw_crypto_strerror(int status)
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

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);
    if (rc < 0)
        return rc;

    if (msg.resp.length > sig_max_len) {
        fprintf(stderr, "msg.length %d > sig_max_len %ld\n", msg.resp.length, sig_max_len);
        return -RPI_FW_CRYPTO_EINVAL;
    }

    memcpy(sig, msg.resp.sig, msg.resp.length);
    *sig_len = msg.resp.length;
    return RPI_FW_CRYPTO_SUCCESS;
}

int rpi_fw_crypto_hmac_sha256(uint32_t key_id, uint32_t flags, const uint8_t *message, size_t message_len, uint8_t *hmac)
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
    msg.hdr.tag_buf_size = 4 + 4 + 4 + message_len; // flags + key_id + length + message
    msg.hmac.flags = flags;
    msg.hmac.key_id = key_id;
    msg.hmac.length = message_len;
    memcpy(msg.hmac.message, message, message_len);

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

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    return (rc < 0) ? rc : RPI_FW_CRYPTO_SUCCESS;
}
