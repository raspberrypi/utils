/*
 *	Parses EEPROM text file and createds binary .eep file
 *	Usage: eepmake input_file output_file
 */

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>

#include "eeplib.h"

static struct vendor_info_d vinf;
static struct var_blob_t dt_blob;
struct var_blob_t *custom_blobs;
static struct var_blob_t *data_blob;
bool in_string;

/* Common features */
static bool product_serial_set, product_id_set, product_ver_set, vendor_set, product_set;
static bool has_dt;

/* Legacy V1 features */
static struct gpio_map_d gpiomap_bank0, gpiomap_bank1;
static bool has_gpio_bank0, has_gpio_bank1;
static bool gpio_drive_set, gpio_slew_set, gpio_hysteresis_set, gpio_power_set,
	bank1_gpio_drive_set, bank1_gpio_slew_set, bank1_gpio_hysteresis_set;

/* HAT+ features */
static struct power_supply_d power_supply;
static bool current_supply_set, has_power_supply;

static unsigned int custom_ct, data_cap, custom_cap;

static enum file_ver_t hat_format = EEP_VERSION_HATPLUS;

static void fatal_error(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	printf("FATAL: ");
	vprintf(msg, ap);
	printf("\n");
	exit(1);
}

static void hatplus_required(const char *cmd)
{
	if (hat_format >= EEP_VERSION_HATPLUS)
		return;
	printf("'%s' not supported on V1 HAT\n", cmd);
	exit(1);
}

static void hatplus_unsupported(const char *cmd)
{
	if (hat_format == EEP_VERSION_HATV1)
		return;
	printf("'%s' not supported on HAT+\n", cmd);
	exit(1);
}

static int write_binary(const char *out)
{
	FILE *fp;
	unsigned int i;
	enum eepio_dir_t dir = EEPIO_WRITE;
	enum atom_type_t type;

	fp = fopen(out, "wb");
	if (!fp)
	{
		printf("Error writing file %s\n", out);
		return -1;
	}

	eepio_start(&hat_format, NULL, dir, fp);

	type = ATOM_VENDOR_TYPE;
	eepio_atom_start(&type, NULL);
	eepio_atom_vinf(&vinf);
	eepio_atom_end();

	if (has_gpio_bank0)
	{
		type = ATOM_GPIO_TYPE;
		eepio_atom_start(&type, NULL);
		eepio_atom_gpio(&gpiomap_bank0);
		eepio_atom_end();
	}

	if (has_dt)
	{
		printf("Writing out DT...\n");
		type = ATOM_DT_TYPE;
		eepio_atom_start(&type, NULL);
		eepio_atom_var(&dt_blob);
		eepio_atom_end();
	}

	for (i = 0; i < custom_ct; i++)
	{
		type = ATOM_CUSTOM_TYPE;
		eepio_atom_start(&type, NULL);
		eepio_atom_var(&custom_blobs[i]);
		eepio_atom_end();
	}

	if (has_gpio_bank1)
	{
		type = ATOM_GPIO_BANK1_TYPE;
		eepio_atom_start(&type, NULL);
		eepio_atom_gpio_bank1(&gpiomap_bank1);
		eepio_atom_end();
	}

	if (has_power_supply)
	{
		type = ATOM_POWER_SUPPLY_TYPE;
		eepio_atom_start(&type, NULL);
		eepio_atom_power_supply(&power_supply);
		eepio_atom_end();
	}

	eepio_end();

	fclose(fp);
	return 0;
}

static void add_data_byte(int byte)
{
	if (data_blob->dlen >= data_cap)
	{
		data_cap = max(data_cap * 2, 64);
		data_blob->data = realloc(data_blob->data, data_cap);
	}

	data_blob->data[data_blob->dlen++] = byte;
}

static void finish_data(void)
{
	if (data_blob)
	{
		data_blob->data = realloc(data_blob->data, data_blob->dlen);
		data_blob = NULL;
		data_cap = 0;
	}
}

