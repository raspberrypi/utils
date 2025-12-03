#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <gnutls/crypto.h>
#include "rpifwcrypto.h"

#define SHA256_HASH_SIZE 32

static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s <command> [args]\n"
        "\n"
        "This application provides a command line interface for the Raspberry Pi\n"
        "firmware cryptography service. This service enables a limited set of operations to\n"
        "be performed with keys stored in OTP without exposing the raw key to userspace.\n"
        "\n"
        "Commands:\n"
        "  get-num-otp-keys                 Gets the number of OTP keys\n"
        "\n"
        "  get-key-status <key-id>          Gets the status of a specified key\n"
        "     STATUS is a bitmask of the following flags:\n"
        "     - DEVICE - Key is the device unique private key\n"
        "     - LOCKED - The key cannot be read raw format. LOCKED persists until reboot.\n"
        "\n"
        "  set-key-status <key-id> [LOCKED] Sets the status attributes for the specified key.\n"
        "\n"
        "  sign --in <infile> --key-id <id> --alg <alg> [--out <outfile>] [--outform hex]\n"
        "\n"
        "    Supported algorithms: ec\n"
        "    --outform hex: Output signature in hexadecimal format\n"
        "    If --out is omitted, writes to stdout\n"
        "\n"
        "  hmac --in <infile> --key-id <id> [--out <outfile>] [--outform hex]\n"
        "    Calculates HMAC-SHA256 of input file (max 2KB) using the specified key\n"
        "    --outform hex: Output HMAC in hexadecimal format\n"
        "    If --out is omitted, writes to stdout\n"
        "\n"
        "  pubkey --key-id <id> [--out <outfile>] [--outform hex]\n"
        "    Retrieves the public key in DER format for the specified key\n"
        "    --outform hex: Output public key in hexadecimal format\n"
        "    If --out is omitted, writes to stdout\n"
        "\n"
        "  privkey --key-id <id> [--out <outfile>] [--outform hex]\n"
        "    Retrieves the private key for the specified key\n"
        "    --outform hex: Output private key in hexadecimal format\n"
        "    If --out is omitted, writes to stdout\n"
        "\n"
        "  genkey --key-id <id> --alg <alg>\n"
        "    Generates a private key in the specified key slot\n"
        "    Supported algorithms: ec\n"
        "    The key-slot must be unlocked and blank\n"
        "    Use pubkey or privkey commands to retrieve the generated key\n",
        progname);
    exit(1);
}

static int hash_file(const char *filename, unsigned char *hash, size_t hash_size)
{
    FILE *f;
    gnutls_hash_hd_t hash_handle;
    unsigned char buffer[4096];
    unsigned char temp_hash[SHA256_HASH_SIZE];
    size_t bytes;
    int rc;

    if (hash_size < SHA256_HASH_SIZE) {
        fprintf(stderr, "Hash buffer too small. Need at least %d bytes\n", SHA256_HASH_SIZE);
        return -1;
    }

    if ((rc = gnutls_hash_init(&hash_handle, GNUTLS_DIG_SHA256)) < 0) {
        fprintf(stderr, "Error initializing hash: %s\n", gnutls_strerror(rc));
        return -1;
    }

    f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open input file");
        gnutls_hash_deinit(hash_handle, NULL);
        return -1;
    }

    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if ((rc = gnutls_hash(hash_handle, buffer, bytes)) < 0) {
            fprintf(stderr, "Error updating hash: %s\n", gnutls_strerror(rc));
            fclose(f);
            gnutls_hash_deinit(hash_handle, NULL);
            return -1;
        }
    }

    if (ferror(f)) {
        perror("Error reading file");
        fclose(f);
        gnutls_hash_deinit(hash_handle, NULL);
        return -1;
    }

    fclose(f);

    /* Get the hash output, ensuring we don't write beyond the provided buffer */
    gnutls_hash_deinit(hash_handle, temp_hash);
    memcpy(hash, temp_hash, hash_size < SHA256_HASH_SIZE ? hash_size : SHA256_HASH_SIZE);

    return 0;
}

