#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pio_platform.h"
#include "rp1_pio_if.h"

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#if 0
static void dump_words(const uint32_t *buf, int words, uint32_t addr)
{
    int i = 0;

    while (i < words)
    {
        int block = min(words - i, 4);
        int j;
        printf("%08x:", addr + i * 4);
        for (j = 0; j < block; j++)
            printf(" %08x", buf[i++]);
        printf("\n");
    }
}
#endif

static void usage(void)
{
    printf("Usage: rp1sm <sm>\n");
    exit(1);
}

int main(int argc, const char **argv)
{
    const char *devname = "/dev/pio0";
    struct rp1_access_hw_args args;
    uint32_t databuf[64];
    uint sm;
    int ret;
    int fd;

    if (argc < 2)
        usage();

    fd = open(devname, O_RDWR, O_CLOEXEC);
    if (fd < 0)
        return -errno;

    sm = (uint)strtoul(argv[1], NULL, 16);

    args.addr = 0xf0000000;
    args.len = 0x20;
    args.data = databuf;

    ret = ioctl(fd, PIO_IOC_READ_HW, &args);
    if (ret < 0)
        return ret;

    uint ctrl = databuf[0];
    uint fstat = databuf[1];
    uint flevel = databuf[3];
    uint flevel2 = databuf[4];

    args.addr = 0xf00000cc + sm * 0x20;
    args.len = 0x20;
    args.data = databuf;

    ret = ioctl(fd, PIO_IOC_READ_HW, &args);
    if (ret < 0)
        return ret;

    uint clkdiv = databuf[0];
    uint execctrl = databuf[1];
    uint shiftctrl = databuf[2];
    uint pc = databuf[3];
    uint instr = databuf[4];
    uint pinctrl = databuf[5];
    uint dmactrl_tx = databuf[6];
    uint dmactrl_rx = databuf[7];

    printf("enabled: %d\n", (ctrl >> sm) & 1);
    printf("clkdiv: %08x\n", clkdiv);
    printf("execctrl: %08x\n", execctrl);
    printf("shiftctrl: %08x\n", shiftctrl);
    printf("pc: %08x\n", pc);
    printf("instr: %08x\n", instr);
    printf("pinctrl: %08x\n", pinctrl);
    printf("dmactrl_tx: %08x\n", dmactrl_tx);
    printf("dmactrl_rx: %08x\n", dmactrl_rx);

    printf("TX fifo: level %x, flags %c%c\n",
           ((flevel >> (sm * 8)) & 0xf) + (((flevel2 >> (sm * 8)) & 1) << 4),
           (fstat & (0x10000 << sm)) ? 'F' : 'f', 
           (fstat & (0x1000000 << sm)) ? 'E' : 'e');

    printf("RX fifo: level %x, flags %c%c\n",
           ((flevel >> (sm * 8 + 4)) & 0xf) + (((flevel2 >> (sm * 8 + 4)) & 1) << 4),
           (fstat & (0x1 << sm)) ? 'F' : 'f',
           (fstat & (0x100 << sm)) ? 'E' : 'e');

    close(fd);

    return 0;
}