static int parse_string(const char *p)
{
	/* Read string data, stopping at escaped-doublequote */
	int c;

	while ((c = *(p++)) != 0)
	{
		if (c == '\\')
		{
			int c2 = *(p++);
			if (c2 == 'r')
				c = '\r';
			else if (c2 == '\\')
				c = '\\'; // Technically a no-op
			else if (c2 == '0')
			{
				add_data_byte(0);
				break; // Ignore the rest of the line
			}
			else if (c2 == '"')
			{
				in_string = false;
				finish_data();
				break;
			}
			else
				fatal_error("Bad escape sequence '\\%c'", c2);
		}
		else if (c == '\r') // Real CRs are escaped, so ignore this as MS droppings
			continue;

		add_data_byte(c);
	}

	return 0;
}

static int parse_data(char *c)
{
	char *i = c;
	char *j = c;
	int len;
	int k;

	while (isspace(*j))
		j++;

	if (j[0] == '"')
	{
		if (j[1] == '\n' || j[1] == '\r')
		{
			// Ignore anything else
			in_string = true;
			return 0;
		}

		// Simple in-line string
		while (1)
		{
			int byte = *(++j);
			if (byte == '"')
			{
				finish_data();
				return 0;
			}
			if (!isprint(byte))
				fatal_error("Bad character 0x%02x in simple string '%s'", byte, c);
			add_data_byte(byte);
		}
		return 0;
	}

	/* Filter out the non-hex-digits */
	while (*j != '\0')
	{
		*i = *j++;
		if (isxdigit(*i))
			i++;
	}
	*i = '\0';

	len = strlen(c);
	if (len % 2 != 0)
	{
		printf("Error: data must have an even number of hex digits\n");
		return -1;
	}
	else
	{
		for (k = 0; k < len / 2; k++)
		{
			int byte;
			sscanf(c, "%2x", &byte);
			add_data_byte(byte);
			c += 2;
		}
	}

	return 0;
}

static void init_blob(struct var_blob_t *blob)
{
	blob->data = NULL;
	blob->dlen = 0;
}

struct var_blob_t *add_custom_blob(void)
{
	struct var_blob_t *blob;

	if (custom_cap == custom_ct)
	{
		custom_cap = max(custom_cap * 2, 1);
		custom_blobs = realloc(custom_blobs, custom_cap * sizeof(struct var_blob_t));
	}

	blob = &custom_blobs[custom_ct++];
	init_blob(blob);
	return blob;
}

