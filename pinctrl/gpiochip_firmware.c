#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gpiochip.h"
#include "util.h"

#define DEVICE_FILE_NAME        "/dev/vcio"
#define MAJOR_NUM               100

#define NUM_GPIOS               8
#define RPI_EXP_GPIO_BASE       128

#define IOCTL_MBOX_PROPERTY     _IOWR(MAJOR_NUM, 0, char *)

#define RPI_FIRMWARE_STATUS_REQUEST  0
#define RPI_FIRMWARE_STATUS_SUCCESS  0x80000000
#define RPI_FIRMWARE_STATUS_ERROR    0x80000001

#define RPI_FIRMWARE_PROPERTY_END    0
#define RPI_FIRMWARE_GET_GPIO_STATE  0x00030041
#define RPI_FIRMWARE_SET_GPIO_STATE  0x00038041
#define RPI_FIRMWARE_GET_GPIO_CONFIG 0x00030043
#define RPI_FIRMWARE_SET_GPIO_CONFIG 0x00038043

struct firmware_inst
{
    unsigned num_gpios;
    int mbox_fd;
};

struct gpio_config
{
    uint32_t direction;
    uint32_t polarity;
    uint32_t term_en;
    uint32_t term_pull_up;
    uint32_t drive;
};

struct gpio_get_set_config
{
    uint32_t gpio;
    struct gpio_config config;
};

struct gpio_get_set_state
{
    uint32_t gpio;
    uint32_t state;
};

static struct firmware_inst firmware_instance;

static int firmware_property(struct firmware_inst *inst, uint32_t tag, void *tag_data, int tag_size)
{
    uint32_t buf[16];
    int words = 5 + (tag_size + 3) / 4 + 1;
    int err;

    if (words > (int)ARRAY_SIZE(buf))
        return -1;
    if (!inst->mbox_fd)
        inst->mbox_fd = open(DEVICE_FILE_NAME, 0);
    if (inst->mbox_fd < 0)
        return -1;

    buf[0] = words * sizeof(buf[0]);
    buf[1] = RPI_FIRMWARE_STATUS_REQUEST; // process request
    buf[2] = tag;
    buf[3] = tag_size;
    buf[4] = tag_size; // set to response length
    memcpy(&buf[5], tag_data, tag_size);
    buf[words - 1] = RPI_FIRMWARE_PROPERTY_END;
    err = ioctl(inst->mbox_fd, IOCTL_MBOX_PROPERTY, buf);
    if (!err)
    {
        if (buf[4] & RPI_FIRMWARE_STATUS_SUCCESS)
            memcpy(tag_data, &buf[5], buf[4] & ~RPI_FIRMWARE_STATUS_SUCCESS);
        else
            err = -EREMOTEIO;
    }
    return err;
}

static int firmware_get_gpio_state(struct firmware_inst *inst, unsigned gpio)
{
    struct gpio_get_set_state prop;
    prop.gpio = RPI_EXP_GPIO_BASE + gpio;
    if (firmware_property(inst, RPI_FIRMWARE_GET_GPIO_STATE, &prop, sizeof(prop)))
        return -1;
    return prop.state;
}

static int firmware_set_gpio_state(struct firmware_inst *inst, unsigned gpio, unsigned state)
{
    struct gpio_get_set_state prop;
    prop.gpio = RPI_EXP_GPIO_BASE + gpio;
    prop.state = state;
    return firmware_property(inst, RPI_FIRMWARE_SET_GPIO_STATE, &prop, sizeof(prop));
}

static int firmware_get_gpio_config(struct firmware_inst *inst, int gpio, struct gpio_config *config)
{
    struct gpio_get_set_config prop;
    prop.gpio = RPI_EXP_GPIO_BASE + gpio;
    prop.config.drive = ~0;
    if (firmware_property(inst, RPI_FIRMWARE_GET_GPIO_CONFIG, &prop, sizeof(prop)) < 0)
        return -1;
    if (prop.config.drive == ~0u)
        prop.config.drive = firmware_get_gpio_state(inst, gpio);
    *config = prop.config;
    return 0;
}

static int firmware_set_gpio_config(struct firmware_inst *inst, int gpio, const struct gpio_config *config)
{
    struct gpio_get_set_config prop;
    prop.gpio = RPI_EXP_GPIO_BASE + gpio;
    prop.config = *config;
    return firmware_property(inst, RPI_FIRMWARE_SET_GPIO_CONFIG, &prop, sizeof(prop));
}

static GPIO_DIR_T firmware_gpio_get_dir(void *priv, unsigned gpio)
{
    struct firmware_inst *inst = priv;
    struct gpio_config config;
    if (gpio < inst->num_gpios &&
        !firmware_get_gpio_config(inst, gpio, &config))
        return (config.direction == 1) ? DIR_OUTPUT : DIR_INPUT;
    return DIR_MAX;
}

static void firmware_gpio_set_dir(void *priv, unsigned gpio, GPIO_DIR_T dir)
{
    struct firmware_inst *inst = priv;
    struct gpio_config config;

    if (gpio >= inst->num_gpios)
        return;

    if (!firmware_get_gpio_config(inst, gpio, &config))
    {
        if (dir != config.direction)
        {
            config.direction = dir;
            firmware_set_gpio_config(inst, gpio, &config);
        }
    }
}