static int write_hex_output_to_stream(FILE *f, const unsigned char *data, size_t len)
{
    int rc = 0;

    for (size_t i = 0; i < len; i++) {
        if (fprintf(f, "%02x", data[i]) < 0) {
            rc = -1;
            break;
        }
    }

    return rc;
}

static int write_hex_output(const char *outfile, const unsigned char *data, size_t len)
{
    FILE *f;
    int rc;

    if (!outfile)
        return write_hex_output_to_stream(stdout, data, len);

    f = fopen(outfile, "w");
    if (!f)
        return -1;

    rc = write_hex_output_to_stream(f, data, len);
    fclose(f);
    return rc;
}

static int write_binary_output(const char *outfile, const unsigned char *data, size_t len)
{
    FILE *f;
    size_t written;

    if (!outfile) {
        written = fwrite(data, 1, len, stdout);
        return written == len ? 0 : -1;
    }

    f = fopen(outfile, "wb");
    if (!f)
        return -1;

    written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

static int cmd_hmac(int argc, char *argv[])
{
    const char *infile = NULL;
    const char *outfile = NULL;
    const char *outform = NULL;
    int key_id = -1;
    int i;
    int rc;
    FILE *f;
    unsigned char hmac[32];
    uint8_t message[RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE];
    size_t file_size;
    const uint32_t flags = 0;

    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--in") == 0 && i + 1 < argc)
            infile = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outfile = argv[++i];
        else if (strcmp(argv[i], "--key-id") == 0 && i + 1 < argc)
            key_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--outform") == 0 && i + 1 < argc)
            outform = argv[++i];
    }

    if (!infile || key_id < 0)
        usage(argv[0]);

    f = fopen(infile, "rb");
    if (!f)
        goto fail_read;

    file_size = fread(message, 1, RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE, f);
    if ((file_size < RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE) && ferror(f))
        goto fail_read;

    // Detect files that are too large (but don't false-positive files that are
    // exactly MSG_MAX_SIZE large)
    if (!feof(f) && !(!fread(hmac, 1, 1, f) && feof(f))) {
        fprintf(stderr, "Input file too large (max %d bytes)\n", RPI_FW_CRYPTO_HMAC_MSG_MAX_SIZE);
        goto fail_read;
    }

    // Calculate HMAC-SHA256 and write the output to a file
    rc = rpi_fw_crypto_hmac_sha256(flags, key_id, message, file_size, hmac);
    if (rc < 0)
        goto fail_write;

    if (outform && strcmp(outform, "hex") == 0) {
        if (write_hex_output(outfile, hmac, sizeof(hmac)) < 0)
            goto fail_write;
    } else {
        if (write_binary_output(outfile, hmac, sizeof(hmac)) < 0)
            goto fail_write;
    }

    rc = 0;
    goto end;

fail_read:
    rc = -1;
    goto end;
fail_write:
    rc = -1;
    goto end;
end:
    if (f)
        fclose(f);
    return rc;
}