static int parse_command(char *cmd, char *c)
{
	int val;
	uint32_t high1, high2;
	char *fn, *pull;
	char pin;
	bool valid;

	/* Vendor info related part */
	if (strcmp(cmd, "product_uuid") == 0)
	{
		product_serial_set = true; // required field
		high1 = 0;
		high2 = 0;

		sscanf(c, "%100s %08x-%04x-%04x-%04x-%04x%08x\n", cmd,
			   &vinf.serial[3],
			   &high1, &vinf.serial[2],
			   &high2, &vinf.serial[1],
			   &vinf.serial[0]);

		vinf.serial[2] |= high1 << 16;
		vinf.serial[1] |= high2 << 16;

		if ((vinf.serial[3] == 0) && (vinf.serial[2] == 0) &&
			(vinf.serial[1] == 0) && (vinf.serial[0] == 0))
		{
			// read 128 random bits from /dev/urandom
			int random_file = open("/dev/urandom", O_RDONLY);
			ssize_t result = read(random_file, vinf.serial, 16);
			close(random_file);
			if (result <= 0)
			{
				printf("Unable to read from /dev/urandom to set up UUID");
				return -1;
			}
			else
			{
				// put in the version
				vinf.serial[2] = (vinf.serial[2] & 0xffff0fff) | 0x00004000;

				// put in the variant
				vinf.serial[1] = (vinf.serial[1] & 0x3fffffff) | 0x80000000;

				printf("UUID=%08x-%04x-%04x-%04x-%04x%08x\n",
					   vinf.serial[3],
					   vinf.serial[2] >> 16, vinf.serial[2] & 0xffff,
					   vinf.serial[1] >> 16, vinf.serial[1] & 0xffff,
					   vinf.serial[0]);
			}
		}
	}
	else if (strcmp(cmd, "product_id") == 0)
	{
		product_id_set = true; // required field
		sscanf(c, "%100s %hx", cmd, &vinf.pid);
	}
	else if (strcmp(cmd, "product_ver") == 0)
	{
		product_ver_set = true; // required field
		sscanf(c, "%100s %hx", cmd, &vinf.pver);
	}
	else if (strcmp(cmd, "vendor") == 0)
	{
		vendor_set = true; // required field
		vinf.vstr = malloc(256);
		sscanf(c, "%100s \"%255[^\"]\"", cmd, vinf.vstr);
		vinf.vslen = strlen(vinf.vstr);
	}
	else if (strcmp(cmd, "product") == 0)
	{
		product_set = true; // required field
		vinf.pstr = malloc(256);
		sscanf(c, "%100s \"%255[^\"]\"", cmd, vinf.pstr);
		vinf.pslen = strlen(vinf.pstr);
	}

	/* HAT+ features */
	else if (strcmp(cmd, "current_supply") == 0)
	{
		hatplus_required(cmd);
		sscanf(c, "%100s %u", cmd, &power_supply.current_supply);
		if (power_supply.current_supply)
		{
			current_supply_set = true;
			has_power_supply = true;
		}
	}

	/* GPIO map related part */
	else if (strcmp(cmd, "gpio_drive") == 0)
	{
		hatplus_unsupported(cmd);
		gpio_drive_set = true; // required field
		has_gpio_bank0 = true;

		sscanf(c, "%100s %1x", cmd, &val);
		if (val > 8 || val < 0)
			printf("Warning: gpio_drive property in invalid region, using default value instead\n");
		else
			gpiomap_bank0.flags |= val;
	}
	else if (strcmp(cmd, "gpio_slew") == 0)
	{
		hatplus_unsupported(cmd);
		gpio_slew_set = true; // required field
		has_gpio_bank0 = true;

		sscanf(c, "%100s %1x", cmd, &val);

		if (val > 2 || val < 0)
			printf("Warning: gpio_slew property in invalid region, using default value instead\n");
		else
			gpiomap_bank0.flags |= val << 4;
	}
	else if (strcmp(cmd, "gpio_hysteresis") == 0)
	{
		hatplus_unsupported(cmd);
		gpio_hysteresis_set = true; // required field
		has_gpio_bank0 = true;

		sscanf(c, "%100s %1x", cmd, &val);

		if (val > 2 || val < 0)
			printf("Warning: gpio_hysteresis property in invalid region, using default value instead\n");
		else
			gpiomap_bank0.flags |= val << 6;
	}
	else if (strcmp(cmd, "back_power") == 0)
	{
		hatplus_unsupported(cmd);
		gpio_power_set = true; // required field
		has_gpio_bank0 = true;

		sscanf(c, "%100s %1x", cmd, &val);

		if (val > 2 || val < 0)
			printf("Warning: back_power property in invalid region, using default value instead\n");
		else
			gpiomap_bank0.power = val;
	}
	else if (strcmp(cmd, "bank1_gpio_drive") == 0)
	{
		hatplus_unsupported(cmd);
		bank1_gpio_drive_set = true; // required field if bank 1 is used
		has_gpio_bank1 = true;

		sscanf(c, "%100s %1x", cmd, &val);

		if (val > 8 || val < 0)
			printf("Warning: bank1 gpio_drive property in invalid region, using default value instead\n");
		else
			gpiomap_bank1.flags |= val;
	}
	else if (strcmp(cmd, "bank1_gpio_slew") == 0)
	{
		hatplus_unsupported(cmd);
		bank1_gpio_slew_set = true; // required field if bank 1 is used
		has_gpio_bank1 = true;

		sscanf(c, "%100s %1x", cmd, &val);

		if (val > 2 || val < 0)
			printf("Warning: bank1 gpio_slew property in invalid region, using default value instead\n");
		else
			gpiomap_bank1.flags |= val << 4;
	}
	else if (strcmp(cmd, "bank1_gpio_hysteresis") == 0)
	{
		hatplus_unsupported(cmd);
		bank1_gpio_hysteresis_set = true; // required field if bank 1 is used
		has_gpio_bank1 = true;

		sscanf(c, "%100s %1x", cmd, &val);

		if (val > 2 || val < 0)
			printf("Warning: bank1 gpio_hysteresis property in invalid region, using default value instead\n");
		else
			gpiomap_bank1.flags |= val << 6;
	}
	else if (strcmp(cmd, "setgpio") == 0)
	{
		hatplus_unsupported(cmd);
		fn = malloc(101);
		pull = malloc(101);

		sscanf(c, "%100s %d %100s %100s", cmd, &val, fn, pull);

		if (val < GPIO_MIN || val >= GPIO_COUNT_TOTAL)
			printf("Error: GPIO number out of bounds\n");
		else
		{
			struct gpio_map_d *gpiomap = &gpiomap_bank0;

			if (val >= GPIO_COUNT)
			{
				gpiomap = &gpiomap_bank1;
				val -= GPIO_COUNT;
				has_gpio_bank1 = true;
			}

			valid = true;
			pin = 0;

			if (strcmp(fn, "INPUT") == 0)
			{
				// no action
			}
			else if (strcmp(fn, "OUTPUT") == 0)
			{
				pin |= 1;
			}
			else if (strcmp(fn, "ALT0") == 0)
			{
				pin |= 4;
			}
			else if (strcmp(fn, "ALT1") == 0)
			{
				pin |= 5;
			}
			else if (strcmp(fn, "ALT2") == 0)
			{
				pin |= 6;
			}
			else if (strcmp(fn, "ALT3") == 0)
			{
				pin |= 7;
			}
			else if (strcmp(fn, "ALT4") == 0)
			{
				pin |= 3;
			}
			else if (strcmp(fn, "ALT5") == 0)
			{
				pin |= 2;
			}
			else
			{
				printf("Error at setgpio: function type not recognised\n");
				valid = false;
			}

			if (strcmp(pull, "DEFAULT") == 0)
			{
				// no action
			}
			else if (strcmp(pull, "UP") == 0)
			{
				pin |= 1 << 5;
			}
			else if (strcmp(pull, "DOWN") == 0)
			{
				pin |= 2 << 5;
			}
			else if (strcmp(pull, "NONE") == 0)
			{
				pin |= 3 << 5;
			}
			else
			{
				printf("Error at setgpio: pull type not recognised\n");
				valid = false;
			}

			pin |= 1 << 7; // board uses this pin

			if (valid)
				gpiomap->pins[val] = pin;
		}
	}
	else if (strcmp(cmd, "dt_blob") == 0)
	{
		finish_data();

		if (has_dt)
			fatal_error("Only one dt_blob allowed");
		has_dt = true;
		data_blob = &dt_blob;
		c += strlen("dt_blob");
		return parse_data(c);
	}
	else if (strcmp(cmd, "custom_data") == 0)
	{
		finish_data();

		data_blob = add_custom_blob();
		c += strlen("custom_data");
		return parse_data(c);
	}
	else if (strcmp(cmd, "end") == 0)
	{
		// close last data atom
		finish_data();
	}
	else if (data_blob)
	{
		return parse_data(c);
	}

	return 0;
}

