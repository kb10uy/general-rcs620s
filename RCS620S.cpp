#include "RCS620S.h"
#include <string.h>

/**
 * チェックサムを計算する。
 */
uint8_t RCS620S::checksum(const uint8_t *data, uint16_t length) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < length; ++i) sum += data[i];
    return uint8_t(-(sum & 0xff));
}

/**
 * キャンセルコマンドを送信する。
 */
void RCS620S::sendCancel()
{
    write((const uint8_t *) "\x00\x00\xff\x00\xff\x00", 6);
    delayMillisecond(1);
    flush();
}

/**
 * 生データを送信する。
 * レスポンスは内部バッファに格納される。
 */
RCS620S::Result RCS620S::sendRaw(const uint8_t *data, uint16_t length)
{
    flush();

    buffer[0] = 0x00;
    buffer[1] = 0x00;
    buffer[2] = 0xff;
    if (length <= 255) {
        buffer[3] = length;
        buffer[4] = uint8_t(buffer[3]);
        write(buffer, 5);
    } else {
        buffer[3] = 0xff;
        buffer[4] = 0xff;
        buffer[5] = uint8_t((length >> 8) & 0xff);
        buffer[6] = uint8_t((length >> 8) & 0xff);
        buffer[7] = uint8_t(-(buffer[5] + buffer[6]));
        write(buffer, 8);
    }

    write(data, length);
    buffer[0] = checksum(data, length);
    buffer[1] = 0x00;
    write(buffer, 2);

    const auto readAck = read(buffer, 6);
    if (!readAck || memcmp(buffer, "\x00\x00\xff\x00\xff\x00", 6) != 0) {
        sendCancel();
        return Result::NoAck;
    }

    if (buffer[3] == 0xff && buffer[4] == 0xff) {
        const auto readHeader = read(buffer + 5, 3);
        if (!readHeader || ((buffer[5] + buffer[6] + buffer[7]) & 0xff) != 0) {
            return Result::Invalid;
        }
        bufferWritten = uint16_t(buffer[5] << 8) | uint16_t(buffer[6]);
    } else {
        if (((buffer[3] + buffer[4]) & 0xff) != 0) {
            return Result::Invalid;
        }
        bufferWritten = buffer[3];
    }

    if (bufferWritten > RCS620S_MAX_RESPONSE_SIZE) {
        return Result::Failed;
    }

    const auto readResponse = read(buffer, bufferWritten);
    if (!readResponse) {
        sendCancel();
        return Result::Failed;
    }

    uint8_t csbuf[2];
    const auto responseChecksum = checksum(buffer, bufferWritten);
    const auto readChecksum = read(csbuf, 2);
    if (!readChecksum || buffer[0] != responseChecksum || buffer[1] != 0x00) {
        sendCancel();
        return Result::Invalid;
    }

    return Result::Success;
}

/**
 * コマンドを送信する。
 * response に this->buffer 及びその一部を指定してはいけない。
 */
RCS620S::Result
RCS620S::sendCommand(const uint8_t *command, uint16_t commandLength, uint8_t *response, uint16_t *responseLength)
{
    const uint16_t commandTimeout = timeout >= 0x8000 ? 0xffff : timeout * 2;

    buffer[0] = 0xd4;
    buffer[1] = 0xa0;
    buffer[2] = uint8_t((commandTimeout >> 0) & 0xff);
    buffer[3] = uint8_t((commandTimeout >> 8) & 0xff);
    buffer[4] = uint8_t(commandLength + 1);
    memcpy(buffer + 5, command, commandLength);

    const auto result = sendRaw(buffer, commandLength + 5);
    if (result != Result::Success) {
        return result;
    } else if (bufferWritten < 4 || memcmp(buffer, "\xd5\xa1\x00", 3) != 0) {
        return Result::Invalid;
    } else if (bufferWritten != buffer[3] + 3) {
        return Result::Invalid;
    }

    *responseLength = uint8_t(buffer[3] - 1);
    memcpy(response, buffer + 4, *responseLength);
    return Result::Success;
}
