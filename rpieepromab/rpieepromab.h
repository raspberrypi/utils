#ifndef RPI_EEPROM_AB_UPDATE_H
#define RPI_EEPROM_AB_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#define RPI_EEPROM_AB_UPDATE_PACKET_MAX_SIZE  (512 * 1024)
#define RPI_EEPROM_AB_PARTITION_SIZE          (988 * 1024)
#define RPI_EEPROM_A_PARTITION_START_ADDRESS  (64 * 1024)
#define RPI_EEPROM_B_PARTITION_START_ADDRESS  (RPI_EEPROM_A_PARTITION_START_ADDRESS + RPI_EEPROM_AB_PARTITION_SIZE)
#define RPI_EEPROM_CAPACITY                   (2 * 1024 * 1024)

/* Status codes */
typedef enum {
    RPI_EEPROM_AB_UPDATE_RC_NO_UPDATE      = 0,
    RPI_EEPROM_AB_UPDATE_RC_CANCELED       = 1,
    RPI_EEPROM_AB_UPDATE_RC_BUSY           = 2,
    RPI_EEPROM_AB_UPDATE_RC_SUCCSESS       = 3,
} RPI_EEPROM_AB_UPDATE_RC_STATUS;

/* Error codes */
typedef enum {
    RPI_EEPROM_AB_ERROR_NO_ERROR           = 0,
    RPI_EEPROM_AB_ERROR_FAILED             = 1,
    RPI_EEPROM_AB_ERROR_INVALID_PARTITION  = 2,
    RPI_EEPROM_AB_ERROR_HASH_MISMATCH      = 3,
    RPI_EEPROM_AB_ERROR_BUSY               = 4,
    RPI_EEPROM_AB_ERROR_UPDATE             = 5,
    RPI_EEPROM_AB_ERROR_UNCOMMITTED        = 6,
    RPI_EEPROM_AB_ERROR_INVALID_ARG        = 7,
    RPI_EEPROM_AB_ERROR_LENGTH             = 8,
    RPI_EEPROM_AB_ERROR_ERASE              = 9,
    RPI_EEPROM_AB_ERROR_WRITE              = 10,
    RPI_EEPROM_AB_ERROR_ALREADY_COMMITTED  = 11,
    RPI_EEPROM_AB_ERROR_SPI_GPIO_ERROR     = 12,
    RPI_EEPROM_AB_ERROR_NO_PARTITIONING    = 13,
} RPI_EEPROM_AB_ERROR;

typedef enum {
    RPI_EEPROM_AB_PARTITION_A = 1,
    RPI_EEPROM_AB_PARTITION_B = 2,
} RPI_EEPROM_AB_PARTITION;

typedef enum {
    RPI_EEPROM_AB_CURRENT_PARTITION  = 0,
    RPI_EEPROM_AB_OPPOSITE_PARTITION = 1,
} RPI_EEPROM_RELATIVE_PARTITION;

/**
 * Get the error string for the error code.
 *
 * @param error The error code
 * @return The error string
 */
const char *rpi_eeprom_ab_update_strerror(RPI_EEPROM_AB_ERROR error);

/**
 * Get the status string for the status code.
 *
 * @param status The status code
 * @return The status string
 */
const char *rpi_eeprom_ab_update_strstatus(RPI_EEPROM_AB_UPDATE_RC_STATUS status);

