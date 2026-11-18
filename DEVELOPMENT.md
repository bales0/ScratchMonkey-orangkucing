# HVprog2 development environment

## Hardware and scope

This project targets the improved HVprog2 hardware, not an unmodified STK500.
The owner confirmed that the fitted controller is ATmega32. The external
crystal is configured for 7.3728 MHz, following Q1 in the supplied HVprog2
schematic. Its generic MEGA8535-P symbol does not identify the fitted MCU.

HVprog2-specific code already exists: SMO_LAYOUT_HVPROG2 selects voltage
control, reset polarity, direct parallel control/data ports, and timer logic.
ATmega32 already selects this layout in SMoConfig.h, so no firmware source
changes are needed for this setup. Runtime behavior has not been hardware-tested.

## Setup

PlatformIO Core 6.2.0 is installed on the setup machine. Open this directory
in VS Code with the recommended PlatformIO IDE extension. Alternatively,
install the pinned CLI into a local virtual environment:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\pio.exe run -e hvprog2
```

On Linux/macOS use .venv/bin/python and .venv/bin/pio. Dependencies are
downloaded on the first build and stored under .pio/packages. The configuration
pins Atmel AVR 5.1.0 and MightyCore 3.0.2. SPI comes with the core.

## Build

```powershell
pio run -e hvprog2
```

platformio.ini selects the actual ATmega32 at 7372800 Hz with MightyCore
standard pinout: PB0..7 = 0..7, PD0..7 = 8..15, PC0..7 = 16..23,
PA0..7 = 24..31. Source filtering includes SMo*.cpp and the generated
ScratchMonkey.ino.cpp translation unit; Tools and Tests are excluded.

If the Windows console reports UnicodeEncodeError, set this in that terminal:

```powershell
$env:PYTHONIOENCODING = "utf-8"
```

## Verified result

The complete firmware builds successfully for ATmega32 with AVR GCC 7.3.0,
MightyCore 3.0.2 and the configured 7.3728 MHz clock:

- Flash: 12950 / 32768 bytes (39.5%).
- Static RAM: 541 / 2048 bytes (26.4%); stack usage is additional.
- Output: .pio/build/hvprog2/firmware.hex and firmware.elf.
- Build log: .pio/hvprog2-build.log.

The previous flash overflow concerned the incorrectly selected ATmega8535 and
does not apply to this ATmega32 build. All existing protocols remain enabled.
The compiler reports existing warnings about expression parentheses and unused
variables. Hardware protocols, voltage control and runtime stack usage remain
unverified.

## Author reference and host compatibility

The [Atmel STK500 mod article](https://mewpro.cc/en/2016/01/18/atmel-stk500-mod/)
explicitly lists ATmega32 among replacement controllers and specifies MightyCore,
an external 7.3728 MHz clock and Standard pinout. These match platformio.ini.

For TPI/PDI, the article describes host-side AVRDUDE changes for XPROG support,
memory-size reporting and TPI fuse handling, with links to patched forks.
Firmware support alone does not establish compatibility with the installed
AVRDUDE version. That client has not been checked as part of this setup.

The article concerns a modified original STK500. Its component-removal steps
and J200 connector refer to that board; use the supplied HVprog2 schematic
for this board. Fuse settings must be derived for the fitted ATmega32 rather
than copied from another MCU example. No fuse configuration is automated here.

## Hardware operations

Upload uses a custom protocol without an upload command until the external
ISP programmer is identified. A build never programs the controller. No flash,
EEPROM, fuse or bootloader writes were performed. The schematic labels JP2
1-2 as SELF and 2-3 as OUT; use the proper controller-programming connection
when upload is eventually configured.

The firmware uses binary STK500-family commands at 115200 baud. A serial
monitor is not a text command console. Close it before a programming client
opens the port. Tests/Makefile and Tools/RunTest require Make, Ruby, AVRDUDE
and physical targets; they write flash, EEPROM and fuse bits. They were not
run during environment preparation. The legacy doc HTML files mainly cover
the original Arduino wiring.

## Upstream and host patch review

See [the compatibility review](doc/UPSTREAM_REVIEW.md) for the pinned upstream
and gist revisions, differences between the two AVRDUDE patches, and remaining
host compatibility checks. The HVprog2 fork remains the development baseline.

## References

- Owner-supplied HVprog2.jpg schematic, revision 1.4.
- [Atmel STK500 mod: configuration and AVRDUDE details](https://mewpro.cc/en/2016/01/18/atmel-stk500-mod/)
- [HVprog2 author documentation](https://mewpro.cc/en/category/hvprog2/)
- [MightyCore PlatformIO configuration](https://github.com/MCUdude/MightyCore/blob/master/PlatformIO.md)
