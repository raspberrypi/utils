#include <stdio.h>
#include <string.h>
#include "rpierrorlog.h"

static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s <command>\n"
        "\n"
        "Commands:\n"
        "  get    Read and print all EEPROM error log entries\n"
        "         (Pi 4 and Pi 5 family boards only)\n"
        "  clear  Clear the EEPROM error log and verify it is empty\n"
        "         (Pi 5 family boards only)\n",
        progname);
}

static int cmd_get(void)
{
    rpi_error_log_t entries[RPI_ERROR_LOG_MAX_SIZE / RPI_ERROR_LOG_ENTRY_SIZE];
    size_t count;

    if (rpi_error_log_get(entries, sizeof(entries) / sizeof(entries[0]), &count) < 0) {
        fprintf(stderr, "Failed to read error log\n");
        return -1;
    }

    if (count == 0) {
        printf("No error log entries\n");
        return 0;
    }

    for (size_t i = 0; i < count; i++)
        printf("Entry %zu: 0x%08x - %s\n", i, entries[i].error_code, rpi_error_log_strerror(entries[i].error_code));

    return 0;
}

static int cmd_clear(void)
{
    rpi_error_log_t entries[RPI_ERROR_LOG_MAX_SIZE / RPI_ERROR_LOG_ENTRY_SIZE];
    size_t count;

    if (rpi_error_log_clear() < 0) {
        fprintf(stderr, "Failed to clear error log\n");
        return -1;
    }
    printf("Error log cleared\n");

    if (rpi_error_log_get(entries, sizeof(entries) / sizeof(entries[0]), &count) < 0) {
        fprintf(stderr, "Verification read failed\n");
        return -1;
    }
    if (count != 0) {
        fprintf(stderr, "Verification failed: %zu entries still present after clear\n", count);
        return -1;
    }
    printf("Verification OK: error log is empty\n");
    return 0;
}

int main(int argc, char *argv[])
{
    int rc = -1;

    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "get") == 0) {
        rc = cmd_get();
        if (rc < 0)
            goto error;
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        rc = cmd_clear();
        if (rc < 0)
            goto error;
        return 0;
    }

    usage(argv[0]);
    return 1;

error:
    fprintf(stderr, "Command '%s' failed\n", argv[1]);
    return rc;
}
