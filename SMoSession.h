#ifndef SMO_SESSION_H
#define SMO_SESSION_H

#include <stdint.h>

namespace SMoSession {
    // An idle host has six times the normal 5s AVRDUDE receive timeout.
    // Poll runs only between commands; a blocking operation cannot expire.
    const uint32_t kIdleTimeout = 30000UL;
    void Refresh();
    void Poll();
}

#endif
