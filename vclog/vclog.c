/*******************************************************************************
  Summary:
    Command line program for printing VideoCore log messages
    or assertion logs messages

  Licensing:
    Copyright (c) 2022-2023, Raspberry Pi Ltd. All rights reserved.
*******************************************************************************/
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum
{
    LOG_UNKNOWN,
    LOG_MSG,
    LOG_ASSERT,
};

typedef struct
{
    uint32_t padding[8];
    uint32_t assert_type;
    uint32_t assert_addr;
    uint32_t msg_type;
    uint32_t msg_addr;
    uint32_t task_type;
    uint32_t task_addr;
} log_toc_t;

typedef struct
{
    uint32_t id;
    uint32_t start;  // circular buffer start for this type of message
    uint32_t end;    // circular buffer end
    uint32_t write;  // where VC will write the next message
    uint32_t read;   // the oldest valid message
} log_hdr_t;

typedef struct
{
    uint32_t time;     // time log produced in millseconds
    uint16_t seq_num;  // if two entries have same timestamp then
                       // this seq num differentiates them
    uint16_t size;     // size of entire log entry = this header + payload
} msg_hdr_t;

struct fb_dmacopy
{
    void *dst;
    uint32_t src;
    uint32_t length;
};

enum
{
    LOG_ID = 0x564c4f47,
    MSG_ID = 0x5353454d,
    TASK_ID = 0x3b534154,
    ASSERT_ID = 0x54525341,
};

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))

#define FBIODMACOPY _IOW('z', 0x22, struct fb_dmacopy)

#ifdef DEBUG
#define DLOG(...) printf(__VA_ARGS__)
#else
#define DLOG(...) (void)0
#endif

static int log_type = LOG_UNKNOWN;
static int help = 0;
static int follow_mode = 0;

static const struct option long_options[] = {
    { "a", no_argument, &log_type, LOG_ASSERT },
    { "assert", no_argument, &log_type, LOG_ASSERT },
    { "m", no_argument, &log_type, LOG_MSG },
    { "msg", no_argument, &log_type, LOG_MSG },
    { "f", no_argument, &follow_mode, 1 },
    { "follow", no_argument, &follow_mode, 1 },
    { "h", no_argument, &help, 1 },
    { "help", no_argument, &help, 1 },
    { 0 }
};

static uint32_t vc_map_base;
static uint32_t vc_map_end;
static int dma_fd = -1;
static char *vc_map = MAP_FAILED;

static bool find_logs(uint32_t *logs_start, uint32_t *logs_size);
static bool prepare_vc_mapping(uint32_t vc_start, uint32_t vc_size);
static void destroy_vc_mapping(void);
static void read_vc_mem(uint32_t vc_addr, uint32_t size, void *dest);
static uint32_t log_copy_wrap(const char *log_buffer, uint32_t log_size,
                              uint32_t offset, uint32_t len, char *dest);
static void die(const char *msg);

