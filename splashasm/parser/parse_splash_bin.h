#include <stdint.h>

#define MAX_COMMAND_LENGTH 1024

typedef enum {
   INST_OPCODE_DELAY   = 0x00,
   INST_OPCODE_DEFINE  = 0x01,
   INST_OPCODE_COMMAND = 0x10
} INST_OPCODE_T;

typedef enum {
   SPI_DATA_ONLY_FLAG = 1,
} SPI_FLAGS_T;

typedef enum {
   I2C_READ_FLAG  = 1,
} I2C_FLAGS_T;

typedef enum {
   SWALLOW_ERRORS_FLAG = 0x80
} ALL_FLAGS_T;

typedef struct {
   uint32_t    protocol;
   uint8_t     param_len;
} DEFINE_HEADER_T;

typedef struct {
   uint8_t copi_pin;
   uint8_t cipo_pin;
   uint8_t sclk_pin;
   uint8_t cs_pin;
   uint8_t dc_pin;
   uint8_t clock_polarity;
   uint8_t clock_phase;
   uint8_t cs_active_high;
   uint32_t frequency;
} SPI_CONFIG_T;

typedef struct {
   uint8_t sda_pin;
   uint8_t scl_pin;
   uint8_t endpoint_address;
   uint8_t reserved;
   uint32_t frequency;
} I2C_CONFIG_T;

typedef union {
   SPI_CONFIG_T spi_config;
   I2C_CONFIG_T i2c_config;
} WIRE_PROTOCOL_CONFIG_T;

typedef struct {
   DEFINE_HEADER_T header;
   WIRE_PROTOCOL_CONFIG_T config;
} DEFINE_INST_T;

typedef struct {
   uint8_t id;
   uint8_t flags;
   uint16_t data_len;
} COMMAND_HEADER_T;

typedef struct {
   COMMAND_HEADER_T header;
   uint8_t* data;
} COMMAND_INST_T;

typedef struct {
   uint32_t    delay_time_us;
} DELAY_INST_T;
