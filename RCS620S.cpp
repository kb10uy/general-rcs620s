#include "RCS620S.h"
#include <string.h>

/**
 * チェックサムを計算する。
 */
uint8_t RCS620S::checksum(const uint8_t *data, uint16_t length)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < length; ++i) sum += data[i];
    return uint8_t(-(sum & 0xff));
}

/**
 * タイムアウトしたかどうかをチェックする。
 */
bool RCS620S::checkTimeout(unsigned long start) { return (currentMillisecond() - start) >= timeout; }

/**
 * ACK の 2byte を確認する。
 */
RCS620S::Result RCS620S::assertAck(RCS620S::Result previous)
{
    if (previous != Result::Success) {
        return previous;
    } else if (bufferWritten != 2 || memcmp(buffer, "\xd5\x33", 2) != 0) {
        return Result::NoAck;
    }
    return Result::Success;
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
 * CommunicateThruEx コマンドを送信する。
 * response に this->buffer 及びその一部を指定してはいけない。
 */
RCS620S::Result RCS620S::sendCommunicateThruEx(const uint8_t *command,
                                               uint16_t commandLength,
                                               uint8_t *response,
                                               uint16_t *responseLength)
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

/**
 * Push コマンドを送信する。
 */
RCS620S::Result RCS620S::sendPush(const uint8_t *data, uint16_t length)
{
    if (length > 224) {
        return Result::Invalid;
    }

    buffer[0] = 0xb0;
    buffer[9] = length;
    memcpy(buffer + 1, idm, 8);
    memcpy(buffer + 10, data, length);

    uint8_t pushBuffer[RCS620S_MAX_RESPONSE_SIZE];
    uint16_t pushWritten;
    const auto pushResult = sendCommunicateThruEx(buffer, length + 10, pushBuffer, &pushWritten);
    if (pushResult != Result::Success) {
        return pushResult;
    } else if (pushWritten != 10 || pushBuffer[0] != 0xb1) {
        return Result::Invalid;
    } else if (memcmp(pushBuffer + 1, idm, 8) != 0 || pushBuffer[9] != length) {
        return Result::Invalid;
    }

    buffer[0] = 0xa4;
    buffer[9] = 0x00;
    memcpy(buffer + 1, idm, 8);

    const auto result = sendCommunicateThruEx(buffer, 10, pushBuffer, &pushWritten);
    if (result != Result::Success) {
        return result;
    } else if (pushWritten != 10 || pushBuffer[0] != 0xa5) {
        return Result::Invalid;
    } else if (memcmp(pushBuffer + 1, idm, 8) != 0 || pushBuffer[9] != 0x00) {
        return Result::Invalid;
    }

    delayMillisecond(1000);
    return Result::Success;
}

/**
 * RC-S620/S を初期化する。
 */
RCS620S::Result RCS620S::initialize()
{
    initializeDevice();

    // various timings
    const auto rvt = assertAck(sendRaw((const uint8_t *) "\xd4\x32\x02\x00\x00\x00", 6));
    if (rvt != Result::Success) return rvt;

    // max retries
    const auto rmr = assertAck(sendRaw((const uint8_t *) "\xd4\x32\x05\x00\x00\x00", 6));
    if (rmr != Result::Success) return rmr;

    // additional wait 24ms
    const auto rawt = assertAck(sendRaw((const uint8_t *) "\xd4\x32\x81\xb7", 4));
    if (rawt != Result::Success) return rawt;

    return Result::Success;
}

/**
 * 搬送波をオフにする。
 */
RCS620S::Result RCS620S::turnOffRF()
{
    const auto result = assertAck(sendRaw((const uint8_t *) "\xd4\x32\x01\x00", 4));
    return result;
}

/**
 * Type A のポーリングを行う。
 */
RCS620S::Result RCS620S::pollingTypeA()
{
    type = Type::Unknown;

    const auto pollResult = sendRaw((const uint8_t *) "\xd4\x4a\x01\x00", 4);
    if (pollResult != Result::Success) {
        return pollResult;
    } else if (bufferWritten < 12) {
        return Result::NotFound;
    } else if (memcmp(buffer, "\xd5\x4b\x01\x01\x00", 5) != 0) {
        return Result::NotFound;
    }

    type = Type::TypeAMifare;
    if (memcmp(buffer + 4, "\x00\x44\x00\x07", 4) == 0) {
        type = Type::TypeAMifareUL;
    }

    idmLength = buffer[7];
    memcpy(idm, buffer + 8, idmLength);
    return Result::Success;
}

/**
 * Type A のポーリングを行う。
 */
RCS620S::Result RCS620S::pollingTypeB()
{
    type = Type::Unknown;

    const auto pollResult = sendRaw((const uint8_t *) "\xd4\x4a\x01\x03\x00", 5);
    if (pollResult != Result::Success) {
        return pollResult;
    } else if (bufferWritten <= 3 && (buffer[0] == 0x7f || buffer[2] != 0x00)) {
        return Result::NotFound;
    } else if (bufferWritten < 18) {
        return Result::NotFound;
    } else if (memcmp(buffer, "\xd5\x4b\x01\x01", 4) != 0) {
        return Result::NotFound;
    }

    type = Type::TypeB;

    idmLength = 4;
    memcpy(idm, buffer + 5, 4);
    return Result::Success;
}

/**
 * Type F のポーリングを行う。
 */
RCS620S::Result RCS620S::pollingTypeF(uint16_t systemCode)
{
    type = Type::Unknown;

    memcpy(buffer, "\xd4\x4a\x01\x01\x00\xff\xff\x00\x00", 9);
    buffer[5] = uint8_t((systemCode >> 8) & 0xff);
    buffer[6] = uint8_t((systemCode >> 0) & 0xff);

    const auto pollResult = sendRaw(buffer, 9);
    if (pollResult != Result::Success) {
        return pollResult;
    } else if (bufferWritten != 22) {
        return Result::NotFound;
    } else if (memcmp(buffer, "\xd5\x4b\x01\x01\x12\x01", 6) != 0) {
        return Result::NotFound;
    }

    type = Type::TypeFFeliCa;

    idmLength = 8;
    memcpy(idm, buffer + 6, 8);
    memcpy(pmm, buffer + 14, 8);
    return Result::Success;
}