static int read_text(const char *in)
{
	FILE *fp;
	char *line = NULL;
	char *c = NULL;
	size_t len = 0;
	ssize_t read;
	int linect = 0;
	char *command = (char *)malloc(101);
	int i;

	has_dt = false;

	printf("Opening file '%s' for read\n", in);

	fp = fopen(in, "r");
	if (fp == NULL)
	{
		printf("Error opening input file '%s'\n", in);
		return -1;
	}

	// allocating memory and setting up required atoms
	custom_cap = 1;
	custom_blobs = malloc(sizeof(struct atom_t) * custom_cap);

	in_string = false;

	while ((read = getline(&line, &len, fp)) != -1)
	{
		linect++;
		c = line;

		if (in_string)
		{
			if (parse_string(c))
				return -1;
			continue;
		}

		for (i = 0; i < read; i++)
		{
			if (c[i] == '#')
				c[i] = '\0';
		}

		while (isspace(*c))
			++c;

		if (*c == '\0' || *c == '\n' || *c == '\r')
		{
			// empty line, do nothing
		}
		else if (isalnum(*c))
		{
			sscanf(c, "%100s", command);

			if (parse_command(command, c))
				return -1;
		}
		else
			printf("Can't parse line %u: %s", linect, c);
	}

	finish_data();

	if (!product_serial_set || !product_id_set || !product_ver_set || !vendor_set || !product_set)
		printf("Warning: required fields missing in vendor information, using default values\n");

	if (hat_format == EEP_VERSION_HATV1 && !has_gpio_bank0)
		fatal_error("GPIO bank 0 is required for HAT V1");

	if (has_gpio_bank0 && (!gpio_drive_set || !gpio_slew_set || !gpio_hysteresis_set || !gpio_power_set))
		printf("Warning: required fields missing in GPIO map, using default values\n");

	if (has_gpio_bank1 && (!bank1_gpio_drive_set || !bank1_gpio_slew_set || !bank1_gpio_hysteresis_set))
		printf("Warning: required fields missing in GPIO map of bank 1, using default values\n");

	if (hat_format != EEP_VERSION_HATV1 && dt_blob.data && !isalnum(dt_blob.data[0]))
		fatal_error("Only embed the name of the overlay");

	printf("Done reading\n");

	return 0;
}

