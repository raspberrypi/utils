#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "rpierrorlog.h"

#define DEVICE_FILE_NAME    "/dev/vcio"
#define MAJOR_NUM           100
#define IOCTL_MBOX_PROPERTY _IOWR(MAJOR_NUM, 0, char *)

#if 0
#define LOG_DEBUG(...) do { \
    fprintf(stderr, __VA_ARGS__); \
} while (0)
#else
#define LOG_DEBUG(...) do { } while (0)
#endif

/* VideoCore mailbox error flag */
#define VC_MAILBOX_ERROR 0x80000000u

/* Error log mailbox tags */
#define TAG_GET_EEPROM_ERROR_LOGS  0x0003009au  /* Read error log entries from EEPROM */
#define TAG_SET_EEPROM_ERROR_LOGS  0x0003809au  /* Clear error log in EEPROM */

/* Common header for all firmware mailbox messages */
struct firmware_msg_header {
    uint32_t buf_size;
    uint32_t code;
    uint32_t tag;
    uint32_t tag_buf_size;
    uint32_t tag_req_resp_size;
};

/* Mailbox message for TAG_GET_EEPROM_ERROR_LOGS.
 *
 * Request:  reserved (ignored, 0) + length (max bytes to receive)
 * Response: status (VC_MAILBOX_ERROR on failure) + length (bytes written) + data
 */
struct firmware_error_log_msg {
    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t reserved;
            uint32_t length;
        } req;
        struct {
            uint32_t status;
            uint32_t length;
            uint8_t  data[RPI_ERROR_LOG_MAX_SIZE];
        } resp;
    };
    uint32_t end_tag;
};

/* Mailbox message for TAG_SET_EEPROM_ERROR_LOGS.
 *
 * Request:  reserved[2] (ignored)
 * Response: status (VC_MAILBOX_ERROR on failure) + reserved
 */
struct firmware_error_log_clear_msg {
    struct firmware_msg_header hdr;
    union {
        struct {
            uint32_t reserved[2];
        } req;
        struct {
            uint32_t status;
            uint32_t reserved;
        } resp;
    };
    uint32_t end_tag;
};

static int mbox_open(void)
{
    int fd = open(DEVICE_FILE_NAME, O_RDWR);
    if (fd < 0)
        fprintf(stderr, "Failed to open %s: %s\n", DEVICE_FILE_NAME, strerror(errno));
    return fd;
}

static void mbox_close(int fd)
{
    close(fd);
}

static int mbox_property(int fd, void *msg)
{
    struct firmware_msg_header *hdr = (struct firmware_msg_header *)msg;
    int rc = ioctl(fd, IOCTL_MBOX_PROPERTY, msg);
    if (rc < 0)
        fprintf(stderr, "ioctl_mbox_property failed: %d\n", rc);

    LOG_DEBUG("hdr.code=0x%08x tag=0x%08x tag_buf_size=%u tag_req_resp_size=0x%08x\n",
              hdr->code, hdr->tag, hdr->tag_buf_size, hdr->tag_req_resp_size);

    if (!(hdr->code & VC_MAILBOX_ERROR) ||
        !(hdr->tag_req_resp_size & VC_MAILBOX_ERROR))
        return -1;

    return 0;
}

const char *rpi_error_log_strerror(uint32_t error_code)
{
    switch (error_code) {
    case 0x03: return "Generic boot failure";
    case 0x04: return "Firmware (start*.elf) not found";
    case 0x07: return "Kernel or device-tree not found or is not compatible";
    case 0x08: return "SDRAM failure";
    case 0x09: return "SDRAM mismatch";
    case 0x0a: return "Halting";
    case 0x11: return "Operation requires USB high current limit";
    case 0x12: return "SD card overcurrent";
    case 0x21: return "Partition is not FAT";
    case 0x22: return "Failed to read from partition";
    case 0x23: return "Extended partition not FAT";
    case 0x24: return "File signature or hash mismatch";
    case 0x31: return "SPI EEPROM error";
    case 0x32: return "EEPROM is write protected";
    case 0x33: return "I2C error";
    case 0x34: return "Configuration error";
    case 0x43: return "RP1 not found";
    case 0x44: return "Unsupported board type";
    case 0x45: return "Fatal firmware error";
    case 0x46: return "Power failure type A";
    case 0x47: return "Power failure type B";
    default:   return "Unknown";
    }
}

int rpi_error_log_get(rpi_error_log_t *entries, size_t max_entries, size_t *out_count)
{
    struct firmware_error_log_msg msg = {0};
    size_t num_entries, i;
    int mb, rc;

    if (!entries || !out_count || max_entries == 0)
        return -1;

    mb = mbox_open();
    if (mb < 0)
        return -1;

    msg.hdr.buf_size      = sizeof(msg);
    msg.hdr.tag           = TAG_GET_EEPROM_ERROR_LOGS;
    msg.hdr.tag_buf_size  = sizeof(msg.resp);
    msg.req.reserved      = 0;
    msg.req.length        = RPI_ERROR_LOG_MAX_SIZE;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    if (rc < 0)
        return -1;

    if (msg.resp.status & VC_MAILBOX_ERROR) {
        /* Firmware error means no error log file exists in EEPROM */
        *out_count = 0;
        return 0;
    }

    num_entries = msg.resp.length / RPI_ERROR_LOG_ENTRY_SIZE;
    if (num_entries > max_entries)
        num_entries = max_entries;

    for (i = 0; i < num_entries; i++) {
        uint32_t ec, ec_inv;
        size_t offset = i * RPI_ERROR_LOG_ENTRY_SIZE;

        memcpy(&ec,     msg.resp.data + offset,     sizeof(ec));
        memcpy(&ec_inv, msg.resp.data + offset + 4, sizeof(ec_inv));

        LOG_DEBUG("entry[%zu] ec=0x%08x ec_inv=0x%08x\n", i, ec, ec_inv);

        if (ec == 0xffffffffu)
            break;

        if (ec_inv != (~ec & 0xffffffffu)) {
            fprintf(stderr, "Entry %zu checksum invalid\n", i);
            return -1;
        }

        entries[i].error_code = ec;
    }

    *out_count = i;
    return 0;
}

int rpi_error_log_clear(void)
{
    struct firmware_error_log_clear_msg msg = {0};
    int mb, rc;

    mb = mbox_open();
    if (mb < 0)
        return -1;

    msg.hdr.buf_size     = sizeof(msg);
    msg.hdr.tag          = TAG_SET_EEPROM_ERROR_LOGS;
    msg.hdr.tag_buf_size = sizeof(msg.resp);

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    if (rc < 0)
        return -1;

    if (msg.resp.status & VC_MAILBOX_ERROR) {
        fprintf(stderr, "Firmware returned error for SET_EEPROM_ERROR_LOGS\n");
        return -1;
    }

    return 0;
}
