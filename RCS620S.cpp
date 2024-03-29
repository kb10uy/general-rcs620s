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

    uint8_t preBuffer[9];

    preBuffer[0] = 0x00;
    preBuffer[1] = 0x00;
    preBuffer[2] = 0xff;
    if (length <= 255) {
        preBuffer[3] = length;
        preBuffer[4] = uint8_t(-preBuffer[3]);
        write(preBuffer, 5);
    } else {
        preBuffer[3] = 0xff;
        preBuffer[4] = 0xff;
        preBuffer[5] = uint8_t((length >> 8) & 0xff);
        preBuffer[6] = uint8_t((length >> 0) & 0xff);
        preBuffer[7] = uint8_t(-(preBuffer[5] + preBuffer[6]));
        write(preBuffer, 8);
    }

    write(data, length);
    preBuffer[0] = checksum(data, length);
    preBuffer[1] = 0x00;
    write(preBuffer, 2);

    const auto readAck = read(buffer, 6);
    if (!readAck || memcmp(buffer, "\x00\x00\xff\x00\xff\x00", 6) != 0) {
        sendCancel();
        return Result::NoAck;
    }

    const auto readHead = read(buffer, 5);
    if (!readHead) {
        sendCancel();
        return Result::Failed;
    } else if (memcmp(buffer, "\x00\x00\xff", 3) != 0) {
        sendCancel();
        return Result::Invalid;
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

    const auto responseChecksum = checksum(buffer, bufferWritten);
    const auto readChecksum = read(preBuffer, 2);
    if (!readChecksum || preBuffer[0] != responseChecksum || preBuffer[1] != 0x00) {
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
 * InDataExchange コマンドを送信する。
 * response に this->buffer 及びその一部を指定してはいけない。
 */
RCS620S::Result
RCS620S::sendInDataExchange(const uint8_t *command, uint16_t commandLength, uint8_t *response, uint16_t *responseLength)
{
    memcpy(buffer, "\xd4\x40\x01", 3);
    memcpy(buffer + 3, command, commandLength);
    const auto result = sendRaw(buffer, commandLength + 3);
    if (result != Result::Success) {
        return result;
    } else if (bufferWritten < 3 || memcmp(buffer, "\xd5\x41\x00", 3) != 0) {
        return Result::Failed;
    }

    *responseLength = bufferWritten - 3;
    memcpy(response, buffer + 3, *responseLength);
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
 * Type B のポーリングを行う。
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

/**
 * TypeA Mifare Ultralight から 16byte 読み出す。
 */
RCS620S::Result RCS620S::readTypeA(uint8_t page, uint8_t *buffer) {
    uint8_t cmd[2], response[64];
    uint16_t len;
    cmd[0] = 0x30;
    cmd[1] = page;

    const auto result = sendInDataExchange(cmd, 2, response, &len);
    if (result != Result::Success) return result;
    if (len < 16) return Result::Invalid;
    memcpy(buffer, response, 16);
    return Result::Success;
}

/**
 *
 */
RCS620S::Result RCS620S::writeTypeA(uint8_t page, const uint8_t *data) {
    uint8_t cmd[6], response[64];
    uint16_t len;
    cmd[0] = 0xa2;
    cmd[1] = page;
    memcpy(cmd + 2, data, 4);

    const auto result = sendInDataExchange(cmd, 6, response, &len);
    if (result != Result::Success) return result;
    return Result::Success;
}
