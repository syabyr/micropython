# MicroPython for EFR32MG21 (Series 2)

A bare-metal MicroPython port for the EFR32MG21 (Cortex-M33, 802.15.4 radio),
tested on the EFR32MG21 Breakout Board REV 1.0 (EFR32MG21B010F1024IM32).

## Building

    make BOARD=efr32mg21_devboard

## External dependencies (not part of this repository)

The build references two SDK checkouts that live outside the tree, exposed
via symlinks (see `Makefile`):

- `lib/simplicity-sdk` -> Silicon Labs Simplicity SDK (emlib, device
  headers, CMSIS).  Only platform/ sources are used.
- `lib/efr32-base` -> https://github.com/syabyr/efr32_base (GCC startup
  files and linker scripts for EFR32; the simplicity SDK does not ship
  bare-metal GCC ones).

## Radio (IEEE 802.15.4)

`radio.c` drives the RAIL library in `rail/`.  This is **RAIL 2.11.3**
(librail from GSDK 3.1.1), vendored with its headers.  The 2.4 GHz OQPSK
802.15.4 PHY is provided by the library's internal standard-phys
configurator data; no external radio configurator output is compiled.

> Do not "upgrade" to the newer `librail_efr32xg21` (RAIL 2.19.x from GSDK
> 4.x) without on-hardware RX verification: on this port that library never
> terminates received frames (headers/addresses/CRC-region pass, but the
> RX FIFO fills to 512 bytes, radio wedges in RECEIVING and no
> RAIL_EVENT_RX_PACKET_RECEIVED is ever raised).  RAIL 2.11.3 works.
> See `docs/efr32/rx-802154-frame-termination-investigation.md`.

## Flashing

    make BOARD=efr32mg21_devboard flash   # via J-Link (flash.jlink)