static GPIO_FSEL_T firmware_gpio_get_fsel(void *priv, unsigned gpio)
{
    GPIO_DIR_T dir = firmware_gpio_get_dir(priv, gpio);
    if (dir == DIR_INPUT)
        return GPIO_FSEL_INPUT;
    else if (dir == DIR_OUTPUT)
        return GPIO_FSEL_OUTPUT;
    else
        return GPIO_FSEL_MAX;
}

static void firmware_gpio_set_fsel(void *priv, unsigned gpio, const GPIO_FSEL_T func)
{
    GPIO_DIR_T dir;

    switch (func)
    {
    case GPIO_FSEL_INPUT: dir = DIR_INPUT; break;
    case GPIO_FSEL_OUTPUT: dir = DIR_OUTPUT; break;
    default:
        return;
    }

    firmware_gpio_set_dir(priv, gpio, dir);
}

static int firmware_gpio_get_level(void *priv, unsigned gpio)
{
    struct firmware_inst *inst = priv;

    if (gpio >= inst->num_gpios)
        return -1;

    return firmware_get_gpio_state(inst, gpio);
}

GPIO_DRIVE_T firmware_gpio_get_drive(void *priv, unsigned gpio)
{
    struct firmware_inst *inst = priv;
    struct gpio_config config;
    if (!firmware_get_gpio_config(inst, gpio, &config))
    {
        if (config.direction == 1)
            return config.drive ? DRIVE_HIGH : DRIVE_LOW;
    }
    return DRIVE_MAX;
}

static void firmware_gpio_set_drive(void *priv, unsigned gpio, GPIO_DRIVE_T drv)
{
    struct firmware_inst *inst = priv;

    if (gpio >= inst->num_gpios)
        return;

    firmware_set_gpio_state(inst, gpio, drv == DRIVE_HIGH);
}

static GPIO_PULL_T firmware_gpio_get_pull(void *priv, unsigned gpio)
{
    struct firmware_inst *inst = priv;
    struct gpio_config config;
    if (!firmware_get_gpio_config(inst, gpio, &config))
    {
        if (!config.term_en)
            return PULL_NONE;
        return config.term_pull_up ? PULL_UP : PULL_DOWN;
    }
    return PULL_MAX;
}

static void firmware_gpio_set_pull(void *priv, unsigned gpio, GPIO_PULL_T pull)
{
    struct firmware_inst *inst = priv;
    struct gpio_config config;
    uint32_t term_en, term_pull_up;

    switch (pull)
    {
    case PULL_NONE:
        term_en = 0;
        term_pull_up = 0;
        break;
    case PULL_DOWN:
        term_en = 1;
        term_pull_up = 0;
        break;
    case PULL_UP:
        term_en = 1;
        term_pull_up = 1;
        break;
    default:
        return;
    }

    if (!firmware_get_gpio_config(inst, gpio, &config))
    {
        if (term_en != config.term_en ||
            term_pull_up != config.term_pull_up)
        {
            config.term_en = term_en;
            config.term_pull_up = term_pull_up;
            firmware_set_gpio_config(inst, gpio, &config);
        }
    }
}

static const char *firmware_gpio_get_name(void *priv, unsigned gpio)
{
    struct firmware_inst *inst = priv;
    static char name_buf[16];
    if (gpio >= inst->num_gpios)
        return NULL;
    sprintf(name_buf, "FWGPIO%d", gpio);
    return name_buf;
}

static const char *firmware_gpio_get_fsel_name(void *priv, unsigned gpio, GPIO_FSEL_T fsel)
{
    UNUSED(priv);
    UNUSED(gpio);
    switch (fsel)
    {
    case GPIO_FSEL_INPUT:
        return "input";
    case GPIO_FSEL_OUTPUT:
        return "output";
    default:
        return NULL;
    }
}

static void *firmware_gpio_create_instance(const GPIO_CHIP_T *chip,
                                           const char *dtnode)
{
    UNUSED(chip);
    UNUSED(dtnode);
    firmware_instance.num_gpios = NUM_GPIOS;
    firmware_instance.mbox_fd = 0;
    return &firmware_instance;
}

static int firmware_gpio_count(void *priv)
{
    struct firmware_inst *inst = priv;
    return inst->num_gpios;
}

static const GPIO_CHIP_INTERFACE_T firmware_gpio_interface =
{
    .gpio_create_instance = firmware_gpio_create_instance,
    .gpio_count = firmware_gpio_count,
    .gpio_get_fsel = firmware_gpio_get_fsel,
    .gpio_set_fsel = firmware_gpio_set_fsel,
    .gpio_set_drive = firmware_gpio_set_drive,
    .gpio_set_dir = firmware_gpio_set_dir,
    .gpio_get_dir = firmware_gpio_get_dir,
    .gpio_get_level = firmware_gpio_get_level,
    .gpio_get_drive = firmware_gpio_get_drive,
    .gpio_get_pull = firmware_gpio_get_pull,
    .gpio_set_pull = firmware_gpio_set_pull,
    .gpio_get_name = firmware_gpio_get_name,
    .gpio_get_fsel_name = firmware_gpio_get_fsel_name,
};

DECLARE_GPIO_CHIP(firmware, "raspberrypi,firmware-gpio", &firmware_gpio_interface,
                  0, 0);