int32_t main(int32_t argc, char *argv[])
{
    const struct timespec delay_10ms = {0, 10000000};
    const struct timespec delay_1ms = {0, 1000000};
    uint32_t logs_start_vc = 0;
    uint32_t logs_size = 0;
    char *payload_buffer = NULL;
    uint32_t payload_buffer_size = 0;
    uint32_t log_id;
    uint32_t log_addr;
    uint32_t log_size;
    char *log_buffer;
    log_toc_t toc;
    log_hdr_t log_hdr;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t next_pos = 0;
    int corrupt_retries = 0;

    while (getopt_long_only(argc, argv, "", long_options, NULL) != -1)
        continue;
    
    if (help || log_type == LOG_UNKNOWN)
    {
        fprintf(stderr,
                "Usage:\n\t%s [-f] <-m|-a>\n\t%s [--follow] <--msg|--assert>\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    
    // find the address and size of the logs in VC memory
    if (!find_logs(&logs_start_vc, &logs_size))
        die("Could not determine logs location from Device Tree");

    if (!prepare_vc_mapping(logs_start_vc, logs_size))
        die("Cannot access logs");

    read_vc_mem(logs_start_vc, sizeof(toc), &toc);

    switch (log_type)
    {
    case LOG_ASSERT:
        log_addr = toc.assert_addr;
        log_id = ASSERT_ID;
        break;
    case LOG_MSG:
        log_addr = toc.msg_addr;
        log_id = MSG_ID;
        break;
    default:
        die("Invalid type");
    }

    read_vc_mem(log_addr, sizeof(log_hdr), &log_hdr);

    if (log_hdr.id != log_id)
        die("Log ID incorrect");
    log_size = log_hdr.end - log_hdr.start;
    log_buffer = malloc(log_size);
    if (!log_buffer)
    {
        fprintf(stderr, "Failed to allocate buffer for VC msgs\n");
        goto cleanup;
    }

    read_vc_mem(log_hdr.start, log_size, log_buffer);
    read_pos = log_hdr.read - log_hdr.start;
    write_pos = log_hdr.write - log_hdr.start;
    if (read_pos >= log_size || write_pos >= log_size)
        die("Log pointers out of range");

    while (1)
    {
        uint32_t new_write_pos;
        int new_data;

        // Print logs between read_pos and write_pos
        while (read_pos != write_pos)
        {
            msg_hdr_t msg;
            uint32_t payload_pos;
            uint32_t payload_len;

            msg.size = 0;
            payload_pos = log_copy_wrap(log_buffer, log_size,
                                        read_pos, sizeof(msg), (char *)&msg);
            payload_len = msg.size - sizeof(msg_hdr_t);
            if (payload_len > log_size)
            {
                DLOG("log corrupt 1\n");
                corrupt_retries++;
                break;
            }

            if (payload_len > payload_buffer_size)
            {
                payload_buffer_size = MAX(payload_len, 100); // skip some churn
                payload_buffer = realloc(payload_buffer, payload_buffer_size);
                if (!payload_buffer)
                    die("Out of memory");
            }

            next_pos = log_copy_wrap(log_buffer, log_size,
                                     payload_pos, payload_len, payload_buffer);
            
            // Check read_pos hasn't overtaken write_pos
            if (next_pos > read_pos)
            {
                // Normal
                if (read_pos < write_pos && next_pos > write_pos)
                {
                    DLOG("log corrupt 2\n");
                    corrupt_retries++;
                    break;
                }
            }
            else
            {
                // This message wrapped
                if (read_pos < write_pos ||
                    next_pos > write_pos)
                {
                    DLOG("log corrupt 3\n");
                    corrupt_retries++;
                    break;
                }
            }

            if (log_type == LOG_MSG)
            {
                DLOG("%08x: ", read_pos);
                printf("%06i.%03i: %.*s\n", msg.time / 1000,
                       msg.time % 1000, payload_len - 4, payload_buffer + 4);
            }
            else if (log_type == LOG_ASSERT)
            {
                size_t filename_len = strnlen(payload_buffer, payload_len);
                uint32_t cond_offset;
                uint32_t line_number;

                cond_offset = filename_len +1 + 4;
                if ((cond_offset + 1) >= payload_len)
                {
                    DLOG("log corrupt 4\n");
                    corrupt_retries++;
                    break;
                }

                memcpy(&line_number, payload_buffer + filename_len + 1, 4);

                printf("%06i.%03i: assert( %.*s ) failed; %s line %d\n",
                       msg.time / 1000, msg.time % 1000,
                       payload_len = cond_offset, payload_buffer + cond_offset,
                       payload_buffer, line_number);
                printf("----------------\n");
            }

            corrupt_retries = 0;
            read_pos = next_pos;
        }

        if (corrupt_retries)
        {
            // Apparent corruption is likely to be due to the effects of caches.
            // Rewind to re-copy the affected area a number of times before
            // giving up.
            if (corrupt_retries == 10)
            {
                DLOG("==== read %x, write %x, next %x, size %x ====\n",
                     read_pos, write_pos, next_pos, log_size);
                die("Log corrupt");
            }

            write_pos = read_pos;
            nanosleep(&delay_10ms, NULL);
        }

        if (!follow_mode)
            break;

        while (1)
        {
            // Refresh view of the log pointers
            read_vc_mem(log_addr, sizeof(log_hdr), &log_hdr);
            new_write_pos = log_hdr.write - log_hdr.start;
            if (new_write_pos != write_pos)
                break;
            nanosleep(&delay_1ms, NULL);
        }

        // More messages added
        new_data = new_write_pos - write_pos;
        if (new_data > 0)
        {
            read_vc_mem(log_hdr.start + write_pos, new_data, log_buffer + write_pos);
        }
        else
        {
            // The new data wraps around the end of the buffer
            read_vc_mem(log_hdr.start + write_pos, log_size - write_pos, log_buffer + write_pos);
            read_vc_mem(log_hdr.start, new_write_pos, log_buffer);
        }

        write_pos = new_write_pos;
    }

    free(payload_buffer);
    free(log_buffer);

cleanup:
    destroy_vc_mapping();
    return EXIT_SUCCESS;
}

static bool find_logs(uint32_t *logs_start, uint32_t *logs_size)
{
    const char *const filename = "/proc/device-tree/chosen/log";
    uint32_t vals[2];
    FILE *fp;
    bool ret = false;

    if (!logs_start || !logs_size)
        goto exit;

    // VideoCore logs start and size can be found in the Device Tree
    fp = fopen(filename, "rb");
    if (!fp)
        goto exit;

    if (fread(vals, sizeof(vals), 1, fp) != 1)
        goto cleanup;

    // Device Tree is stored in network order, i.e. big-endian
    *logs_start = ntohl(vals[0]);
    *logs_size = ntohl(vals[1]);

    ret = true;

cleanup:
    fclose(fp);
exit:
    return ret;
}

static bool prepare_vc_mapping(uint32_t vc_start, uint32_t vc_size)
{
    const char *dma_filenames[] = { "/dev/vc-mem", "/dev/fb0" };
    const char *mem_filename = "/dev/mem";
    struct fb_dmacopy ioparam;
    uint32_t id;
    int err, fd, i;

    ioparam.dst = &id;
    ioparam.src = vc_start;
    ioparam.length = sizeof(id);

    for (i = 0; i < (int)ARRAY_SIZE(dma_filenames); i++)
    {
        if ((fd = open(dma_filenames[i], O_RDWR | O_SYNC)) >= 0)
        {
            err = ioctl(fd, FBIODMACOPY, &ioparam);
            if (err == 0 && id == LOG_ID)
            {
                dma_fd = fd;
                goto success;
            }
            close(fd);
        }
    }

    if ((fd = open(mem_filename, O_RDONLY)) >= 0)
    {
        long page_size = sysconf(_SC_PAGE_SIZE);

        /* find start and end addresses aligned down and up to pagesize respectively */
        off_t mmap_start = (uintptr_t)vc_start & ~(page_size - 1);
        off_t mmap_end = ((uintptr_t)vc_start + vc_size + page_size - 1) & ~(page_size -1);

        vc_map = mmap(NULL, mmap_end - mmap_start, PROT_READ, MAP_PRIVATE,
                      fd, (uintptr_t)mmap_start);
        close(fd);
        if (vc_map != MAP_FAILED)
        {
            id = *(uint32_t *)(vc_map + vc_start - mmap_start);
            if (id == LOG_ID)
            {
                vc_start = mmap_start;
                vc_size = mmap_end - mmap_start;
                goto success;
            }
        }
    }

    fprintf(stderr, "Could not map VC memory: %s\n", strerror(errno));

    return false;

success:
    vc_map_base = vc_start;
    vc_map_end = vc_start + vc_size;

    return true;
}

static void destroy_vc_mapping(void)
{
    if (dma_fd != -1)
    {
        close(dma_fd);
        dma_fd = -1;
    }
    if (vc_map != MAP_FAILED)
    {
        munmap(vc_map, vc_map_end - vc_map_base);
        vc_map = MAP_FAILED;
    }
}


/********************************************************************************/
/* Note: gcc with -O2 or higher may replace most of this code with memcpy       */
/* which causes a bus error when given an insufficiently aligned mmap-ed buffer */
/* Using volatile disables that optimisation                                    */
/********************************************************************************/
static void memcpy_vc_memory(void *restrict dest, const volatile void *restrict src,
                             size_t n)
{
    if ((((uintptr_t)dest | (uintptr_t)src | n) & 3) == 0)
    {
        uint32_t *restrict d = (uint32_t *restrict )dest;
        const volatile uint32_t *restrict s = (const volatile uint32_t *restrict)src;
        while (n)
            *d++ = *s++, n -= 4;
    }
    else
    {
        uint8_t *restrict d = (uint8_t *restrict )dest;
        const volatile uint8_t *restrict s = (const volatile uint8_t *restrict)src;
        while (n--)
            *d++ = *s++;
    }
}

static void read_vc_mem(uint32_t vc_addr, uint32_t size, void *dest)
{
    vc_addr &= 0x3fffffff;

    if (vc_addr < vc_map_base || vc_addr + size > vc_map_end)
        die("VC access out-of-bounds");

    if (dma_fd != -1)
    {
        struct fb_dmacopy ioparam;
        ioparam.dst = dest;
        ioparam.src = vc_addr;
        ioparam.length = size;
        if (ioctl(dma_fd, FBIODMACOPY, &ioparam) != 0)
            die("Failed to copy VC memory");
    }
    else
    {
        memcpy_vc_memory(dest, vc_map + vc_addr - vc_map_base, size);
    }
}

static uint32_t log_copy_wrap(const char *log_buffer, uint32_t log_size,
                              uint32_t offset, uint32_t len, char *dest)
{
    if (offset >= log_size)
        return ~0;

    if (offset + len < log_size)
    {
        memcpy(dest, log_buffer + offset, len);
        return offset + len;
    }
    else
    {
        uint32_t first_chunk = log_size - offset;
        memcpy(dest, log_buffer + offset, first_chunk);
        len -= first_chunk;
        if (len)
            memcpy(dest + first_chunk, log_buffer, len);
        return len;
    }
}

static void die(const char *msg)
{
    fprintf(stderr, "Fatal error: %s\n", msg);
    exit(EXIT_FAILURE);
}
