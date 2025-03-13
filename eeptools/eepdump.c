#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#include "eeplib.h"

static struct var_blob_t dt_atom, custom_atom;
static struct vendor_info_d vinf;
static struct gpio_map_d gpiomap;
static struct power_supply_d supply;
static const char *blob_prefix;

static const char *atom_type_names[] ={
		[ATOM_INVALID_TYPE] = "invalid",
		[ATOM_VENDOR_TYPE] = "vendor",
		[ATOM_GPIO_TYPE] = "GPIO map",
		[ATOM_DT_TYPE] = "DT overlay",
		[ATOM_CUSTOM_TYPE] = "manufacturer custom data",
		[ATOM_GPIO_BANK1_TYPE] = "GPIO map bank 1",
		[ATOM_POWER_SUPPLY_TYPE] = "power supply",
};

static void dump_data(FILE *out, const uint8_t *data, int len)
{
	bool is_string = true, is_simple_string = true;
	int j;

	for (j = 0; j < len; j++)
	{
		int c = data[j];
		if (!c || c == '"' || !isprint(c))
			is_simple_string = false;
		if (c && !isprint(c) && !isspace(c))
		{
			is_string = false;
			break;
		}
	}

	if (is_simple_string)
	{
		fputs(" \"", out);
		for (j = 0; j < len; j++)
			fputc(data[j], out);
		fputs("\"\n", out);
	}
	else if (is_string)
	{
		fputs(" \"\n", out);

		for (j = 0; j < len; j++)
		{
			int c = data[j];
			if (c == '\\')
				fputs("\\\\", out);
			else if (c == '\r')
				fputs("\\r", out);
			else if (!c)
				fputs("\\0\n", out);
			else
				fputc(c, out);
		}
		fputs("\\\"\n", out);
	}
	else
	{
		for (j = 0; j < len; j++)
		{
			if (j % 16 == 0)
				fprintf(out, "\n");
			fprintf(out, "%02X ", data[j]);
		}
		fprintf(out, "\n");
	}
}

