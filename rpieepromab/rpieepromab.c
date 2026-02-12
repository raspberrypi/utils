#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "rpieepromab.h"
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

/* AB EEPROM related mailbox tags */
typedef enum {
    TAG_GET_EEPROM_PACKET          = 0x00030096,
    TAG_SET_EEPROM_PACKET          = 0x00038096,
    TAG_GET_EEPROM_UPDATE_STATUS   = 0x00030097,
    TAG_SET_EEPROM_UPDATE_STATUS   = 0x00038097,
    TAG_GET_EEPROM_PARTITION       = 0x00030098,
    TAG_SET_EEPROM_PARTITION       = 0x00038098,
    TAG_GET_EEPROM_AB_PARAMS       = 0x00030099,
    TAG_SET_EEPROM_AB_PARAMS       = 0x00038099,
} RPI_EEPROM_AB_UPDATE_TAG;

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

/* Message structure to transfer EEPROM data to firmware mailbox */
struct firmware_update_packet_msg {
    struct firmware_msg_header hdr;
    union {
        uint32_t address;
        RPI_EEPROM_AB_ERROR error;
    };
    uint32_t length;
    uint8_t data[RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE];
    uint32_t end_tag;
};

/* Message structure to get the status of the EEPROM update from firmware mailbox */
struct firmware_update_get_status_msg {
    struct firmware_msg_header hdr;
    union {
        RPI_EEPROM_AB_UPDATE_RC_STATUS status;
        RPI_EEPROM_AB_ERROR error;
    };
    RPI_EEPROM_AB_ERROR firmware_error;
    uint32_t spi_gpio_check;
    uint32_t using_partitioning;
    uint32_t end_tag;
};

/* Command codes */
typedef enum {
    RPI_EEPROM_AB_UPDATE_CANCEL            = 0,
    RPI_EEPROM_AB_UPDATE_START_WRITE       = 1,
} RPI_EEPROM_AB_UPDATE_COMMAND_CODE;

/* Message structure to send EEPROM update command to firmware mailbox */
struct firmware_update_command_msg {
    struct firmware_msg_header hdr;
    union {
        RPI_EEPROM_AB_UPDATE_COMMAND_CODE command;
        RPI_EEPROM_AB_ERROR error;
    };
    uint32_t end_tag;
};


struct firmware_update_get_eeprom_partition_msg {
    struct firmware_msg_header hdr;
    union {
        RPI_EEPROM_AB_PARTITION committed_partition;
        RPI_EEPROM_AB_ERROR error;
    };
    RPI_EEPROM_AB_PARTITION valid_partition;
    uint8_t committed_partition_hash[32];
    uint8_t valid_partition_hash[32];
    uint32_t end_tag;
};

struct firmware_update_set_eeprom_partition_msg {
    struct firmware_msg_header hdr;
    union {
        RPI_EEPROM_RELATIVE_PARTITION relative_partition;
        RPI_EEPROM_AB_ERROR error;
    };
    uint8_t hash[32];
    uint32_t end_tag;
};

/* Message structure to get all AB EEPROM parameters from firmware mailbox */
struct firmware_update_get_ab_params_msg {
    struct firmware_msg_header hdr;
    union {
        RPI_EEPROM_AB_PARTITION partition;
        RPI_EEPROM_AB_ERROR error;
    };
    uint32_t committed;
    uint32_t tryboot;
    RPI_EEPROM_AB_PARTITION partition_at_boot;
    uint32_t committed_at_boot;
    uint32_t end_tag;
};

/* Message structure to set individual AB EEPROM parameter to firmware mailbox */
struct firmware_update_set_ab_param_msg {
    struct firmware_msg_header hdr;
    union {
        enum {
            EEPROM_UPDATE_PARAM_COMMIT   = 1,
            EEPROM_UPDATE_PARAM_TRYBOOT,
        } param;
        RPI_EEPROM_AB_ERROR error;
    };
    union {
        RPI_EEPROM_RELATIVE_PARTITION relative_partition_to_commit;
        uint32_t tryboot_value;
    } value;
    uint32_t end_tag;
};


#define TAG_BUFFER_SIZE(S) (sizeof(S) - sizeof(struct firmware_msg_header) - sizeof(uint32_t))

static int mbox_open(void) {
    int file_desc = open(DEVICE_FILE_NAME, 0);
    if (file_desc < 0)
        fprintf(stderr, "Failed to open %s: %s\n", DEVICE_FILE_NAME, strerror(errno));
    return file_desc;
}

static void mbox_close(int file_desc) {
    close(file_desc);
}

