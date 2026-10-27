# RP1 Ethernet PHC PPS support

`rp1-ptp-pps` configures the PTP hardware clock on the RP1 Ethernet MAC through
Linux's standard PTP interface. The command-line utility uses the kernel's
`testptp` tool. It supports selected GPIO input and output, including a mode
with two independent rising-edge inputs and one simultaneous periodic output.

The PHC driver must report two external timestamp channels, one periodic
output channel, and 28 programmable pins. The runtime checks those capabilities
before it maps a pin. The kernel support is under review in
[raspberrypi/linux PR 7659](https://github.com/raspberrypi/linux/pull/7659).

GPIO numbers are BCM/RP1 GPIO numbers 0 through 27, not physical header pin
numbers. GPIO18 is physical pin 12, GPIO23 is pin 16, and GPIO24 is pin 18.
Signals connected to a Pi GPIO must remain between 0 and 3.3 V. Do not connect
two actively driven output pins together.

Build and install from the `utils` repository:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

The standalone command prints a plan without changing hardware. Add
`--execute` only after checking that the selected pins are wired and available:

```sh
sudo rp1-pps --mode duplex --input-gpio 18 --input-gpio2 24 \
  --output-gpio 23 --events 8 --execute
```

To run continuously under systemd, copy
`/usr/share/doc/rp1-ptp-pps/examples/rp1-pps.conf.example` to
`/etc/default/rp1-pps`, edit the PHC interface and BCM GPIOs, then enable
exactly one service. The example values use GPIO18 and prepare GPIO24 and
GPIO23 for duplex mode. For duplex operation, enable
`rp1-pps-duplex.service`:

```sh
sudo cp /usr/share/doc/rp1-ptp-pps/examples/rp1-pps.conf.example \
  /etc/default/rp1-pps
sudoedit /etc/default/rp1-pps
sudo systemctl daemon-reload
sudo systemctl enable --now rp1-pps-duplex.service
```

The second input uses EXTTS channel 1, which currently requires active PEROUT.
Disable the service before removing or rewiring its GPIO connections. Rising
edges are used by the simultaneous counter path. Its edge timing has not been
independently calibrated, so this software makes no board-level accuracy or
jitter guarantee. Physical validation currently covers only earlier
sequential tests; simultaneous two-input capture is still under qualification.
