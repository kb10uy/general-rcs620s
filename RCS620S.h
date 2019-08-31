#pragma once

#include <stddef.h>
#include <stdint.h>

#define RCS620S_MAX_RESPONSE_SIZE 265

class RCS620S
{
public:
    enum class Result
    {
        Success,
        Failed,
        NoAck,
        Invalid,
        NotFound,
    };

    enum class Type
    {
        Unknown,
        TypeAMifare,
        TypeAMifareUL,
        TypeB,
        TypeFFeliCa,
    };

protected:
    uint8_t buffer[265];
    uint16_t bufferWritten;
    uint16_t timeout;
    uint8_t idm[8];
    uint8_t pmm[8];
    uint8_t idmLength;
    Type type;

    // デバイス依存
    virtual bool write(const uint8_t *data, uint16_t length) = 0;
    virtual bool read(uint8_t *buffer, uint16_t length) = 0;
    virtual void flush() = 0;
    virtual size_t available() = 0;
    virtual void delayMillisecond(uint16_t time) = 0;
    virtual unsigned long currentMillisecond() = 0;

    // 制御用
    void sendCancel();
    Result sendRaw(const uint8_t *command, uint16_t length);

public:
    // getter
    inline Type detectedType() const { return type; }
    uint8_t manufactureIdLength() const { return idmLength; }
    const uint8_t *manufactureId() const { return idm; }
    const uint8_t *manufactureParameter() const { return pmm; }

    // コマンドを直接送信
    Result sendCommand(const uint8_t *command, uint16_t commandLength, uint8_t *response, uint16_t *responseLength);

    // 各種コマンド
    Result initialize();
    Result turnOffRadioField();
    Result pollingTypeA();
    Result pollingTypeB();
    Result pollingTypeF();

    // その他
    static uint8_t checksum(const uint8_t *data, uint16_t length);
};
