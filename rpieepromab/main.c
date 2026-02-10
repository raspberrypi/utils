#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "rpieepromab.h"

#define PARTITION_NUM_TO_STRING(P) (P == RPI_EEPROM_AB_PARTITION_A ? "A" : "B")

/* Print the usage message */
static void usage(const char *progname, int exit_status) {
    fprintf(stderr,
        "Usage: %s <command> [args]\n"
        "\n"
        "This application provides a command line interface to update the Raspberry Pi\n"
        "AB EEPROM partitions.\n"
        "\n"
        "Commands:\n"
        "  update <update.bin>            Update the opposite partition of the EEPROM\n"
        "                                 with the contents of the file\n"
        "  read <out.bin>                 Read the current AB partition and write the\n"
        "                                 contents to a file\n"
        "  dump <out.bin>                 Read the entire EEPROM and write the\n"
        "                                 contents to a file\n"
        "  update-status                  Get the status of the EEPROM update and any\n"
        "                                 error codes\n"
        "  partition                      Get the current AB partition select of the\n"
        "                                 EEPROM\n"
        "  mark-partition-valid <hash>    Mark the AB partition thats not committed as\n"
        "                                 valid if hash matches the calculated hash of\n"
        "                                 the partition\n"
        "  revert-to-committed <hash>     Mark the committed AB partition as valid\n"
        "                                 again to stop a valid uncommitted partition\n"
        "                                 from being used by tryboot. Hash must match\n"
        "                                 the calculated hash of the committed partition\n"
        "  tryboot                        Get the current value of tryboot\n"
        "  tryboot <tryboot>              Set the value of tryboot to 0 or 1\n"
        "  committed                      Get whether the current AB partition is\n"
        "                                 committed\n"
        "  commit                         Commit the current AB partition\n"
        "  force-commit-opposite          Force commit the opposite partition\n"
        "                                 (use with caution)\n"
        "  partition-status               Get the committed and valid partition\n"
        "                                 selections and their hashes\n"
        "  status-at-boot                 Get the partition used at boot and the\n"
        "                                 committed status at boot\n"
        "  help                           Show this help message\n",
        progname);
    exit(exit_status);
}

/* Convert a hex string to a binary array */
static int hex2bin(const char *hexstr, uint8_t *bin, size_t bin_len) {
    size_t hex_len;
    size_t i;
    if (!hexstr || !bin) {
        return -1;
    }
    hex_len = strlen(hexstr);
    if (hex_len != bin_len * 2) {
        return -1;
    }
    for (i = 0; i < bin_len; i++) {
        if (sscanf(hexstr + i * 2, "%2hhx", &bin[i]) != 1) {
            return -1;
        }
    }
    return 0;
}

/* Wait for the write to the EEPROM to complete */
static int wait_for_eeprom_update_write(void) {
    RPI_EEPROM_AB_ERROR err;
    RPI_EEPROM_AB_UPDATE_RC_STATUS status;
    RPI_EEPROM_AB_ERROR firmware_error;
    int max_wait = 15;

    printf("Waiting for write to EEPROM to complete\n");
    for (int i = 0; i < max_wait; i++) {
        uint32_t _spi_gpio_check, _using_partitioning;

        err = rpi_eeprom_ab_update_get_status(&status, &firmware_error,
            &_spi_gpio_check, &_using_partitioning);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR){
            printf("Failed to get EEPROM update status: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        } else {
            if (status == RPI_EEPROM_AB_UPDATE_RC_SUCCSESS) {
                printf("\nCompleted\n");
                break;
            } else if (status == RPI_EEPROM_AB_UPDATE_RC_BUSY) {
                printf("."); fflush(stdout);
            } else {
                printf("EEPROM update firmware error: %s\n",
                    rpi_eeprom_ab_update_strerror(firmware_error));
                return -1;
            }
        }
        sleep(1);
    }
    return 0;
}