/**
 * Get the status of the EEPROM update.
 *
 * @param status Pointer to the status
 * @param firmware_error Pointer to the firmware error
 * @param spi_gpio_check Pointer to the SPI GPIO check flag
 * @param using_partitioning Pointer to the using partitioning flag
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_status(RPI_EEPROM_AB_UPDATE_RC_STATUS *status, RPI_EEPROM_AB_ERROR *firmware_error, uint32_t *spi_gpio_check, uint32_t *using_partitioning);

/**
 * Get the SPI GPIO check flag to indicate if the firmware can access SPI EEPROM.
 *
 * @param spi_gpio_check Pointer to the SPI GPIO check flag
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_spi_check(uint32_t *spi_gpio_check);

/**
 * Cancel the current EEPROM AB update write.
 *
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_cancel(void);


/**
 * Get the AB EEPROM parameters from firmware mailbox
 *
 * @param partition Current partition (RPI_EEPROM_AB_PARTITION_A or RPI_EEPROM_AB_PARTITION_B)
 * @param committed Committed flag for the current partition
 * @param tryboot Tryboot flag
 * @param partition_at_boot Partition that was used at boot
 * @param committed_at_boot Committed flag at time of boot
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_ab_params(RPI_EEPROM_AB_PARTITION *partition, uint32_t *committed, uint32_t *tryboot, RPI_EEPROM_AB_PARTITION *partition_at_boot, uint32_t *committed_at_boot);

/**
 * Get the committed and valid partitions and their hashes
 *
 * @param committed_partition Pointer to committed partition select
 * @param valid_partition Pointer to valid partition select
 * @param committed_partition_hash Pointer to the committed partition hash
 * @param valid_partition_hash Pointer to the valid partition hash
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_eeprom_partition(RPI_EEPROM_AB_PARTITION *committed_partition,
        RPI_EEPROM_AB_PARTITION *valid_partition, uint8_t *committed_partition_hash, uint8_t *valid_partition_hash);

/**
 * Get the current partition and committed flag
 *
 * @param partition Pointer to the current partition
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_current_partition(RPI_EEPROM_AB_PARTITION *partition);

/**
 * Get the committed flag for the current partition
 *
 * @param committed Pointer to the committed flag
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_current_committed(uint32_t *committed);

/**
 * Get the current value of the tryboot flag
 *
 * @param tryboot Pointer to the tryboot flag
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_current_tryboot(uint32_t *tryboot);

/**
 * Get the partition and committed flag from during boot
 *
 * @param partition_at_boot Pointer to the partition at boot
 * @param committed_at_boot Pointer to the committed flag at boot
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_get_boot_partition(RPI_EEPROM_AB_PARTITION *partition_at_boot, uint32_t *committed_at_boot);

/**
 * Add a journal entry to EEPROM to mark the AB partition as valid but not committed
 *
 * @param relative_partition The partition to set, either the current or opposite partition
 * @param hash Pointer to the hash of the AB partition (size of 32 bytes)
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_set_partition(RPI_EEPROM_RELATIVE_PARTITION relative_partition, uint8_t *hash);
 
/**
 * Commit the current AB partition.
 *
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_commit_current_partition(void);

/**
 * Commit the opposite AB partition. This should only be used if it can be garunteed
 * that the image that has been written will boot successfully. If the image is incorrect,
 * the system will not recover and will need to be reflashed manually.
 *
 * @param partition The partition to commit
 * @return 0 on success, error code on failure
 */
 RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_force_commit_opposite(void);
 
/**
 * Set the value of the tryboot flag.
 *
 * @param tryboot The value to set the tryboot flag to
 * @return 0 on success, error code on failure
 */
 RPI_EEPROM_AB_ERROR rpi_eeprom_ab_update_set_tryboot(uint32_t tryboot);

/**
 * Write update data to the EEPROM.
 *
 * @param update_data Pointer to the update data
 * @param update_len The length of the update data
 * @return 0 on success
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_write_eeprom_update(uint8_t *update_data, uint32_t update_len);

/**
 * Read the entire EEPROM and store the data in the buffer.
 *
 * @param data[RPI_EEPROM_CAPACITY] Data buffer to store the read data
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_read_entire_eeprom(uint8_t data[RPI_EEPROM_CAPACITY]);

/**
 * Read the current partition from the EEPROM and store the data in the buffer.
 *
 * @param data[RPI_EEPROM_AB_PARTITION_SIZE] Data buffer to store the read data
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_read_current_partition(uint8_t data[RPI_EEPROM_AB_PARTITION_SIZE]);

/**
 * Read the partition from the EEPROM and store the data in the buffer.
 *
 * @param partition The partition to read
 * @param data[RPI_EEPROM_AB_PARTITION_SIZE] Data buffer to store the read data
 * @return 0 on success, error code on failure
 */
RPI_EEPROM_AB_ERROR rpi_eeprom_ab_read_eeprom_partition(RPI_EEPROM_AB_PARTITION partition, uint8_t data[RPI_EEPROM_AB_PARTITION_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* RPI_EEPROM_AB_UPDATE_H */
