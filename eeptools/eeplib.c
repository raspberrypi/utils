#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "eeplib.h"

struct header_t eep_header;
struct atom_t eep_atom_header;
uint16_t eep_atom_crc;
unsigned int eep_atom_num;

static enum eepio_dir_t eepio_dir;
static FILE *eepio_fp;
static uint16_t crc_state;
static long eepio_pos_start;
static long eepio_atom_data_start;
static bool eepio_error_flag;
static bool eepio_buffer_writes;
static void *eepio_write_buf;
static int eepio_write_buf_pos;
static int eepio_write_buf_size;

static log_callback_t eep_error_callback;
static log_callback_t eep_warning_callback;

void eepio_clear_error(void)
{
	eepio_error_flag = false;
}

bool eepio_error(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	eepio_error_flag = true;
	if (eep_error_callback)
		(*eep_error_callback)(msg, ap);
	else
	{
		printf("ERROR: ");
		vprintf(msg, ap);
		printf("\n");
	}
	va_end(ap);
	return true;
}

void eepio_fatal_error(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	if (eep_error_callback)
		(*eep_error_callback)(msg, ap);
	else
	{
		printf("ERROR: ");
		vprintf(msg, ap);
		printf("\n");
	}
	va_end(ap);
	exit(1);
}

void eepio_warning(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	if (eep_warning_callback)
		(*eep_warning_callback)(msg, ap);
	else
	{
		printf("WARNING: ");
		vprintf(msg, ap);
		printf("\n");
	}
	va_end(ap);
}

bool eepio_got_error(void)
{
	return eepio_error_flag;
}

void crc_init(void)
{
	crc_state = 0;
}

void crc_add(uint8_t *data, unsigned int size)
{
	int bits_read = 0, bit_flag;

	while (size > 0)
	{
		bit_flag = crc_state >> 15;

		/* Get next bit: */
		crc_state <<= 1;
		/* work from the least significant bits */
		crc_state |= (*data >> bits_read) & 1;

		/* Increment bit counter: */
		bits_read++;
		if (bits_read > 7)
		{
			bits_read = 0;
			data++;
			size--;
		}

		/* Cycle check: */
		if (bit_flag)
			crc_state ^= CRC16;
	}
}

uint16_t crc_get(void)
{
	uint16_t crc;
	int i, j;

	/* "push out" the last 16 bits */
	for (i = 0; i < 16; ++i)
	{
		int bit_flag = crc_state >> 15;
		crc_state <<= 1;
		if (bit_flag)
			crc_state ^= CRC16;
	}

	/* reverse the bits */
	crc = 0;
	i = 0x8000;
	j = 0x0001;
	for (; i != 0; i >>= 1, j <<= 1)
	{
		if (i & crc_state)
			crc |= j;
	}

	return crc;
}

static void *eepio_write_buf_space(int len)
{
	void *space;

	if ((eepio_write_buf_pos + len) > eepio_write_buf_size)
	{
		eepio_write_buf_size += eepio_write_buf_size + len * 2;
		eepio_write_buf = realloc(eepio_write_buf, eepio_write_buf_size);
		if (!eepio_write_buf)
			eepio_fatal_error("Out of memory");
	}

	space = (void *)((uint8_t *)eepio_write_buf + eepio_write_buf_pos);
	eepio_write_buf_pos += len;
	return space;
}

void eepio_blob(void *blob, int len)
{
	if (eepio_buffer_writes)
	{
		memcpy(eepio_write_buf_space(len), blob, len);
		return;
	}
	if (eepio_dir == EEPIO_READ)
	{
		if (!fread(blob, len, 1, eepio_fp))
			eepio_error("Failed to read from file");
	}
	else
	{
		if (!fwrite(blob, len, 1, eepio_fp))
			eepio_error("Failed to write to file");
	}
	crc_add(blob, len);
}

void eepio_string(char **pstr, int len)
{
	if (eepio_dir == EEPIO_READ)
		*pstr = (char *)malloc(len + 1);
	eepio_blob(*pstr, len);
	(*pstr)[len] = 0;
}


void eepio_start(enum file_ver_t *pver, int *pnumatoms, enum eepio_dir_t dir, FILE *fp)
{
	if (dir == EEPIO_WRITE)
	{
		eep_header.signature = HEADER_SIGN;
		eep_header.ver = *pver;
		eep_header.res = 0;
		eep_header.numatoms = 0;
	}
	eepio_dir = dir;
	eepio_fp = fp;
	eepio_pos_start = ftell(eepio_fp);
	eepio_blob(&eep_header, sizeof(eep_header));
	if (dir == EEPIO_READ)
	{
		*pver = eep_header.ver;
		if (eep_header.signature != HEADER_SIGN)
			eepio_warning("Format signature mismatch");
		if (pnumatoms)
			*pnumatoms = eep_header.numatoms;
	}
	eep_atom_num = 0;
}