static int cmd_write_eeprom_update(int argc, char *argv[]) {
    RPI_EEPROM_AB_ERROR err;
    int ret = -1;
    const char *update_filename = NULL;
    FILE *f;
    uint8_t update_data[RPI_EEPROM_AB_PARTITION_SIZE];
    long file_size;

    if (argc < 3) {
        return -1;
    }
    update_filename = argv[2];

    if (!update_filename) {
        printf("Update file is required\n");
        usage(argv[0], 1);
        return -1;
    }

    f = fopen(update_filename, "rb");
    if (!f) {
        printf("Failed to read file: %s\n", update_filename);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        printf("Failed to seek to end of file: %s\n", update_filename);
        return -1;
    }

    file_size = ftell(f);
    if (file_size < 0) {
        printf("Failed to get file size: %s\n", update_filename);
        return -1;
    }
    printf("file_size: %ld\n", file_size);
    if (file_size != RPI_EEPROM_AB_PARTITION_SIZE) {
        printf("File size is not a valid AB update size: %ld\n", file_size);
        return -1;
    }

    rewind(f);
    if (fread(update_data, 1, file_size, f) != (size_t) file_size) {
        printf("Failed to read file: %s\n", update_filename);
        return -1;
    }

    fclose(f);
    err = rpi_eeprom_ab_write_eeprom_update(update_data, file_size);
    if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
        if (err == RPI_EEPROM_AB_ERROR_BUSY) {
            printf("Failed to write update. EEPROM is busy.\n");
            return -1;
        } else if (err == RPI_EEPROM_AB_ERROR_UNCOMMITTED) {
            printf("Failed to write update. Cannot write from an uncommitted partition.\n");
            return -1;
        }
        printf("Failed to write update: %s\n", rpi_eeprom_ab_update_strerror(ret));
        return -1;
    }

    ret = wait_for_eeprom_update_write();
    if (ret < 0) {
        printf("Failed to wait for write to EEPROM to complete\n");
        return -1;
    }

    printf("Write to EEPROM completed\n");
    return 0;
}

static int cmd_eeprom_dump(int argc, char *argv[]) {
    RPI_EEPROM_AB_ERROR err;
    const char *outfile = NULL;
    FILE *f;
    uint8_t data[RPI_EEPROM_CAPACITY];

    if (argc < 3) {
        return -1;
    }
    outfile = argv[2];
    if (!outfile) {
        usage(argv[0], 1);
        return -1;
    }

    f = fopen(outfile, "wb");
    if (!f) {
        printf("Failed to open file: %s\n", outfile);
        return -1;
    }

    err = rpi_eeprom_ab_read_entire_eeprom(data);
    if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
        printf("Failed to read from EEPROM: %s\n", 
            rpi_eeprom_ab_update_strerror(err));
        return -1;
    }

    if (fwrite(data, 1, RPI_EEPROM_CAPACITY, f) != RPI_EEPROM_CAPACITY) {
        printf("Failed to write to file: %s\n", outfile);
        return -1;
    }

    fclose(f);
    printf("EEPROM dump completed\n");
    return 0;
}

static int cmd_eeprom_read_partition(int argc, char *argv[]) {
    RPI_EEPROM_AB_ERROR err;
    const char *outfile = NULL;
    FILE *f;
    uint8_t data[RPI_EEPROM_AB_PARTITION_SIZE];
    RPI_EEPROM_AB_PARTITION partition;

    if (argc < 3) {
        return -1;
    }
    outfile = argv[2];
    if (!outfile) {
        usage(argv[0], 1);
        return -1;
    }

    f = fopen(outfile, "wb");
    if (!f) {
        printf("Failed to open file: %s\n", outfile);
        return -1;
    }

    err = rpi_eeprom_ab_update_get_current_partition(&partition);
    if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
        printf("Failed to get AB partition: %s\n",
            rpi_eeprom_ab_update_strerror(err));
        return -1;
    }

    err = rpi_eeprom_ab_read_eeprom_partition(partition, data);
    if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
        printf("Failed to read from EEPROM: %s\n",
            rpi_eeprom_ab_update_strerror(err));
        return -1;
    }

    if (fwrite(data, 1, RPI_EEPROM_AB_PARTITION_SIZE, f) != RPI_EEPROM_AB_PARTITION_SIZE) {
        printf("Failed to write to file: %s\n", outfile);
        return -1;
    }

    fclose(f);
    printf("EEPROM partition read completed\n");
    return 0;
}

