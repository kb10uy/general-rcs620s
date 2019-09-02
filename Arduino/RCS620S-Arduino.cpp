#include "RCS620S-Arduino.h"
#include <Arduino.h>

void RCS620SArduino::initializeDevice() {
    serial->begin(115200);
}

bool RCS620SArduino::write(const uint8_t *data, uint16_t length) {
    serial->write(data, length);
    return true;
}

bool RCS620SArduino::read(uint8_t *buffer, uint16_t length) {
    uint16_t read = 0;
    unsigned long start = millis();

    while (read < length) {
        if (millis() - start > timeout) {
            return false;
        }

        if (serial->available() > 0) {
            buffer[read] = serial->read();
            ++read;
        }
    }

    return true;
}

void RCS620SArduino::flush() {
    serial->flush();
}

void RCS620SArduino::delayMillisecond(uint16_t time) {
    delay(time);
}
