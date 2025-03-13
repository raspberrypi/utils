#ifndef _EEPLIB_H
#define _EEPLIB_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <endian.h>

// minimal sizes of data structures
#define HEADER_SIZE 12
#define ATOM_SIZE 10
#define ATOM_HDR_SIZE 8
#define VENDOR_SIZE 22
#define GPIO_SIZE 30
#define GPIO_BANK1_SIZE 20
#define POWER_SUPPLY_SIZE 4
#define CRC_SIZE 2

#define GPIO_MIN 2
#define GPIO_COUNT 28
#define GPIO_COUNT_BANK1 18
#define GPIO_COUNT_TOTAL (GPIO_COUNT + GPIO_COUNT_BANK1)

#define CRC16 0x8005

// Signature is "R-Pi" in ASCII. It is required to reversed (little endian) on disk.
#define HEADER_SIGN (uint32_t)be32toh((((char)'R' << 24) | ((char)'-' << 16) | ((char)'P' << 8) | ((char)'i')))

#define count_of(x) ((sizeof(x) / sizeof(x[0])))
#define max(x, y) ((x) > (y) ? (x) : (y))

typedef void (*log_callback_t)(const char *, va_list);

/* Atom types */
enum atom_type_t
{
	ATOM_INVALID_TYPE = 0x0000,
	ATOM_VENDOR_TYPE = 0x0001,
	ATOM_GPIO_TYPE = 0x0002,
	ATOM_DT_TYPE = 0x0003,
	ATOM_CUSTOM_TYPE = 0x0004,
	ATOM_GPIO_BANK1_TYPE = 0x0005,
	ATOM_POWER_SUPPLY_TYPE = 0x0006,
	ATOM_HINVALID_TYPE = 0xffff
};

enum file_ver_t
{
	EEP_VERSION_HATV1 = 0x01,
	EEP_VERSION_HATPLUS = 0x02,

	EEP_VERSION = EEP_VERSION_HATPLUS,
};

enum eepio_dir_t
{
	EEPIO_READ,
	EEPIO_WRITE,
};

/* EEPROM header structure */
struct header_t
{
	uint32_t signature;
	unsigned char ver;
	unsigned char res;
	uint16_t numatoms;
	uint32_t eeplen;
};

/* Atom structure */
struct atom_t
{
	uint16_t type;
	uint16_t count;
	uint32_t dlen;
	char *data;
};

struct var_blob_t
{
	uint8_t *data;
	uint32_t dlen;
};

/* Vendor info atom data */
struct vendor_info_d
{
	uint32_t serial[4]; // 0 = least significant, 3 = most significant
	uint16_t pid;
	uint16_t pver;
	unsigned char vslen;
	unsigned char pslen;
	char *vstr;
	char *pstr;
};

/* GPIO map atom data */
struct gpio_map_d
{
	unsigned char flags;
	unsigned char power;
	unsigned char pins[GPIO_COUNT];
};

/* Power supply atom data */
struct power_supply_d
{
	uint32_t current_supply; /* In milliamps */
};

extern struct header_t eep_header;
extern struct atom_t eep_atom_header;
extern uint16_t eep_atom_crc;

void eepio_start(enum file_ver_t *pver, int *pnumatoms, enum eepio_dir_t dir, FILE *fp);
bool eepio_end(void);

bool eepio_atom_start(enum atom_type_t *ptype, uint32_t *pdlen);
bool eepio_atom_vinf(struct vendor_info_d *vinf);
bool eepio_atom_gpio(struct gpio_map_d *map);
bool eepio_atom_gpio_bank1(struct gpio_map_d *map);
bool eepio_atom_power_supply(struct power_supply_d *power);
bool eepio_atom_var(struct var_blob_t *var);
void eepio_atom_end(void);
void eepio_clear_error(void);
bool eepio_error(const char *msg, ...);
void eepio_warning(const char *msg, ...);
bool eepio_got_error(void);

void eepio_set_error_callback(log_callback_t callback);
void eepio_set_warning_callback(log_callback_t callback);

#endif
