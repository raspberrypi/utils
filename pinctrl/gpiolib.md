# gpiolib

## Introduction

gpiolib is a library for querying and controlling GPIOs via direct hardware access. The GPIOs available on the system are discovered from Device Tree.

The library is split into two parts:

1. a common core that maintains a list of the GPIO controllers ("gpiochips") in the system, as identified using the active Device Tree, and the pins they control, and
2. a number of hardware-specific drivers for those controllers, the internals of which are not documented.

There follows a description of the gpiolib API. Readers are encouraged to read pinctrl.c as a comprehensive usage example.

## GPIOs, pins, pinmuxing and chips

Pins are contacts on an integrated circuit such as a System-on-Chip (SoC) or other piece of hardware under its control. Pins are used for connections to other devices, either directly or brought out onto a header on the printed circuit board, where they are accessible to the user.

"GPIO" is a common name for a pin that can be configured for General Purpose Input or Output. A GPIO can be driven or queried under software control, without which it will continue in the current state indefinitely.

On most SoCs, GPIO is just one of several functions available on some of its pins. The other, _alternate_ functions assign the pin to the direct control of some other hardware block within the SoC - perhaps an I2C controller, a PWM, or an SD card interface. The process of selecting one function from several is called "pin multiplexing", often abbreviated to "pinmuxing".

A device such as the Raspberry Pi 5 has many GPIOs provided by a variety of different controllers, including on the dedicated I/O chip RP1. These GPIO controllers are commonly known as GPIO "chips", although a single SoC chip may contain several of them.

## Enter gpiolib

gpiolib gives access to SoC pins (mainly - there is also a driver for the firmware-controlled GPIO expander found on some Raspberry Pi devices). A pin's function can be queried and set. If the current function is GPIO, it can be made an input or an output, driven high or low, and the current level queried.

Unlike gpiod (or the old sysfs GPIO interface), gpiolib controls the hardware directly, bypassing the kernel. This can be useful for querying the state of a running system, or for making simple changes at runtime, but it comes at the risk of contention and confusion if the kernel is trying to make changes in the same area - you have been warnined.

Another advantage of gpiolib is that after initialisation it gives rapid access to GPIOs from userspace, avoiding the overhead of crossing the kernel boundary for every change. However, this requires that the application is run with enough privilege. On Raspberry Pi OS the main user is usually made a member of group "gpio", which may be sufficient, but at other times the use of "sudo" may be required.

## Initialisation

Initialisation happens in two stages:

#### `int gpiolib_init(void)`

Query the Device Tree to locate the GPIO controllers. Once completed, the API can be used to enumerate the GPIOs, their names, and any alternate functions they support. Each GPIO chip is allocated a space in a global number space. The gpio number range for a chip starts at a multiple of 100, with the first GPIO on the first chip being GPIO 0. GPIOs outside or between the allocated ranges are not accessible.

This step does not require acces to the hardware, and on many Operating Systems it can be performed at normal user privilege.

Returns the number of GPIOs in the system, including any gaps between multiple GPIO chips, i.e. 1 more than the highest valid GPIO number, or -1 on error.

#### `int gpiolib_mmap(void)`

In order to access the GPIO and pinmux hardware, the Memory Mapped Input/Output (MMIO) registers must be mapped into the address space of the application process, accomplished by the system call `mmap`. This requires a higher privilege level than normal - membership of a special user group such as `gpio` or temporary root access via `sudo`. For most use case `gpiolib_init` will be followed immediately by `gpiolib_mmap`.

Returns 0 on success and a non-zero error (positive `errno` values or -1).

#### `int gpiolib_init_by_name(const char *name)`

`gpiolib_init_by_name` is an alternative to `gpiolib_init` that only gives access to the list of functions supported by the named GPIO chip. It can be used to query the capabilities of, say, a Pi 3 while running on a Pi 5. It is the mechanism behind `pinctrl -c <chip>`.

Returns the number of GPIOs provided by the GPIO chip, or -1 on error.

## GPIOs and pins

#### `int gpio_num_is_valid(unsigned gpio)`

Returns 1 if `gpio` is the number of an allocated GPIO line in the global space, otherwise 0.

#### `void gpio_get_pin_range(unsigned *first, unsigned *last)`

Returns the numbers of the first and last board (40-pin header) pin via the passed-in pointers, or `GPIO_INVALID` if no pins are known about.

N.B. Some internal initialisation occurs as a side-effect of getting the pin range, so it is best to call it once before any of the `_pin` functions are used.

#### `unsigned gpio_for_pin(int pin)`

Returns the global GPIO number associated with the board-specific pin (currently the pins on 40-pin header, from 1 to 40). A number of special values may also be returned - `GPIO_GND`, `GPIO_5V`, `GPIO_3V3`, `GPIO_1V8`, `GPIO_OTHER`, or `GPIO_INVALID` if the pin does not exist.

N.B. Call `gpio_get_pin_range` at least once before using.

#### `int gpio_to_pin(unsigned gpio)`

Returns the board-specific pin (currently the pins on 40-pin header, from 1 to 40) associated with the global GPIO number, or `GPIO_INVALID` if none.

N.B. Call `gpio_get_pin_range` at least once before using.

## GPIO attributes

### Function

