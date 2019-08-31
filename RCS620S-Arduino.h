#pragma once

#include <Arduino.h>
#include "RCS620S.h"

class RCS620SArduino : public RCS620S
{
protected:
    HardwareSerial *serial;

    void initializeDevice() override;
    bool write(const uint8_t *data, uint16_t length) override;
    bool read(uint8_t *buffer, uint16_t length) override;
    void flush() override;
    void delayMillisecond(uint16_t time) override;

public:
    RCS620SArduino(HardwareSerial *hws) : serial(hws) {}
};
