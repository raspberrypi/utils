# dtapply and examples

`dtapply` reads a Raspberry Pi `config.txt` file and uses `dtmerge` to apply
all of its `dtparam` and `dtoverlay` directives to a base `.dtb` file,
producing a merged `.dtb` without requiring a running Pi. This could be used
to speed up the booting process.

## Usage

```
usage: dtapply [-h] [-o OUTPUT] [--overlays-dir OVERLAYS_DIR]
               [--dtmerge DTMERGE] [--model MODEL] [-d] [-n]
               dtb config

positional arguments:
  dtb                   Base DTB file (e.g. bcm2711-rpi-4-b.dtb)
  config                Raspberry Pi config.txt file

options:
  -h, --help            show this help message and exit
  -o OUTPUT, --output OUTPUT
                        Output DTB file (default: output.dtb)
  --overlays-dir OVERLAYS_DIR
                        Directory containing .dtbo overlay files (default:
                        auto-detect /boot/firmware/overlays or /boot/overlays)
  --dtmerge DTMERGE     Path to the dtmerge binary (default: dtmerge)
  --model MODEL         Override Pi model identifier for section filtering
                        (e.g. pi4, cm4, pi5)
  -d, --debug           Pass -d to dtmerge and print extra information
  -n, --dry-run         Parse and print directives but do not run dtmerge
```

## Example files

| File | Demonstrates |
|---|---|
| `config-basic.txt` | Common `dtparam` and `dtoverlay` lines with no section filtering |
| `config-sections.txt` | Conditional `[section]` headers to target specific Pi models |
| `config-overlay-context.txt` | Multi-line overlay parameters and mixed global/overlay params |

---

## config-basic.txt — Basic usage

```
dtapply bcm2711-rpi-4-b.dtb config-basic.txt -o merged.dtb
```

The simplest case: every directive falls under the implicit `[all]` section
and is applied unconditionally.  `dtapply` calls `dtmerge` once per
`dtparam` block and once per `dtoverlay`, chaining the output of each step
into the next.

```
dtparam=audio=on
dtparam=i2c_arm=on
dtparam=spi=on
dtoverlay=vc4-kms-v3d
dtoverlay=w1-gpio,gpiopin=4
```

Produces five `dtmerge` calls in sequence:

```
dtmerge base.dtb step1.dtb - audio=on
dtmerge step1.dtb step2.dtb - i2c_arm=on
dtmerge step2.dtb step3.dtb - spi=on
dtmerge step3.dtb step4.dtb vc4-kms-v3d.dtbo
dtmerge step4.dtb merged.dtb w1-gpio.dtbo gpiopin=4
```

---

## config-sections.txt — Conditional section filters

`config.txt` uses `[section]` headers to gate directives to specific
hardware.  `dtapply` infers the Pi model from the DTB filename:

| DTB prefix | Model identifier(s) |
|---|---|
| `bcm2708-rpi-zero-w` | `pi0w` |
| `bcm2708-rpi-b-plus` | `pi1` |
| `bcm2709-rpi-2-b` | `pi0w` |
| `bcm2710-rpi-zero-2-w` | `pi02` |
| `bcm2710-rpi-3-b` | `pi3` |
| `bcm2710-rpi-cm0` | `cm0` |
| `bcm2711-rpi-4-b` | `pi4` |
| `bcm2711-rpi-400` | `pi400` |
| `bcm2711-rpi-cm4` | `cm4` |
| `bcm2712-rpi-5-b` | `pi5` |
| `bcm2712-rpi-cm5` | `cm5` |
| `bcm2712-rpi-500` | `pi500` |
| … | … |
(not an exhaustive list)

A directive is included if and only if its enclosing section matches the
inferred model, or is `[all]`.  Lines before the first `[section]` header
are treated as `[all]`.  `[none]` suppresses everything inside it.

```
dtapply bcm2711-rpi-4-b.dtb config-sections.txt -o merged-pi4.dtb
dtapply bcm2712-rpi-5-b.dtb config-sections.txt -o merged-pi5.dtb
```

For `bcm2711-rpi-4-b.dtb` (`pi4` model), only the `[all]` and `[pi4]`
sections are active — the `[pi5]` and `[cm4]` directives are skipped.

If the model cannot be reliably inferred from the filename, use `--model`:

```
dtapply mystery-board.dtb config-sections.txt --model pi4 -o merged.dtb
```

Use `--dry-run` (`-n`) to inspect which directives would be applied without
actually running `dtmerge`:

```
dtapply bcm2712-rpi-5-b.dtb config-sections.txt -n
```

```
Detected model: pi5
Found 3 action(s) to apply:
  dtparam=audio=on
  dtparam=i2c_arm=on
  dtoverlay=vc4-kms-v3d-pi5
  dtparam=spi=on
```

---

## config-overlay-context.txt — Overlay context and multi-line parameters

### Parameters split across lines

A `dtoverlay` line sets the *current overlay context*.  All subsequent
`dtparam` lines are treated as additional parameters for that overlay until
the context changes.  This lets you spread a long parameter list across
multiple lines:

```
dtoverlay=i2c-gpio
dtparam=i2c_gpio_sda=2
dtparam=i2c_gpio_scl=3
dtparam=i2c_gpio_delay_us=2
dtparam=bus=3
```

All five lines are collapsed into a single `dtmerge` call:

```
dtmerge current.dtb next.dtb i2c-gpio.dtbo \
    i2c_gpio_sda=2 i2c_gpio_scl=3 i2c_gpio_delay_us=2 bus=3
```

### Global parameters inside an overlay block

Each parameter is resolved against the current overlay's `__overrides__`
node first, falling back to the base device tree's `__overrides__` if it
is not found there.  This means base-DT parameters can appear inside an
overlay block without causing an error:

```
dtoverlay=uart0,txd0_pin=14,rxd0_pin=15
dtparam=i2c_arm_baudrate=400000
```

Here `i2c_arm_baudrate` is a base-DT parameter, not a `uart0` parameter.
`dtmerge` detects this automatically and applies it to the base DT.  The
result is the same as if the `dtparam` line had appeared outside the
overlay block.

### Resetting the overlay context

A bare `dtoverlay=` (nothing after the `=`) or `dtoverlay=none` ends the
current overlay context.  Any `dtparam` lines that follow are then
applied to the base DT only, with no overlay lookup:

```
dtoverlay=gpio-fan,gpiopin=14
dtoverlay=                       # end gpio-fan context

dtparam=audio=on                 # base-DT parameter, no overlay involved
```
