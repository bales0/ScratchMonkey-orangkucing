ScratchMonkey
=============

Arduino software programmer sketch, supporting ISP, HVSP, and HVPP

For details, please consult the [User Manual](http://microtherion.github.com/ScratchMonkey/)

I'm still putting the finishing touches on the release, so while all 
three programming methods have been tested briefly, this has not
been done in sufficient detail for me to be truly comfortable with
letting others try it. But hey, this is open source, so go ahead if
you like living dangerously!

## HVprog2 development

This fork includes support for the improved HVprog2 hardware. The local
development target is ATmega32 at 7.3728 MHz.

See [DEVELOPMENT.md](DEVELOPMENT.md) for setup and build instructions. The full
firmware builds successfully: 12950 bytes of flash and 541 bytes of static RAM.