static int cmd_sign(int argc, char *argv[])
{
    const char *infile = NULL;
    const char *outfile = NULL;
    const char *alg = NULL;
    const char *outform = NULL;
    int key_id = -1;
    int i;
    int rc;
    unsigned char hash[SHA256_HASH_SIZE];
    unsigned char sig[128];
    size_t sig_len = 0;
    const uint32_t flags = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--in") == 0 && i + 1 < argc)
            infile = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outfile = argv[++i];
        else if (strcmp(argv[i], "--key-id") == 0 && i + 1 < argc)
            key_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--alg") == 0 && i + 1 < argc)
            alg = argv[++i];
        else if (strcmp(argv[i], "--outform") == 0 && i + 1 < argc)
            outform = argv[++i];
    }

    if (!infile || key_id < 0 || !alg)
        usage(argv[0]);

    if (strcmp(alg, "ec") != 0) {
        fprintf(stderr, "Unsupported algorithm: %s\n", alg);
        return -1;
    }

    if (hash_file(infile, hash, sizeof(hash)) < 0)
        return -1;

    rc = rpi_fw_crypto_ecdsa_sign(flags, (uint32_t)key_id, hash, sizeof(hash), sig, sizeof(sig), &sig_len);
    if (rc < 0) {
        fprintf(stderr, "Failed to sign data\n");
        return -1;
    }

    /* Write signature to output file */
    if (outform && strcmp(outform, "hex") == 0) {
        if (write_hex_output(outfile, sig, sig_len) < 0) {
            perror("Failed to write signature");
            return -1;
        }
    } else {
        if (write_binary_output(outfile, sig, sig_len) < 0) {
            perror("Failed to write signature");
            return -1;
        }
    }

    return 0;
}

static int parse_key_status_args(int argc, char *argv[], int start_idx, int *out_status) {
    int status = 0;
    int i;
    for (i = start_idx; i < argc; ++i) {
        if (strcmp(argv[i], "LOCKED") == 0) {
            status |= ARM_CRYPTO_KEY_STATUS_LOCKED;
        } else {
            fprintf(stderr, "Unknown or unsupported key status string: %s\n", argv[i]);
            return -1;
        }
    }
    *out_status = status;
    return 0;
}

static int cmd_pubkey(int argc, char *argv[])
{
    const char *outfile = NULL;
    const char *outform = NULL;
    int key_id = -1;
    int i;
    int rc;
    uint8_t pubkey[RPI_FW_CRYPTO_PUBLIC_KEY_MAX_SIZE];
    size_t pubkey_len;
    const uint32_t flags = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outfile = argv[++i];
        else if (strcmp(argv[i], "--key-id") == 0 && i + 1 < argc)
            key_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--outform") == 0 && i + 1 < argc)
            outform = argv[++i];
    }

    if (key_id < 0)
        usage(argv[0]);

    rc = rpi_fw_crypto_get_pubkey(flags, key_id, pubkey, sizeof(pubkey), &pubkey_len);
    if (rc < 0) {
        fprintf(stderr, "Failed to get public key: %s\n", rpi_fw_crypto_strerror(rpi_fw_crypto_get_last_error()));
        return -1;
    }

    if (outform && strcmp(outform, "hex") == 0) {
        if (write_hex_output(outfile, pubkey, pubkey_len) < 0) {
            perror("Failed to write public key");
            return -1;
        }
    } else {
        if (write_binary_output(outfile, pubkey, pubkey_len) < 0) {
            perror("Failed to write public key");
            return -1;
        }
    }

    return 0;
}

static int cmd_privkey(int argc, char *argv[])
{
    const char *outfile = NULL;
    const char *outform = NULL;
    int key_id = -1;
    int i;
    int rc;
    uint8_t privkey[RPI_FW_CRYPTO_PRIVATE_KEY_MAX_SIZE];
    size_t privkey_len;
    const uint32_t flags = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outfile = argv[++i];
        else if (strcmp(argv[i], "--key-id") == 0 && i + 1 < argc)
            key_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--outform") == 0 && i + 1 < argc)
            outform = argv[++i];
    }

    if (key_id < 0)
        usage(argv[0]);

    rc = rpi_fw_crypto_get_private_key(flags, key_id, privkey, sizeof(privkey), &privkey_len);
    if (rc < 0) {
        fprintf(stderr, "Failed to get private key: %s\n", rpi_fw_crypto_strerror(rpi_fw_crypto_get_last_error()));
        return -1;
    }

    if (outform && strcmp(outform, "hex") == 0) {
        if (write_hex_output(outfile, privkey, privkey_len) < 0) {
            perror("Failed to write private key");
            return -1;
        }
    } else {
        if (write_binary_output(outfile, privkey, privkey_len) < 0) {
            perror("Failed to write private key");
            return -1;
        }
    }

    return 0;
}

