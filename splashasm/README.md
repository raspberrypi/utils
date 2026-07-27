# Splash Asm

## Introduction

A toy language and binary format for describing SPI & I2C dumps, aimed at putting splash screens on SPI & I2C displays during vc4 boot on Raspberry Pis. This currently only supports non RP1 RPis, i.e. everything but the Pi 5.

## Usage

1. Clone this repository

2. Write a .splash file, or use one of the examples

3. Run `python3 splash_assembler.py <input>.splash -o output.bin` (You need python 3.12 or later)

4. Either already be on or mount a bootable Raspberry Pi drive

5. Copy this output.bin file into `/boot/firmware/`

6. Edit `/boot/firmware/config.txt`, adding a line `splash_screen=output.bin` anywhere in the file

## Tips

* To import an image for splash screens, write a python script to output your image binary in the correct format into a human readable splash file
* Consts are useful, the way I have written this is so that you can structure a generic configuration file for your ic, and then have the actual data in another file. This allows the configuration files to be reused. Use extern consts and expressions like I have in st7789.splash to keep things clean

## The language

There are five kinds of instructions you can write

- `define`
- `command` - this is not a keyword, you write the command name as defined in define to invoke it
- `const`
- `delay`
- `import`

The syntax is as follows:

```bnf
<define>        ::= "define" <command-name> <protocol> <params>
    <protocol>  ::= "spi" | "i2c"
    

<command>       ::= <command-name> <params> <opt-data>
    <opt-data>  ::= <data> <opt-data> | <data>

<const>  ::= "const" <const-name> "extern" | const <const-name> <data>

<delay>  ::= "delay" <params> <int>

<import> ::= "import" <filename>


<params>  ::= "[" <key> <value> "]" | "[" <flag> "]"
<data>    ::= <hex-value> | <const-name>
```

Newlines and whitespaces are treated the same, the only seperator is either a newline or a whitespace, and one newline or whitespace is the same as n newlines and/or whitespace

What are valid params for each instruction are better defined in the binary docs, I always learn best from examples, so I recomend having a look through those

## The binary

```
-----------------------------------------------------------------------
                      1. FILE HEADER - 16 bytes

    0               1               2               3
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               |
   :                                                               :
   |                         "SPLASH ASM"                          |
   :                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                               |      0x00     |      0x00     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |      0x00     |      0x00     |      0x00     |    Version    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Magic   : ASCII "SPLASH ASM" followed by five 0x00 pad bytes (15 B)
    Version : format version, currently 0x01

    Everything after byte 15 is a stream of instructions, each
    beginning with a one-byte opcode:

        0x00  DELAY
        0x01  SETUP
        0x10  COMMAND



-----------------------------------------------------------------------
                       2. DELAY (0x00) - 5 bytes


   +-+-+-+-+-+-+-+-+
   |  Opcode 0x00  |
   +-+-+-+-+-+-+-+-+


    0               1               2               3
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                             Delay                             |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Delay : microseconds to wait (source [US]/[MS]/[S]
            units are multiplied out by the assembler)



-----------------------------------------------------------------------
         3. DEFINE (0x01) - 9-byte header + #Size Parameters


   +-+-+-+-+-+-+-+-+
   |  Opcode 0x01  |
   +-+-+-+-+-+-+-+-+


    0               1               2               3
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                       Protocol four cc                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |     Size      |                    Reserved                   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               :
   :                          Parameters                           :
   :                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Protocol : A four cc code, currently either "SPI " or "I2C "
    Size     : length in bytes of the parameters block that follows.
               This is unique to a four cc and file version
    Reserved : three pad bytes (struct alignment after Size), always 0x00

    Defines are implicitly numbered: the index used later by 
    COMMAND's "Out idx" field is just the order in which DEFINE 
    instructions appear in the stream (0, 1, 2, ...).

    The shape of the N-byte param block depends on Protocol:

    3a. I2C Parameter block (8 bytes)

    0               1               2               3
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |    SDA pin    |    SCL pin    |      ADDR     |    Reserved   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           Frequency                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    ADDR      : The I2C address of the endpoint

    3b. SPI Parameter block (12 bytes)

    0               1               2               3
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |    COPI pin   |    CIPO pin   |    SCLK pin   |     CS pin    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |     DC pin    |      CPOL     |      CPHA     |     CSPOL     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           Frequency                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    CPOL/CPHA  : SPI mode bits (default mode 0)
    CPOL       : clock polarity, if 1 the data is transmitted on
                 rising edges, if 2 the data is transmitted on
                 falling edges (default 1)
    CSPOL      : chip-select polarity active-low is 1
                 active high is 2, (default 1)
    CPHA       : clock phase, if 1 the clock transitions in the
                 middle of bits, if 2, the data transitions are in
                 phase with the clock
    


-----------------------------------------------------------------------
            4. COMMAND (0x10) - 5-byte header + #Size data


    +-+-+-+-+-+-+-+-+
    |  Opcode 0x10  |
    +-+-+-+-+-+-+-+-+

    0               1               2               3
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |    Out idx    |     Flags     |              Size             |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               :
   :                       Data (Size bytes)                       :
   :                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Out idx : which DEFINE this command targets, by definition order
    Flags   : 8-bit bitmask, only bit 0 is currently defined:
    Data : raw bytes to shift out over the target bus

    4a. SPI Flag bitmask

   +-+-+-+-+-+-+-+-+
   |S| Reserved  |D|
   +-+-+-+-+-+-+-+-+

    bit 0 (D), DATA_ONLY  : If this is 1 then the DC pin always
                            indicates data in the command, if 0,
                            the DC pin indicates the first byte
                            as a command
    bit 7 (S), SWALLOW_ERRORS  : If this is 1 then we don't end
                                 the splash on a transaction error
                                 (mainly caused by nacked i2c)

    4b. I2C Flag bitmask

   +-+-+-+-+-+-+-+-+
   |S| Reserved  |R|
   +-+-+-+-+-+-+-+-+

    bit 0 (R), READ  : Indicates an I2C read if 1, indicates an
                       I2C write if 0 (default)
    bit 7 (S), SWALLOW_ERRORS  : If this is 1 then we don't end
                                 the splash on a transaction error
                                 (mainly caused by nacked i2c)
-----------------------------------------------------------------------
```
*(inspired by https://www.ietf.org/rfc/rfc793.html)*

## Limitations

- This is currently incompatible with the Pi 5
- You can have a maximum of 4 SPI defines
- You can have a maximum of 10 I2C defines
- The delays are blocking and therefore a long splash description will slow down a boot
- The only timing guarantee is that if you write a delay command, there will be a wait strictly greater than your delay command. This is not made for timing sensitive applications, there are further delays due to parsing