static void dump_blob(const char *filename, const struct var_blob_t *blob)
{
	FILE *fp = fopen(filename, "wb");
	if (fp)
	{
		fwrite(blob->data, blob->dlen, 1, fp);
		fclose(fp);
	}
}
static int read_bin(const char *in, const char *outf)
{
	enum file_ver_t hat_format;
	int numatoms;
	int custom_count = 0;
	const char *atom_name;
	FILE *fp, *out;
	int i;
	uint32_t j;

	fp = fopen(in, "rb");
	if (!fp)
	{
		printf("Error reading file %s\n", in);
		return -1;
	}

	if (outf)
		out = fopen(outf, "w");
	else
		out = stdout;

	if (!out)
	{
		printf("Error writing file %s\n", outf);
		return -1;
	}

	fprintf(out, "# ---------- Dump generated by eepdump handling format version 0x%02x ----------\n#\n", EEP_VERSION);

	eepio_start(&hat_format, &numatoms, EEPIO_READ, fp);

	if (EEP_VERSION_HATPLUS != hat_format && EEP_VERSION_HATV1 != hat_format)
	{
		fprintf(out, "# WARNING: format version mismatch!!!\n");
		goto err;
	}

	if (EEP_VERSION != eep_header.ver && HEADER_SIGN != eep_header.signature)
	{
		printf("header version and signature mismatch, maybe wrong file?\n");
		goto err;
	}

	fprintf(out, "# --Header--\n# signature=0x%08x\n# version=0x%02x\n# reserved=%u\n# numatoms=%u\n# eeplen=%u\n# ----------\n\n\n",
		eep_header.signature, eep_header.ver, eep_header.res, eep_header.numatoms, eep_header.eeplen);

	for (i = 0; i < numatoms && !eepio_got_error(); i++)
	{
		unsigned int gpio_count;
		enum atom_type_t type = ATOM_INVALID_TYPE;
		bool bank1 = false;
		uint32_t dlen;

		if (!eepio_atom_start(&type, &dlen))
			goto err;

		atom_name = (type < count_of(atom_type_names)) ? atom_type_names[type] : "unknown";
		printf("Reading atom %d (type = 0x%04x (%s), length = %i bytes)...\n", i,
			type, atom_name, eep_atom_header.dlen);

		fprintf(out, "# Start of atom #%u of type 0x%04x and length %u\n", eep_atom_header.count, type, eep_atom_header.dlen);

		switch (type)
		{
		case ATOM_VENDOR_TYPE:
			if (!eepio_atom_vinf(&vinf))
				goto atom_read_err;

			fprintf(out, "# Vendor info\n");
			fprintf(out, "product_uuid %08x-%04x-%04x-%04x-%04x%08x\n",
					vinf.serial[3],
					vinf.serial[2] >> 16, vinf.serial[2] & 0xffff,
					vinf.serial[1] >> 16, vinf.serial[1] & 0xffff,
					vinf.serial[0]);
			fprintf(out, "product_id 0x%04x\n", vinf.pid);
			fprintf(out, "product_ver 0x%04x\n", vinf.pver);

			fprintf(out, "vendor \"%s\"   # length=%u\n", vinf.vstr, vinf.vslen);
			fprintf(out, "product \"%s\"   # length=%u\n", vinf.pstr, vinf.pslen);
			break;

		case ATOM_POWER_SUPPLY_TYPE:
			if (!eepio_atom_power_supply(&supply))
				goto atom_read_err;

			fprintf(out, "# power supply\n");
			fprintf(out, "current_supply %u\n", supply.current_supply);
			break;

		case ATOM_GPIO_BANK1_TYPE:
			bank1 = true;
			/* fall through... */
		case ATOM_GPIO_TYPE:
			if (!eepio_atom_gpio(&gpiomap))
				goto atom_read_err;
			fprintf(out, "# GPIO ");
			if (bank1)
				fprintf(out, "bank 1 ");
			fprintf(out, "map info\n");

			if (bank1)
				fprintf(out, "bank1_");
			fprintf(out, "gpio_drive %d\n", gpiomap.flags & 15); // 1111

			if (bank1)
				fprintf(out, "bank1_");
			fprintf(out, "gpio_slew %d\n", (gpiomap.flags & 48) >> 4); // 110000

			if (bank1)
				fprintf(out, "bank1_");
			fprintf(out, "gpio_hysteresis %d\n", (gpiomap.flags & 192) >> 6); // 11000000

			if (!bank1)
				fprintf(out, "back_power %d\n", gpiomap.power);
			fprintf(out, "#        GPIO  FUNCTION  PULL\n#        ----  --------  ----\n");

			gpio_count = bank1 ? GPIO_COUNT_BANK1 : GPIO_COUNT;
			for (j = 0; j < gpio_count; j++)
			{
				if (gpiomap.pins[j] & (1 << 7))
				{
					// board uses this pin
					char *pull_str = "INVALID";
					char *func_str = "INVALID";

					switch ((gpiomap.pins[j] & 96) >> 5)
					{ // 1100000
					case 0:
						pull_str = "DEFAULT";
						break;
					case 1:
						pull_str = "UP";
						break;
					case 2:
						pull_str = "DOWN";
						break;
					case 3:
						pull_str = "NONE";
						break;
					}

					switch ((gpiomap.pins[j] & 7))
					{ // 111
					case 0:
						func_str = "INPUT";
						break;
					case 1:
						func_str = "OUTPUT";
						break;
					case 4:
						func_str = "ALT0";
						break;
					case 5:
						func_str = "ALT1";
						break;
					case 6:
						func_str = "ALT2";
						break;
					case 7:
						func_str = "ALT3";
						break;
					case 3:
						func_str = "ALT4";
						break;
					case 2:
						func_str = "ALT5";
						break;
					}

					fprintf(out, "setgpio  %d      %s     %s\n",
							bank1 ? j + GPIO_COUNT : j, func_str, pull_str);
				}
			}
			break;

		case ATOM_DT_TYPE:
			dt_atom.dlen = dlen;
			if (!eepio_atom_var(&dt_atom))
				goto atom_read_err;

			fprintf(out, "dt_blob");
			dump_data(out, dt_atom.data, dt_atom.dlen);
			if (blob_prefix)
			{
				char filename[FILENAME_MAX];
				sprintf(filename, "%s_dt_blob", blob_prefix);
				dump_blob(filename, &dt_atom);
			}
			break;

		case ATOM_CUSTOM_TYPE:
			custom_atom.dlen = dlen;
			if (!eepio_atom_var(&custom_atom))
				goto atom_read_err;
			fprintf(out, "custom_data");
			dump_data(out, custom_atom.data, custom_atom.dlen);
			if (blob_prefix)
			{
				char filename[FILENAME_MAX];
				sprintf(filename, "%s_custom_data_%d", blob_prefix, custom_count);
				dump_blob(filename, &custom_atom);
			}
			custom_count++;
			break;

		default:
			printf("Error: unrecognised atom type\n");
			fprintf(out, "# Error: unrecognised atom type\n");
			goto err;
		}

		eepio_atom_end();

		fprintf(out, "# End of atom. CRC16=0x%04x\n", eep_atom_crc);
		fprintf(out, "\n");
	}

	eepio_end();

	printf("Done.\n");

	fclose(fp);
	if (outf)
		fclose(out);
	return 0;

atom_read_err:
	printf("Error reading %s atom\n", atom_name);

err:
	printf("Unexpected EOF or error occurred\n");
	fclose(fp);
	fclose(out);
	return -1;
}

int usage(void)
{
	printf("Usage: eepdump input_file [-b blob_prefix] [output_file]\n");
	printf("  where\n");
	printf("    blob_prefix is prefix string used to generate file names for any\n");
	printf("    data blobs\n");
	return 1;
}

int main(int argc, const char *argv[])
{
	int argn;

	for (argn = 1; argn < argc && argv[argn][0] == '-'; argn++)
	{
		const char *arg = argv[argn];
		if (!strcmp(arg, "-b"))
		{
			if (argn == argc)
				return usage();
			blob_prefix = argv[++argn];
		}
		else
		{
			printf("Unknown option '%s'\n", arg);
			return usage();
		}
	}

	if (argc <= argn)
		return usage();

	return read_bin(argv[argn], argv[argn + 1]);
}