static int cmd_genkey(int argc, char *argv[])
{
    const char *alg = NULL;
    int key_id = -1;
    int i;
    int rc;
    const uint32_t flags = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--key-id") == 0 && i + 1 < argc)
            key_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--alg") == 0 && i + 1 < argc)
            alg = argv[++i];
    }

    if (key_id < 0 || !alg)
        usage(argv[0]);

    if (strcmp(alg, "ec") != 0) {
        fprintf(stderr, "Unsupported algorithm: %s\n", alg);
        return -1;
    }

    rc = rpi_fw_crypto_gen_ecdsa_key(flags, (uint32_t)key_id);
    if (rc < 0) {
        fprintf(stderr, "Failed to generate key: %s\n", rpi_fw_crypto_strerror(rpi_fw_crypto_get_last_error()));
        return -1;
    }

    printf("Successfully generated ECDSA key in slot %d\n", key_id);
    return 0;
}

int main(int argc, char *argv[])
{
    int last_err = 0;
    int rc = -1;
    int num_keys;
    int status;
    uint32_t key_id;
    uint32_t key_status;

    if (argc < 2)
        usage(argv[0]);

    if (strcmp(argv[1], "get-num-otp-keys") == 0) {
        num_keys = rpi_fw_crypto_get_num_otp_keys();
        if (num_keys < 0)
            goto error;
        printf("Number of OTP keys: %d\n", num_keys);
        return 0;
    }

    if (strcmp(argv[1], "get-key-status") == 0) {
        if (argc != 3)
            usage(argv[0]);
        key_id = atoi(argv[2]);
        rc = rpi_fw_crypto_get_key_status(key_id, &key_status);
        if (rc < 0)
            goto error;
        printf("Key %u status: 0x%08x (%s)\n", key_id, key_status, rpi_fw_crypto_key_status_str(key_status));
        return 0;
    }

    if (strcmp(argv[1], "set-key-status") == 0) {
        if (argc < 4)
            usage(argv[0]);
        key_id = atoi(argv[2]);
        status = 0;
        if (parse_key_status_args(argc, argv, 3, &status) != 0) {
            fprintf(stderr, "Failed to parse key status arguments.\n");
            return -1;
        }
        rc = rpi_fw_crypto_set_key_status(key_id, status);
        if (rc < 0) {
            fprintf(stderr, "Failed to set key status: %s\n", rpi_fw_crypto_strerror(rc));
            goto error;
        }
        printf("Set key %u status to 0x%08x (%s)\n", key_id, status, rpi_fw_crypto_key_status_str(status));
        return 0;
    }

    if (strcmp(argv[1], "sign") == 0) {
        rc = cmd_sign(argc, argv);
        if (rc < 0)
            goto error;
        return 0;
    }

    if (strcmp(argv[1], "hmac") == 0) {
        rc = cmd_hmac(argc, argv);
        if (rc < 0)
            goto error;
        return 0;
    }

    if (strcmp(argv[1], "pubkey") == 0) {
        rc = cmd_pubkey(argc, argv);
        if (rc < 0)
            goto error;
        return 0;
    }

    if (strcmp(argv[1], "privkey") == 0) {
        rc = cmd_privkey(argc, argv);
        if (rc < 0)
            goto error;
        return 0;
    }

    if (strcmp(argv[1], "genkey") == 0) {
        rc = cmd_genkey(argc, argv);
        if (rc < 0)
            goto error;
        return 0;
    }

    usage(argv[0]);
    return -1;

error:
    // Check the firmware crypto error status. If set, display the human readable string.
    last_err = rpi_fw_crypto_get_last_error();
    if (last_err != RPI_FW_CRYPTO_SUCCESS)
        fprintf(stderr, "Last crypto error: %d (%s)\n", last_err, rpi_fw_crypto_strerror(last_err));
    return rc;
}