#### `GPIO_FSEL_T gpio_get_fsel(unsigned gpio)`

Returns the number of the function (the function _selector_) active on the specified `gpio`, or `GPIO_FSEL_MAX` on error.

#### `void gpio_set_fsel(unsigned gpio, const GPIO_FSEL_T func)`

Activates the chosen function on the given `gpio`. Does nothing on error. `GPIO_FSEL_GPIO` tries to activate the GPIO function without changing the existing direction, falling back to making it an input if not.

### Direction

#### `GPIO_DIR_T gpio_get_dir(unsigned gpio)`

Returns the direction - `DIR_INPUT` or `DIR_OUTPUT` - of the specified GPIO. Returns `DIR_MAX` in situations where it isn't possible to tell, e.g. where GPIO in and GPIO out are separate functions and neither is activated.

#### `void gpio_set_dir(unsigned gpio, GPIO_DIR_T dir)`

Sets the chosen direction for the given `gpio`. On some chips (where GPIO in and GPIO out are separate funcions) this will also enable GPIO mode. Does nothing on error.

### Drive

#### `GPIO_DRIVE_T gpio_get_drive(unsigned gpio)`

Returns the drive - `DRIVE_HIGH` or `DRIVE_LOW` - set for the given `gpio`. Returns `DRIVE_MAX` on error or if it is not possible to determine the drive.

#### `void gpio_set_drive(unsigned gpio, GPIO_DRIVE_T drv)`

Sets the chosen drive for the given `gpio`. Does nothing on error.

#### `void gpio_set(unsigned gpio)`

Makes `gpio` an output, driving high. Only activates the GPIO function on devices where GPIO in and out are separate functions, otherwise it may be necessary to use `gpio_set_fsel` first. Equivalent to `gpio_set_drive(gpio, DRIVE_HIGH)` followed by `gpio_set_dir(gpio, DIR_OUTPUT)`.

#### `void gpio_clear(unsigned gpio)`

Makes `gpio` an output, driving low. Only activates the GPIO function on devices where GPIO in and out are separate functions, otherwise it may be necessary to use `gpio_set_fsel` first. Equivalent to `gpio_set_drive(gpio, DRIVE_LOW)` followed by `gpio_set_dir(gpio, DIR_OUTPUT)`.

### Level

#### `int gpio_get_level(unsigned gpio)`  /* The actual level observed */

Returns the level observed at the `gpio` (1 or 0) or -1 if not known or on error. Note that on some chips the GPIO function may have to be selected in order for this to work.

### Pull

GPIO controllers usually have internal resistors that can be enabled to pull the pin high or low. These pulls are weak compared to a driven output or most external pull resistors, and serve to set default values for undriven pins (e.g. inputs).

#### `GPIO_PULL_T gpio_get_pull(unsigned gpio)`

Returns the configured pull directions (`PULL_UP`, `PULL_DOWN` or `PULL_NONE`) for the given `gpio`, otherwise `PULL_MAX` if it isn't possible to tell or on error. 

#### `void gpio_set_pull(unsigned gpio, GPIO_PULL_T pull)`

Sets a pull direction (`PULL_UP`, `PULL_DOWN` or `PULL_NONE`) for the given `gpio`. Does nothing on error. 

## Names

Each GPIO chip has names for its GPIOs - often just `GPIO<n>`, where `<n>` is the offset within that GPIO chip starting at 0. This is the "architectural name". Architectural names should exist but are not guaranteed to be unique.

Additionally, the Device Tree for the board can give GPIOs (hopefully) meaningful names. These _given_ names are optional but _should_ be unique.

gpiolib creates names for GPIOs by joining the given name and the architectural name, separated them with a `/` character. Absent names, or given names that duplicate the architectural names, are ignored (no separator is used). In the case that there is no valid name, `-` is used. 

#### `unsigned gpio_get_gpio_by_name(const char *name, int namelen)`

Returns the global GPIO number associated with the given `name`, or `GPIO_INVALID` if none. The supplied name must match either the given name or the architectural name in its entirety, but not a `/`-separated combination of both.

If non-zero, `namelen` specifies the length of the name, useful in the event that it is not NUL-terminated.

#### `const char *gpio_get_name(unsigned gpio)`

Returns the name associated with the given `gpio`, as described above.

#### `const char *gpio_get_gpio_fsel_name(unsigned gpio, GPIO_FSEL_T fsel)`

Returns a short name for the function available as the given `fsel` value on `gpio`, e.g. "TXD0" or "SD0_CMD", or NULL on error.

#### `const char *gpio_get_fsel_name(GPIO_FSEL_T fsel)`

Returns a short name for the given `fsel`, e.g. "op" or "a3", or NULL on error.

#### `const char *gpio_get_pull_name(GPIO_PULL_T pull)`

Returns a short name for the pull `pull`, e.g. "pd" or "pn", or NULL on error.

#### `const char *gpio_get_drive_name(GPIO_DRIVE_T drive)`

Returns a short name for the drive `drv`, e.g. "dh" or "dl", or NULL on error.

## Misc

#### `void gpiolib_set_verbose(void (*callback)(const char *))`

Pass in a function to be called to receive diagnostic output from gpiolib. This is currently just a list of the GPIO chips which are found, as enabled by `pinctrl -v`.