static int mbox_property(int file_desc, void *msg) {
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

/* Get the error string for the error code */
const char *rpi_eeprom_ab_update_strerror(RPI_EEPROM_AB_ERROR error) {
    switch (error) {
    case RPI_EEPROM_AB_ERROR_NO_ERROR:
        return "Success";
    case RPI_EEPROM_AB_ERROR_FAILED:
        return "Unknown error. Please check you are running a firmware version that supports AB.";
    case RPI_EEPROM_AB_ERROR_INVALID_PARTITION:
        return "Invalid partition selected";
    case RPI_EEPROM_AB_ERROR_HASH_MISMATCH:
        return "Hash mismatch";
    case RPI_EEPROM_AB_ERROR_BUSY:
        return "Busy";
    case RPI_EEPROM_AB_ERROR_UPDATE:
        return "Update failed";
    case RPI_EEPROM_AB_ERROR_UNCOMMITTED:
        return "Unsafe to perform action from uncommitted partition";
    case RPI_EEPROM_AB_ERROR_INVALID_ARG:
        return "Invalid argument";
    case RPI_EEPROM_AB_ERROR_LENGTH:
        return "Length error";
    case RPI_EEPROM_AB_ERROR_ERASE:
        return "Erase failed";
    case RPI_EEPROM_AB_ERROR_WRITE:
        return "Write failed";
    case RPI_EEPROM_AB_ERROR_ALREADY_COMMITTED:
        return "Already committed";
    case RPI_EEPROM_AB_ERROR_SPI_GPIO_ERROR:
        return "SPI GPIO Error. Please enable AB Firmware in raspi-config.";
    case RPI_EEPROM_AB_ERROR_NO_PARTITIONING:
        return "AB Partitioning is not being used. Perform an AB update to enable AB partitioning.";
    default:
        return "Unrecognised error";
    }
}

/* Get the status string for the status code */
const char *rpi_eeprom_ab_update_strstatus(RPI_EEPROM_AB_UPDATE_RC_STATUS status) {
    switch (status) {
    case RPI_EEPROM_AB_UPDATE_RC_NO_UPDATE:
        return "No update";
    case RPI_EEPROM_AB_UPDATE_RC_CANCELED:
        return "Canceled";
    case RPI_EEPROM_AB_UPDATE_RC_BUSY:
        return "Busy";
    case RPI_EEPROM_AB_UPDATE_RC_SUCCSESS:
        return "Success";
    default:
        return "Unrecognised status code";
    }
}

/* Send a packet to the firmware mailbox to be written to the partition in EEPROM */
static RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_packet_write(uint32_t address,
        const uint8_t *data, size_t data_len) {
    int mb;
    int rc;
    struct firmware_update_packet_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_SET_EEPROM_PACKET;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_packet_msg);
    msg.address = address;
    msg.length = data_len;
    memcpy(msg.data, data, data_len);
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}