int main(int argc, char *argv[]) {
    RPI_EEPROM_AB_ERROR err;

    if (argc < 2) {
        usage(argv[0], 1);
    }

    if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0 || 
            strcmp(argv[1], "help") == 0) {
        usage(argv[0], 0);
        return 0;
    } else if (strcmp(argv[1], "update") == 0) {
        // Update the EEPROM partition with the contents of the file
        if (argc < 3) {
            usage(argv[0], 1);
            return -1;
        }
        if (cmd_write_eeprom_update(argc, argv) < 0) {
            return -1;
        }
        return 0;
    } else if (strcmp(argv[1], "dump") == 0) {
        // Dump the entire EEPROM to a file
        if (argc < 3) {
            usage(argv[0], 1);
            return -1;
        }
        if (cmd_eeprom_dump(argc, argv) < 0) {
            return -1;
        }
        return 0;
    } else if (strcmp(argv[1], "read") == 0) {
        // Read the current partition from the EEPROM and write the contents to a file
        if (argc < 3) {
            usage(argv[0], 1);
            return -1;
        }
        if (cmd_eeprom_read_partition(argc, argv) < 0) {
            return -1;
        }
    } else if (strcmp(argv[1], "update-status") == 0) {
        // Get the status of the firmware update of the EEPROM
        RPI_EEPROM_AB_UPDATE_RC_STATUS status;
        RPI_EEPROM_AB_ERROR firmware_error;
        uint32_t _spi_gpio_check, _using_partitioning;
        
        err = rpi_eeprom_ab_update_get_status(&status, &firmware_error,
            &_spi_gpio_check, &_using_partitioning);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to get EEPROM update status: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        printf("EEPROM update status: %s\n", rpi_eeprom_ab_update_strstatus(status));
        if (firmware_error != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("EEPROM update firmware error: %s\n",
                rpi_eeprom_ab_update_strerror(firmware_error));
        }

        return 0;
    } else if (strcmp(argv[1], "spi-check") == 0) {
        // Check if the firmware can access SPI EEPROM
        uint32_t spi_gpio_check;

        err = rpi_eeprom_ab_update_get_spi_check(&spi_gpio_check);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to get SPI check: %s\n", rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        if (spi_gpio_check == 1) {
            printf("SPI check: OK\n");
            return 0;
        } else {
            printf("SPI check: Failed\n");
            return -1;
        }
    } else if (strcmp(argv[1], "partition") == 0) {
        RPI_EEPROM_AB_PARTITION partition;

        if (argc == 2) {
            err = rpi_eeprom_ab_update_get_current_partition(&partition);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed to get EEPROM AB partition: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            printf("%s\n", PARTITION_NUM_TO_STRING(partition));
            return 0;
        } else {
            usage(argv[0], 1);
            return -1;
        }
    } else if (strcmp(argv[1], "mark-partition-valid") == 0) {
        // Mark the uncommitted partition as valid
        if (argc == 3) {
            RPI_EEPROM_RELATIVE_PARTITION relative_partition;
            uint8_t hash[32];
            uint32_t committed;

            err = rpi_eeprom_ab_update_get_current_committed(&committed);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            if (committed == 1) {
                relative_partition = RPI_EEPROM_AB_OPPOSITE_PARTITION;
            } else {
                printf("Can't mark a partition as valid from an uncommitted partition.\n");
                return -1;
            }

            // Convert the hex string to a uint8_t array
            if (hex2bin(argv[2], hash, sizeof(hash)) != 0) {
                printf("Invalid hash string\n");
                return -1;
            }

            err = rpi_eeprom_ab_update_set_partition(relative_partition, hash);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed to set EEPROM AB partition: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            printf("Next EEPROM AB partition marked valid\n");
            return 0;
        } else {
            usage(argv[0], 1);
            return -1;
        }
    } else if (strcmp(argv[1], "revert-to-committed") == 0) {
        // Mark the committed partition as the latest valid partition to
        // overwrite any previous attempt to mark the uncommitted partition valid
        if (argc == 3) {
            RPI_EEPROM_RELATIVE_PARTITION relative_partition;
            uint32_t committed;

            err = rpi_eeprom_ab_update_get_current_committed(&committed);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            if (committed == 1) {
                relative_partition = RPI_EEPROM_AB_CURRENT_PARTITION;
            } else {
                relative_partition = RPI_EEPROM_AB_OPPOSITE_PARTITION;
            }

            uint8_t hash[32];
            // Convert the hex string to a uint8_t array
            if (hex2bin(argv[2], hash, sizeof(hash)) != 0) {
                printf("Invalid hash string\n");
                return -1;
            }

            err = rpi_eeprom_ab_update_set_partition(relative_partition, hash);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed to set EEPROM AB partition: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            printf("Reverted to uncommitted valid partition\n");
            return 0;
        } else {
            usage(argv[0], 1);
            return -1;
        }
    } else if (strcmp(argv[1], "tryboot") == 0) {
        uint32_t tryboot;
        if (argc == 2) {
            err = rpi_eeprom_ab_update_get_current_tryboot(&tryboot);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed to get EEPROM tryboot: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            printf("%d\n", (int) tryboot);
            return 0;
        } else if (argc == 3) {
            tryboot = atoi(argv[2]);

            err = rpi_eeprom_ab_update_set_tryboot(tryboot);
            if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
                printf("Failed to set EEPROM tryboot: %s\n", rpi_eeprom_ab_update_strerror(err));
                return -1;
            }

            printf("EEPROM tryboot set to: %d\n", (int) tryboot);
            return 0;
        } else {
            usage(argv[0], 1);
            return -1;
        }
    } else if (strcmp(argv[1], "committed") == 0) {
        uint32_t committed;

        err = rpi_eeprom_ab_update_get_current_committed(&committed);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to get EEPROM AB committed: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        printf("%d\n", (int) committed);
        return 0;
    } else if (strcmp(argv[1], "commit") == 0) {
        if (argc < 2) {
            usage(argv[0], 1);
            return -1;
        }

        err = rpi_eeprom_ab_update_commit_current_partition();
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to commit EEPROM update: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        printf("Committed current EEPROM partition\n");
        return 0;
    } else if (strcmp(argv[1], "force-commit-opposite") == 0) {
        err = rpi_eeprom_ab_update_force_commit_opposite();
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to commit EEPROM update: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        printf("Force committed opposite EEPROM partition\n");
        return 0;
    } else if (strcmp(argv[1], "partition-status") == 0) {
        RPI_EEPROM_AB_PARTITION committed_partition, valid_partition;
        uint8_t committed_partition_hash[32], valid_partition_hash[32];

        err = rpi_eeprom_ab_update_get_eeprom_partition(&committed_partition,
            &valid_partition, committed_partition_hash, valid_partition_hash);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to get partition status: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        printf("EEPROM committed partition: %s\n", PARTITION_NUM_TO_STRING(committed_partition));
        printf("EEPROM valid partition: %s\n", PARTITION_NUM_TO_STRING(valid_partition));

        printf("EEPROM committed partition hash: ");
        for (int i = 0; i < 32; i++) {
            printf("%02x", committed_partition_hash[i]);
        }
        printf("\n");

        printf("EEPROM valid partition hash: ");
        for (int i = 0; i < 32; i++) {
            printf("%02x", valid_partition_hash[i]);
        }
        printf("\n");

        return 0;
    } else if (strcmp(argv[1], "status-at-boot") == 0) {
        RPI_EEPROM_AB_PARTITION partition_at_boot;
        uint32_t committed_at_boot;

        err = rpi_eeprom_ab_update_get_boot_partition(&partition_at_boot, &committed_at_boot);
        if (err != RPI_EEPROM_AB_ERROR_NO_ERROR) {
            printf("Failed to get EEPROM status at boot: %s\n",
                rpi_eeprom_ab_update_strerror(err));
            return -1;
        }

        printf("EEPROM partition used at boot: %s\n", PARTITION_NUM_TO_STRING(partition_at_boot));
        printf("EEPROM committed status at boot: %d\n", committed_at_boot);
        return 0;
    } else {
        printf("Invalid command: %s\n", argv[1]);
        usage(argv[0], 1);
        return -1;
    }
}
