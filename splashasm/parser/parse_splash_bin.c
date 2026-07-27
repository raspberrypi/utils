#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "parse_splash_bin.h"

int parse_define(FILE* f, DEFINE_INST_T* inst)
{
    if (fread(&inst->header, sizeof(inst->header), 1, f) != 1)
        return -1;

    if (memcmp(&inst->header.protocol, "I2C ", 4) == 0)
    {
        if (fread(&inst->config.i2c_config, sizeof(inst->config.i2c_config), 1, f) != 1)
            return -1;
    }
    else if (memcmp(&inst->header.protocol, "SPI ", 4) == 0)
    {
        if (fread(&inst->config.spi_config, sizeof(inst->config.spi_config), 1, f) != 1)
            return -1;
    }

    return 0;
}

int parse_command(FILE* f, COMMAND_INST_T* inst)
{
    if (fread(&inst->header, sizeof(inst->header), 1, f) != 1)
        return -1;

    if (!inst->data)
        return -1;
    
    if (inst->header.data_len > MAX_COMMAND_LENGTH)
        return -1;

    if (fread(inst->data, 1, inst->header.data_len, f) != inst->header.data_len)
        return -1;

    return 0;
}

void free_command(COMMAND_INST_T* inst)
{
    free(inst->data);
    inst->data = NULL;
}


int parse_delay(FILE* f, DELAY_INST_T* inst)
{
    if (fread(inst, sizeof(*inst), 1, f) != 1)
        return -1;

    return 0;
}


void parse_binary(FILE* f)
{
    uint8_t header[16];
    uint8_t op;
    DELAY_INST_T del_inst;
    DEFINE_INST_T def_inst;
    COMMAND_INST_T cmd_inst;
    int num_defines = 0;

    if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
        memcmp(header, "SPLASH ASM\0\0\0\0\0", 15) != 0 || header[15] != 1)
    {
        fprintf(stderr, "bad header\n");
        return;
    }

    cmd_inst.data = malloc(sizeof(uint8_t) * MAX_COMMAND_LENGTH);

    while (fread(&op, 1, 1, f) == 1)
    {
        if (op == INST_OPCODE_DELAY)
        {
            if (parse_delay(f, &del_inst))
                break;

            printf("delay %u\n", del_inst.delay_time_us);
        }
        else if (op == INST_OPCODE_DEFINE)
        {
            if (parse_define(f, &def_inst))
                break;

            if (memcmp(&def_inst.header.protocol, "I2C ", 4) == 0)
            {
                printf("define out%u i2c [SDA %u] [SCL %u] [ADDR %u] [FREQ %u]\n",
                        num_defines, def_inst.config.i2c_config.sda_pin, def_inst.config.i2c_config.scl_pin,
                        def_inst.config.i2c_config.endpoint_address, def_inst.config.i2c_config.frequency);
            }
            else if (memcmp(&def_inst.header.protocol, "SPI ", 4) == 0)
            {
                printf("define out%u spi [COPI %u] [CIPO %u] [SCLK %u] [CS %u] [DC %u] [CPOL %u] [CPHA %u] [CSPOL %u] [FREQ %u]\n",
                        num_defines, def_inst.config.spi_config.copi_pin, def_inst.config.spi_config.cipo_pin,
                        def_inst.config.spi_config.sclk_pin, def_inst.config.spi_config.cs_pin, def_inst.config.spi_config.dc_pin,
                        def_inst.config.spi_config.clock_polarity, def_inst.config.spi_config.clock_phase,
                        def_inst.config.spi_config.cs_active_high, def_inst.config.spi_config.frequency);
            }
            else
            {
                fprintf(stderr, "unknown fourcc %c%c%c%c\n",
                                                (def_inst.header.protocol >> 24) & 0xFF,
                                                (def_inst.header.protocol >> 16) & 0xFF,
                                                (def_inst.header.protocol >> 8)  & 0xFF,
                                                 def_inst.header.protocol        & 0xFF);
                return;
            }
            num_defines++;
        }
        else if (op == INST_OPCODE_COMMAND)
        {
            if (parse_command(f, &cmd_inst))
                break;

            printf("out%u [flags 0x%08x]", cmd_inst.header.id, cmd_inst.header.flags);                                                                                                                    
            for (uint16_t i = 0; i < cmd_inst.header.data_len; i++)                                                                                                                                    
              printf(" 0x%02x", cmd_inst.data[i]);                                                                                                                                                   
            printf("\n");                                                                                                                                                                                              
        }
        else
        {
            fprintf(stderr, "unknown opcode 0x%02x\n", op);
            break;
        }
    }
}

int main(int argc, char** argv)
{
    FILE* f;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <splash bin>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f)
    {
        fprintf(stderr, "invalid file\n");
        return 1;
    }

    parse_binary(f);
    fclose(f);

    return 0;
}
