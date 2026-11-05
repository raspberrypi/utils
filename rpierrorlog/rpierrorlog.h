#ifndef RPI_ERROR_LOG_H
#define RPI_ERROR_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define RPI_ERROR_LOG_ENTRY_SIZE  8     /* error_code (u32 LE) + ~error_code (u32 LE) */
#define RPI_ERROR_LOG_MAX_SIZE    4096

typedef struct {
    uint32_t error_code;
} rpi_error_log_t;

/**
 * Read all error log entries from the EEPROM via the firmware mailbox.
 *
 * Sends the maximum buffer size and uses the firmware-returned length
 * to determine how many entries were actually read.
 *
 * @param entries      Output buffer for parsed entries.
 * @param max_entries  Size of the entries buffer.
 * @param out_count    Output: number of entries returned by firmware.
 * @return             0 on success, -1 on firmware/ioctl error.
 */
int rpi_error_log_get(rpi_error_log_t *entries, size_t max_entries, size_t *out_count);

/**
 * Clear the EEPROM error log via the firmware mailbox (SET_EEPROM_ERROR_LOGS).
 *
 * @return  0 on success, -1 on firmware/ioctl error.
 */
int rpi_error_log_clear(void);

/**
 * Translate a bootrom error code to a human-readable string.
 *
 * @param error_code  The error_code field from rpi_error_log_t.
 * @return            A constant string describing the error.
 */
const char *rpi_error_log_strerror(uint32_t error_code);

#ifdef __cplusplus
}
#endif

#endif /* RPI_ERROR_LOG_H */
