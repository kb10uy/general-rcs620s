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
    uint16_t bufferWritten = 0;
    uint16_t timeout = 1000;
    uint8_t idm[8];
    uint8_t pmm[8];
    uint8_t idmLength = 0;
    Type type = Type::Unknown;

    // デバイス依存
    virtual void initializeDevice() = 0;
    virtual bool write(const uint8_t *data, uint16_t length) = 0;
    virtual bool read(uint8_t *buffer, uint16_t length) = 0;
    virtual void flush() = 0;
    virtual void delayMillisecond(uint16_t time) = 0;

    // 制御用
    Result assertAck(Result previous);
    void sendCancel();
    Result sendRaw(const uint8_t *command, uint16_t length);

public:
    // getter
    inline Type detectedType() const { return type; }
    inline uint8_t manufactureIdLength() const { return idmLength; }
    inline const uint8_t *manufactureId() const { return idm; }
    inline const uint8_t *manufactureParameter() const { return pmm; }

    // setter
    void setTimeout(uint16_t time) { timeout = time; }

    // コマンドを直接送信
    Result sendCommunicateThruEx(const uint8_t *command, uint16_t commandLength, uint8_t *response, uint16_t *responseLength);
    Result sendPush(const uint8_t *data, uint16_t length);

    // 各種コマンド
    Result initialize();
    Result turnOffRF();
    Result pollingTypeA();
    Result pollingTypeB();
    Result pollingTypeF(uint16_t systemCode = 0xffff);

    // その他
    static uint8_t checksum(const uint8_t *data, uint16_t length);
};