bool eepio_end(void)
{
	// Patch/check header with atom count and total length
	long pos, pos_end;

	pos = ftell(eepio_fp);
	if (pos == -1)
		return eepio_error("ftell failed");
	if (eepio_dir == EEPIO_WRITE)
	{
		eep_header.eeplen = pos - eepio_pos_start;
		eep_header.numatoms = eep_atom_num;
		fseek(eepio_fp, eepio_pos_start, SEEK_SET);
		eepio_blob(&eep_header, sizeof(eep_header));
	}
	else
	{
		fseek(eepio_fp, 0L, SEEK_END);
		pos_end = ftell(eepio_fp);
		if (pos_end == -1)
			return eepio_error("ftell failed");
		if (pos != pos_end)
			eepio_warning("Dump finished before EOF");
		if (pos != (long)eep_header.eeplen)
			eepio_warning("Dump finished before length specified in header");
		if (pos_end != (long)eep_header.eeplen)
			eepio_warning("EOF does not match length specified in header");
		if (pos_end != (long)eep_header.eeplen)
			eepio_warning("%i bytes of file not processed");
	}
	return !eepio_got_error();
}

bool eepio_atom_start(enum atom_type_t *type, uint32_t *pdlen)
{
	if (eepio_dir == EEPIO_WRITE)
	{
		eep_atom_header.type = *type;
		eep_atom_header.count = eep_atom_num;
		eepio_write_buf_pos = 0;
		eepio_buffer_writes = true;
	}
	crc_init();
	if (eepio_dir == EEPIO_READ)
	{
		eepio_blob(&eep_atom_header, ATOM_HDR_SIZE);
		*type = eep_atom_header.type;
		if (eep_atom_num != eep_atom_header.count)
			eepio_error("Atom count mismatch (expected %u)", eep_atom_num);
	}
	eepio_atom_data_start = ftell(eepio_fp);
	if (pdlen)
		*pdlen = eep_atom_header.dlen - CRC_SIZE;
	return !eepio_got_error();
}

void eepio_atom_end(void)
{
	uint16_t crc_actual;

	if (eepio_dir == EEPIO_WRITE)
	{
		eep_atom_header.dlen = eepio_write_buf_pos + 2;
		eepio_buffer_writes = false;
		eepio_blob(&eep_atom_header, ATOM_HDR_SIZE);
		eepio_blob(eepio_write_buf, eepio_write_buf_pos);
	}
	crc_actual = crc_get();
	eep_atom_crc = crc_actual;
	eepio_blob(&eep_atom_crc, 2);
	if (eepio_dir == EEPIO_READ)
	{
		long pos = ftell(eepio_fp);
		if (pos - eepio_atom_data_start != (long)eep_atom_header.dlen)
			eepio_warning("atom data length mismatch");
		if (crc_actual != eep_atom_crc)
			eepio_warning("atom CRC16 mismatch. Calculated CRC16=0x%02x", crc_actual);
	}
	eep_atom_num++;
}

bool eepio_atom_var(struct var_blob_t *var)
{
	if (eepio_dir == EEPIO_READ)
	{
		var->data = malloc(var->dlen);
		if (!var->data)
			eepio_fatal_error("out of memory");
	}
	eepio_blob(var->data, var->dlen);
	return !eepio_got_error();
}

bool eepio_atom_vinf(struct vendor_info_d *vinf)
{
	eepio_blob(vinf, VENDOR_SIZE);
	eepio_string(&vinf->vstr, vinf->vslen);
	eepio_string(&vinf->pstr, vinf->pslen);
	return !eepio_got_error();
}

bool eepio_atom_power_supply(struct power_supply_d *power)
{
	eepio_blob(power, POWER_SUPPLY_SIZE);
	return !eepio_got_error();
}

bool eepio_atom_gpio(struct gpio_map_d *map)
{
	eepio_blob(map, GPIO_SIZE);
	return !eepio_got_error();
}

bool eepio_atom_gpio_bank1(struct gpio_map_d *map)
{
	eepio_blob(map, GPIO_BANK1_SIZE);
	return !eepio_got_error();
}