/* Read data from EEPROM through the firmware mailbox */
static RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_packet_read(uint32_t address,
        uint8_t *data, size_t data_len) {
    int mb;
    int rc;
    struct firmware_update_packet_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    if (!data || data_len > RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE) {
        return RPI_EEPROM_AB_ERROR_LENGTH;
    }

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_EEPROM_PACKET;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_packet_msg);
    msg.address = address;
    msg.length = data_len;
    msg.end_tag = 0;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }
    
    memcpy(data, msg.data, data_len);

    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Get the status of the EEPROM update from firmware mailbox */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_status(RPI_EEPROM_AB_UPDATE_RC_STATUS *status,
        RPI_EEPROM_AB_ERROR *firmware_error, uint32_t *spi_gpio_check, uint32_t *using_partitioning) {
    int mb;
    int rc;
    struct firmware_update_get_status_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_EEPROM_UPDATE_STATUS;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_get_status_msg);
    msg.end_tag = 0;

    rc = mbox_property(mb, &msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    *status = msg.status;
    *firmware_error = msg.firmware_error;
    *spi_gpio_check = msg.spi_gpio_check;
    *using_partitioning = msg.using_partitioning;
    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_spi_check(uint32_t *spi_gpio_check) {
    RPI_EEPROM_AB_UPDATE_RC_STATUS _status;
    RPI_EEPROM_AB_ERROR _firmware_error;
    uint32_t using_partitioning;

    return rpi_eeprom_ab_update_get_status(&_status, &_firmware_error,
        spi_gpio_check, &using_partitioning);
}

/* Send an update command to the firmware mailbox */
static RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_send_command(RPI_EEPROM_AB_UPDATE_COMMAND_CODE command) {
    int mb;
    int rc;
    struct firmware_update_command_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_SET_EEPROM_UPDATE_STATUS;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_command_msg);
    msg.command = command;
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Cancel the current EEPROM update */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_cancel(void) {
    return rpi_eeprom_ab_update_send_command(RPI_EEPROM_AB_UPDATE_CANCEL);
}

/* Add a journal entry to EEPROM to mark the AB partition as valid but not committed */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_set_partition(RPI_EEPROM_RELATIVE_PARTITION relative_partition, uint8_t *hash) {
    int mb;
    int rc;
    struct firmware_update_set_eeprom_partition_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_SET_EEPROM_PARTITION;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_set_eeprom_partition_msg);
    msg.relative_partition = relative_partition;
    if (hash)
        memcpy(msg.hash, hash, 32);
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Commit the chosen AB partition */
static RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_commit_partition(RPI_EEPROM_RELATIVE_PARTITION relative_partition) {
    int mb;
    int rc;
    struct firmware_update_set_ab_param_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_SET_EEPROM_AB_PARAMS;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_set_ab_param_msg);
    msg.param = EEPROM_UPDATE_PARAM_COMMIT;
    msg.value.relative_partition_to_commit = relative_partition;
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Commit the current AB partition */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_commit_current_partition(void) {
    return rpi_eeprom_ab_update_commit_partition(RPI_EEPROM_AB_CURRENT_PARTITION);
}

/* Force commit the opposite AB partition */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_force_commit_opposite(void) {
    return rpi_eeprom_ab_update_commit_partition(RPI_EEPROM_AB_OPPOSITE_PARTITION);
}

/* Set the value of the tryboot flag */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_set_tryboot(uint32_t tryboot) {
    int mb;
    int rc;
    struct firmware_update_set_ab_param_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_SET_EEPROM_AB_PARAMS;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_set_ab_param_msg);
    msg.param = EEPROM_UPDATE_PARAM_TRYBOOT;
    msg.value.tryboot_value = tryboot;
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Get the AB EEPROM parameters from firmware mailbox */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_ab_params(RPI_EEPROM_AB_PARTITION *partition,
        uint32_t *committed, uint32_t *tryboot, RPI_EEPROM_AB_PARTITION *partition_at_boot, uint32_t *committed_at_boot) {
    int mb;
    int rc;
    struct firmware_update_get_ab_params_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_EEPROM_AB_PARAMS;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_get_ab_params_msg);
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    *partition = msg.partition;
    *committed = msg.committed;
    *tryboot = msg.tryboot;
    *partition_at_boot = msg.partition_at_boot;
    *committed_at_boot = msg.committed_at_boot;
    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Get the current partition */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_current_partition(RPI_EEPROM_AB_PARTITION *partition) {
    uint32_t _committed, _tryboot;
    RPI_EEPROM_AB_PARTITION _partition_at_boot;
    uint32_t _committed_at_boot;

    return rpi_eeprom_ab_update_get_ab_params(partition, &_committed,
        &_tryboot, &_partition_at_boot, &_committed_at_boot);
}

/* Get the committed flag for the current partition */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_current_committed(uint32_t *committed) {
    uint32_t _tryboot, _committed_at_boot;
    RPI_EEPROM_AB_PARTITION _partition_at_boot, _partition;

    return rpi_eeprom_ab_update_get_ab_params(&_partition, committed,
        &_tryboot, &_partition_at_boot, &_committed_at_boot);
}

/* Get the current value of the tryboot flag */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_current_tryboot(uint32_t *tryboot) {
    uint32_t _committed, _committed_at_boot;
    RPI_EEPROM_AB_PARTITION _partition_at_boot, _partition;

    return rpi_eeprom_ab_update_get_ab_params(&_partition, &_committed,
        tryboot, &_partition_at_boot, &_committed_at_boot);
}

/* Get the partition and committed flag from during boot */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_boot_partition(RPI_EEPROM_AB_PARTITION *partition_at_boot, uint32_t *committed_at_boot) {
    uint32_t _tryboot, _committed;
    RPI_EEPROM_AB_PARTITION _partition;

    return rpi_eeprom_ab_update_get_ab_params(&_partition, &_committed,
        &_tryboot, partition_at_boot, committed_at_boot);
}

/* Get the stored hash and valid flag for the given relative partition */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_eeprom_partition(RPI_EEPROM_AB_PARTITION *committed_partition,
        RPI_EEPROM_AB_PARTITION *valid_partition, uint8_t *committed_partition_hash, uint8_t *valid_partition_hash) {
    int mb;
    int rc;
    struct firmware_update_get_eeprom_partition_msg msg = {0};

    mb = mbox_open();
    if (mb < 0)
        return RPI_EEPROM_AB_ERROR_FAILED;

    msg.hdr.buf_size = sizeof(msg);
    msg.hdr.tag = TAG_GET_EEPROM_PARTITION;
    msg.hdr.tag_buf_size = TAG_BUFFER_SIZE(struct firmware_update_get_eeprom_partition_msg);
    msg.end_tag = 0;

    rc = mbox_property(mb, (struct firmware_msg *)&msg);
    mbox_close(mb);

    if (rc < 0) {
        LOG_DEBUG("Mailbox property failed: %d\n", rc);
        return RPI_EEPROM_AB_ERROR_FAILED;
    }
    if (msg.error & VC_MAILBOX_ERROR) {
        return (RPI_EEPROM_AB_ERROR)(msg.error & ~VC_MAILBOX_ERROR);
    }

    *committed_partition = msg.committed_partition;
    *valid_partition = msg.valid_partition;
    memcpy(committed_partition_hash, msg.committed_partition_hash, 32);
    memcpy(valid_partition_hash, msg.valid_partition_hash, 32);
    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Send the update data to firmware mailbox and start the write to the EEPROM */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_write_eeprom_update(uint8_t *update_data, uint32_t update_len) {
    RPI_EEPROM_AB_ERROR err;
    uint8_t message[RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE];
    uint32_t sent_len = 0;
    int packet_length = RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE;

    if (!update_data || update_len == 0 || update_len > RPI_EEPROM_AB_PARTITION_SIZE) {
        LOG_DEBUG("Invalid update length\n");
        return RPI_EEPROM_AB_ERROR_INVALID_ARG;
    }

    while (sent_len < update_len) {
        if (sent_len + RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE > update_len) {
            packet_length = update_len - sent_len;
        }
        memcpy(message, update_data + sent_len, packet_length);

        err = rpi_eeprom_ab_update_packet_write(sent_len, message,
            packet_length);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            LOG_DEBUG("Failed to send to EEPROM: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return err;
        }
        sent_len += packet_length;
    }
    
    // Start the write to the EEPROM
    err = rpi_eeprom_ab_update_send_command(RPI_EEPROM_AB_UPDATE_START_WRITE);
    if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
        LOG_DEBUG("Failed to start write to EEPROM: %s\n",
            rpi_eeprom_ab_update_strerror(err));
        return err;
    }
    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Read the entire EEPROM and store the data in the buffer */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_read_entire_eeprom(uint8_t data[RPI_EEPROM_CAPACITY]) {
    RPI_EEPROM_AB_ERROR err;
    uint8_t packet[RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE];
    int read_len = 0;
    int packet_length = RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE;

    while (read_len < RPI_EEPROM_CAPACITY) {
        if (read_len + packet_length > RPI_EEPROM_CAPACITY) {
            packet_length = RPI_EEPROM_CAPACITY - read_len;
        }
        err = rpi_eeprom_ab_update_packet_read(read_len, packet, packet_length);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR){
            LOG_DEBUG("Failed to read from EEPROM: %s\n", 
                rpi_eeprom_ab_update_strerror(err));
            return err;
        }
        memcpy(data + read_len, packet, packet_length);
        read_len += packet_length;
    }
    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Read the partition from the EEPROM and store the data in the buffer */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_read_eeprom_partition(RPI_EEPROM_AB_PARTITION partition, uint8_t data[RPI_EEPROM_AB_PARTITION_SIZE]) {
    RPI_EEPROM_AB_ERROR err;
    uint8_t packet[RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE];
    int start_addr = (partition == RPI_EEPROM_AB_PARTITION_A) ? RPI_EEPROM_A_PARTITION_START_ADDRESS : RPI_EEPROM_B_PARTITION_START_ADDRESS;
    int read = 0;
    int packet_length = RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE;

    while (read < RPI_EEPROM_AB_PARTITION_SIZE) {
        if (read + packet_length > RPI_EEPROM_AB_PARTITION_SIZE) {
            packet_length = RPI_EEPROM_AB_PARTITION_SIZE - read;
        }
        err = rpi_eeprom_ab_update_packet_read(start_addr + read, packet, packet_length);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR){
            LOG_DEBUG("Failed to read from EEPROM: %s\n", 
                rpi_eeprom_ab_update_strerror(err));
            return err;
        }
        memcpy(data + read, packet, packet_length);
        read += packet_length;
    }
    return RPI_EEPROM_AB_ERROR_NO_ERROR;
}

/* Read the current partition from the EEPROM and store the data in the buffer */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_read_current_partition(uint8_t data[RPI_EEPROM_AB_PARTITION_SIZE]) {
    RPI_EEPROM_AB_ERROR err;
    RPI_EEPROM_AB_PARTITION partition;
    RPI_EEPROM_AB_PARTITION _partition_at_boot;
    uint32_t _committed_at_boot, _committed, _tryboot;

    err = rpi_eeprom_ab_update_get_ab_params(&partition, &_committed, &_tryboot, &_partition_at_boot, &_committed_at_boot);
    if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
        LOG_DEBUG("Failed to get AB parameters: %s\n", rpi_eeprom_ab_update_strerror(err));
        return err;
    }
    return rpi_eeprom_ab_read_eeprom_partition(partition, data);
}
