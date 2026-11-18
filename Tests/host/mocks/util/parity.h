#pragma once
inline unsigned parity_even_bit(unsigned char b) { return __builtin_parity(b); }