static int read_blob_file(const char *in, const char *type, struct var_blob_t *blob)
{
	FILE *fp;
	unsigned long size = 0;

	printf("Opening %s file '%s' for read\n", type, in);

	fp = fopen(in, "rb");
	if (fp == NULL)
		fatal_error("Error opening input file '%s'", in);

	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	printf("Adding %lu bytes of %s data\n", size, type);

	blob->dlen = size;

	blob->data = malloc(size);
	if (!fread(blob->data, size, 1, fp))
		goto err;

	fclose(fp);
	return 0;

err:
	printf("Unexpected EOF or error occurred\n");
	fclose(fp);
	return -1;
}

int main(int argc, char *argv[])
{
	const char *output_file;
	int argn = 1;
	int ret;

	while (argn < argc && argv[argn][0] == '-')
	{
		const char *arg = argv[argn++];
		if (!strcmp(arg, "-v1"))
		{
			printf("[ Reverting to legacy HAT V1 format ]\n");
			hat_format = EEP_VERSION_HATV1;
		}
		else
		{
			printf("Unrecognised option '%s'\n", arg);
			return 1;
		}
	}

	if ((argc - argn) < 2)
	{
		printf("Wrong input format.\n");
		printf("Try 'eepmake [-v1] input_file output_file [dt_file] [-c custom_file_1 ... custom_file_n]'\n");
		return 1;
	}

	ret = read_text(argv[argn++]);
	if (ret)
		fatal_error("Error reading and parsing input, aborting");

	output_file = argv[argn++];

	if (argn < argc && argv[argn][0] != '-')
	{
		// DT file specified
		if (hat_format == EEP_VERSION_HATPLUS)
			fatal_error("For HAT+ EEPROMs, specify the name of the DT overlay in the text file");
		ret = read_blob_file(argv[argn++], "DT", &dt_blob);
		if (ret)
			fatal_error("Error reading DT file, aborting");
		has_dt = true;
	}

	if (argn < argc && argv[argn][0] == '-')
	{
		if (strcmp(argv[argn], "-c") != 0)
			fatal_error("Unknown option - expected '-c'");
		argn++;
	}

	while (argn < argc)
	{
		struct var_blob_t *blob = add_custom_blob();
		// new custom data file
		ret = read_blob_file(argv[argn++], "custom data", blob);
		if (ret)
			fatal_error("Error reading custom file, aborting");
	}

	printf("Writing out...\n");

	ret = write_binary(output_file);
	if (ret)
		fatal_error("Error writing output");

	printf("Done.\n");

	return 0;
}
