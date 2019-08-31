#pragma once

#include <stddef.h>
#include <stdint.h>

class RCS620S {
protected:
    virtual bool write(uint8_t *data, uint16_t length) = 0;
    virtual bool read(uint8_t *buffer, uint16_t *length) = 0;
    virtual void flush() = 0;
    virtual size_t available() = 0;
};
