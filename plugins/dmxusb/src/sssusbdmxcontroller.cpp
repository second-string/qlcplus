/*
  Q Light Controller Plus
  enttecdmxusbpro.cpp

  Copyright (C) Heikki Junnila, Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QDebug>

#include "sssusbdmxcontroller.h"

#define SSS_CONTROL_PACKET_START_CODE_LSB (const quint8)(SSS_ESTA_ID & 0xFF)
#define SSS_CONTROL_PACKET_START_CODE_MSB (const quint8)((SSS_ESTA_ID >> 8) & 0xFF)
#define SSS_CONTROL_PACKET_END_CODE_LSB (const quint8)((SSS_ESTA_ID & 0xFF) ^ 0xFF)
#define SSS_CONTROL_PACKET_END_CODE_MSB (const quint8)(((SSS_ESTA_ID >> 8) & 0xFF) ^ 0xFF)

#define SSS_MAX_PACKET_DATA_LENGTH (513)

//////////////////////////////////////////////////////////////////////////////////////

SSSUSBDMXControllerOutputThread::SSSUSBDMXControllerOutputThread(DMXInterface *iface)
    : QThread(NULL),
    iface(iface),
    aborted(false),
    controlPacketAvailable(false),
    dmxPacketAvailable(false)
{
}

SSSUSBDMXControllerOutputThread::~SSSUSBDMXControllerOutputThread()
{
    quitWhenReady();
    if (!wait(1000)) {
        qDebug() << "Unable to kill output thread! Forcing with terminate()";
        terminate();
        wait();
    }
}

void SSSUSBDMXControllerOutputThread::handleNewControlPacket(QByteArray packetBytes)
{
    controlPacket = packetBytes;
    controlPacketAvailable = true;
}

void SSSUSBDMXControllerOutputThread::handleDmxPacket(QByteArray packetBytes)
{
    dmxPacket = packetBytes;
    dmxPacketAvailable = true;
}

void SSSUSBDMXControllerOutputThread::quitWhenReady()
{
    aborted = true;
}

void SSSUSBDMXControllerOutputThread::reset()
{
    aborted = false;
}

void SSSUSBDMXControllerOutputThread::run()
{
    qDebug() << "SSS output dmx thread started...";

    int openAttempts = 0;
    while (!iface->isOpen() && !aborted && openAttempts < 20)
    {
        qDebug() << "output thread interface not open, spinning";
        openAttempts++;
        msleep(100);
    }

    if (openAttempts >= 20)
    {
        qDebug() << "Giving up trying to open output thread interface, aborting thread";
        return;
    }

    while (!aborted) {
        if (controlPacketAvailable)
        {
            controlPacketAvailable = false;
            if (iface->write(controlPacket))
            {
                qDebug() << "tx ctrl packet:";
                qDebug() << controlPacket;
            }
            else
            {
                qWarning() << Q_FUNC_INFO << "FTDI write failed in output thread";
            }

            controlPacket.clear();
        }
        else if (dmxPacketAvailable)
        {
            // Right now this is writing packets to device at the dmx output freq of qlc. In the future this should
            // only write on data change if device mode set to cadence (no need to overload device if it's doing the 
            // buffering)
            dmxPacketAvailable = false;
            if (!iface->write(dmxPacket))
            {
                qWarning() << Q_FUNC_INFO << "FTDI write failed in output thread";
            }

            dmxPacket.clear();
        }
        // TODO :: get rid of this
        else
        {
            msleep(20);
        }
    }

    qDebug() << "SSS output thread terminated";
}

//////////////////////////////////////////////////////////////////////////////////////

SSSUSBDMXControllerInputThread::SSSUSBDMXControllerInputThread(DMXInterface *iface)
    : QThread(NULL),
    iface(iface),
    aborted(false),
    expect_rx(false)
{
}

SSSUSBDMXControllerInputThread::~SSSUSBDMXControllerInputThread()
{
    quitWhenReady();
    if (!wait(1000)) {
        qDebug() << "Unable to kill input thread! Waiting forever to prevent locking the FTDI interface";
        // terminate();
        wait();
    }

    qDebug() << "Finished stopping and destructing input thread";
}

void SSSUSBDMXControllerInputThread::quitWhenReady()
{
    aborted = true;
}

void SSSUSBDMXControllerInputThread::reset()
{
    aborted = false;
}


void SSSUSBDMXControllerInputThread::run()
{
    qDebug() << "SSS input dmx thread started...";

    int openAttempts = 0;
    while (!iface->isOpen() && !aborted && openAttempts < 20)
    {
        qDebug() << "input thread interface not open, spinning";
        openAttempts++;
        msleep(100);
    }

    if (openAttempts >= 20)
    {
        qDebug() << "Giving up trying to open output thread interface, aborting thread";
        return;
    }

    bool byteReadSuccess = false;
    uint8_t byte = 0xFF;
    uint8_t startByteLsb;
    uint8_t startByteMsb;
    ControlByte_t controlByte;
    uint16_t dataLength;
    QByteArray packetData;
    uint8_t endByteLsb;
    uint8_t endByteMsb;
    while (!aborted)
    {
        if (!iface->isOpen())
        {
            msleep(50);
            continue;
        }

        // TODO :: FW sending start bytes little endian, make everything match across platforms
        startByteLsb = iface->readByte(&byteReadSuccess);
        if (!byteReadSuccess)
        {
            qDebug() << "readByte timed out, restarting input thread loop";
            continue;
        }

        if (startByteLsb != SSS_CONTROL_PACKET_START_CODE_LSB)
        {
            // qDebug() << "Read non-start code byte (0x" << Qt::hex << startByteLsb << "), bailing";
            continue;
        }

        startByteMsb = iface->readByte(&byteReadSuccess);
        if (!byteReadSuccess)
        {
            qDebug() << "readByte timed out waiting for  second start byte, restarting input thread loop";
            continue;
        }
        if (startByteMsb != SSS_CONTROL_PACKET_START_CODE_MSB)
        {
            qDebug() << "Valid first start byte, invalid second (0x" << Qt::hex << startByteMsb << "), bailing";
            continue;
        }

        controlByte = (ControlByte_t)iface->readByte(&byteReadSuccess);
        if (!byteReadSuccess)
        {
            qDebug() << "readByte timed out waiting for control byte, restarting input thread loop";
            continue;
        }
        if (controlByte >= CONTROL_BYTE_COUNT)
        {
            qDebug() << "Invalid control byte (0x" << Qt::hex << controlByte << "), bailing";
            continue;
        }

        uint8_t dataLengthLsb = iface->readByte(&byteReadSuccess);
        uint8_t dataLengthMsb = iface->readByte(&byteReadSuccess);
        dataLength = (dataLengthMsb << 8) | dataLengthLsb;
        if (!byteReadSuccess)
        {
            qDebug() << "readByte timed out waiting for data length, restarting input thread loop";
            continue;
        }
        if (dataLength > SSS_MAX_PACKET_DATA_LENGTH)
        {
            qDebug() << "Invalid data length (" << dataLength << "), bailing";
            continue;
        }

        if (dataLength)
        {
            packetData = iface->read(dataLength);
        }

        endByteLsb = iface->readByte(&byteReadSuccess);
        if (!byteReadSuccess)
        {
            qDebug() << "readByte timed out waiting for first end byte, restarting input thread loop";
            continue;
        }
        if (endByteLsb != SSS_CONTROL_PACKET_END_CODE_LSB)
        {
            qDebug() << "Read non-end code byte (0x" << Qt::hex << endByteLsb << "), bailing";
            continue;
        }

        endByteMsb = iface->readByte(&byteReadSuccess);
        if (!byteReadSuccess)
        {
            qDebug() << "readByte timed out waiting for last end byte, restarting input thread loop";
            continue;
        }
        if (endByteMsb != SSS_CONTROL_PACKET_END_CODE_MSB)
        {
            qDebug() << "Read non-end code byte (0x" << endByteMsb << "), bailing";
            continue;
        }

        if (controlByte == CONTROL_BYTE_DMX_DATA) {
            // qDebug() << "input thread: Emitting dmxPacketRxd";
            emit dmxPacketRxd(packetData);
        } else {
            qDebug() << "input thread: Emitting controlPacketRxd w/ control byte 0x" << Qt::hex << controlByte;
            emit controlPacketRxd(controlByte, packetData);
        }

        packetData.clear();
    }

    qDebug() << "SSS input thread terminated";
}

//////////////////////////////////////////////////////////////////////////////////////

SSSUSBDMXController::SSSUSBDMXController(DMXInterface *iface, qint32 outputLine, qint32 inputLine)
    : DMXUSBWidget(iface, outputLine, 15)
{
    // DMXWidget base constructor sets only 1 output and 0 inputs, manually set input number and m_inputBaseLine here
    m_inputBaseLine = inputLine;
    setInputsNumber(1);

    m_inputLines[m_inputBaseLine].m_universeData.fill(0, 513);

    outputThread = new SSSUSBDMXControllerOutputThread(iface);
    inputThread = new SSSUSBDMXControllerInputThread(iface);

}

SSSUSBDMXController::~SSSUSBDMXController()
{
    qDebug() << Q_FUNC_INFO;

    // Handle thread cleanup before deallocing mem
    close(m_inputBaseLine, true);
    close(m_outputBaseLine, false);
}

SSSUSBDMXController::Type SSSUSBDMXController::type() const
{
    return DMXUSBWidget::SSSUSBDMX;
}

void SSSUSBDMXController::handleControlPacketResponse(quint8 controlByte, QByteArray controlPacketData)
{
    qDebug() << "controller handle control response";
    rxdControlByte = (ControlByte_t)controlByte;
    rxdControlPacket = controlPacketData;
    emit unblock();
}

void SSSUSBDMXController::handleDmxIn(QByteArray dmxInBytes)
{
    int loopLength = dmxInBytes.length() < 513 ? dmxInBytes.length() : 513;

    if (dmxInBytes.length() == 0) {
        return;
    }

    for (int i = 1; i < dmxInBytes.length(); i++) {
        uchar newByte = dmxInBytes.at(i);
        uchar oldByte = m_inputLines[m_inputBaseLine].m_universeData[i - 1];

        if (newByte != oldByte) {
            // qDebug() << "Changed value at index" << i << "was" << oldByte << "is now" << newByte;
            m_inputLines[m_inputBaseLine].m_universeData[i - 1] = newByte;
            emit dmxInputValueChanged(UINT_MAX, m_inputBaseLine, i - 1, newByte);
        }
    }
}

bool SSSUSBDMXController::readConfig(DMXInterface *iface, ConfigResponse_t &configResponse)
{
    configResponse.configResponseVersion =iface->readByte();

    // TODO :: supported config response check
    qDebug() << "Config response version: " << Qt::hex << configResponse.configResponseVersion;

    configResponse.estaMan =  ((ushort) iface->readByte() << 8) | iface->readByte();
    configResponse.fwVersionMajor = iface->readByte();
    configResponse.fwVersionMinor = iface->readByte();
    configResponse.fwVersionPatch = iface->readByte();
    configResponse.txMode = iface->readByte();
    configResponse.txCadenceHz = iface->readByte();
    configResponse.deviceConfigVersion = iface->readByte();
    configResponse.currentTestPattern = iface->readByte();
    configResponse.breakTimeUs = iface->readByte() | ((ushort) iface->readByte() << 8);
    configResponse.mabTimeUs = iface->readByte() | ((ushort) iface->readByte() << 8);
    configResponse.serial = iface->read(8);
    configResponse.dataDirection = iface->readByte();

    qDebug() << "Received full valid config response packet:";
    qDebug() << "ESTA: 0x" << Qt::hex << configResponse.estaMan;
    qDebug() << "FW version: " << configResponse.fwVersionMajor << "." << configResponse.fwVersionMinor << "." << configResponse.fwVersionPatch;
    qDebug() << "TX Mode: " << configResponse.txMode;
    qDebug() << "TX Cadence (Hz): " << configResponse.txCadenceHz;
    qDebug() << "Device config version: " << configResponse.deviceConfigVersion;
    qDebug() << "Current test pattern: " << configResponse.currentTestPattern;
    qDebug() << "Break Time (uS): " << configResponse.breakTimeUs;
    qDebug() << "MAB Time (uS): " << configResponse.mabTimeUs;
    qDebug() << "Device serial: " << configResponse.serial;
    qDebug() << "Data direction: " << configResponse.dataDirection;

    return true;
}

bool SSSUSBDMXController::open(quint32 line, bool input) {
    if (!DMXUSBWidget::open(line, input))
    {
        qWarning() << "Failed to open base DMXUSBWidget class! Bailing";
        return close(line, input);
    }

    connect(this, &SSSUSBDMXController::queueControlPacket, outputThread, &SSSUSBDMXControllerOutputThread::handleNewControlPacket);
    connect(this, &SSSUSBDMXController::queueDmxPacket, outputThread, &SSSUSBDMXControllerOutputThread::handleDmxPacket);
    connect(inputThread, &SSSUSBDMXControllerInputThread::controlPacketRxd, this, &SSSUSBDMXController::handleControlPacketResponse);
    connect(inputThread, &SSSUSBDMXControllerInputThread::dmxPacketRxd, this, &SSSUSBDMXController::handleDmxIn);

    // Even if we're in output mode, we start the input thread to process any responses from commands/config reqs
    outputThread->reset();
    inputThread->reset();
    outputThread->start();
    inputThread->start();

    QByteArray packet;
    packet.append(SSS_CONTROL_PACKET_START_CODE_MSB);
    packet.append(SSS_CONTROL_PACKET_START_CODE_LSB);
    packet.append(CONTROL_BYTE_PING);
    packet.append((const char)0x00);
    packet.append((const char)0x00);
    packet.append(SSS_CONTROL_PACKET_END_CODE_MSB);
    packet.append(SSS_CONTROL_PACKET_END_CODE_LSB);

    QEventLoop localLoop;
    connect(this, &SSSUSBDMXController::unblock, &localLoop, &QEventLoop::quit);

    emit queueControlPacket(packet);
    localLoop.exec();

    qDebug() << "released in open func, set rxdpacket bytes:" << rxdControlPacket;
    if (rxdControlByte != CONTROL_BYTE_PING) {
        qDebug() << "Released in open(), but set rxdControlByte (0x" << rxdControlByte << ") does not equal expected PING byte (0x" << CONTROL_BYTE_PING << ")";
        return false;
    }

    qDebug() << "RXd ping response, configuring device as " << (input ? "input" : "output");

    const char data_dir = input ? 0x01 : 0x00;
    packet.clear();
    packet.append(SSS_CONTROL_PACKET_START_CODE_MSB);
    packet.append(SSS_CONTROL_PACKET_START_CODE_LSB);
    packet.append(CONTROL_BYTE_SET_DATA_DIRECTION);
    packet.append((const char)0x01);
    packet.append((const char)0x00);
    packet.append(data_dir);
    packet.append(SSS_CONTROL_PACKET_END_CODE_MSB);
    packet.append(SSS_CONTROL_PACKET_END_CODE_LSB);
    emit queueControlPacket(packet);

    // TODO :: write sync or cadence mode (ideally cadence) if line dir is output

    return true;
}

bool SSSUSBDMXController::close(quint32 line , bool input) {
    disconnect(this, &SSSUSBDMXController::queueControlPacket, outputThread, &SSSUSBDMXControllerOutputThread::handleNewControlPacket);
    disconnect(this, &SSSUSBDMXController::queueDmxPacket, outputThread, &SSSUSBDMXControllerOutputThread::handleDmxPacket);
    disconnect(inputThread, &SSSUSBDMXControllerInputThread::controlPacketRxd, this, &SSSUSBDMXController::handleControlPacketResponse);
    disconnect(inputThread, &SSSUSBDMXControllerInputThread::dmxPacketRxd, this, &SSSUSBDMXController::handleDmxIn);

    bool success = DMXUSBWidget::close(line, input);

    // Try to quit our threads first
    if (outputThread) {
        outputThread->quitWhenReady();
    }

    if (inputThread) {
        inputThread->quitWhenReady();
    }

    // The input thread is usually blocking a long time on the readByte call. This locks FTDIs internal mutex in their
    // driver, so this close call then hangs forever. Force a hangup of the interface by calling close, then clean up
    //  the threads after
    // if (!DMXUSBWidget::close(line, input))
    // {
    //     qWarning() << "Failed to open base DMXUSBWidget class! Bailing";
    //     return false;
    // }

    // if (inputThread)
    // {
    //     delete inputThread;
    //     inputThread = NULL;
    // }

    // if (outputThread)
    // {
    //     delete outputThread;
    //     outputThread = NULL;
    // }

    return success;
}

QString SSSUSBDMXController::uniqueName(ushort line, bool input) const
{
    return QString("%1 %2 - (%3)").arg(QObject::tr("SSS DMX Output"), QString::number(line + 0), serial());
}

bool SSSUSBDMXController::writeUniverse(quint32 universe, quint32 output, const QByteArray& data, bool dataChanged)
{
    (void)universe;

    if (!iface()->isOpen())
    {
        qDebug() << "writeUniverse called but ftdi iface not open, bailing!";
        return false;
    }

    quint32 devLine = output - m_outputBaseLine;
    if (devLine >= (quint32)outputsNumber())
    {
        qDebug() << "writeUniverse called with device line greater than device output count, bailing";
        return false;
    }

    int dataLen = data.length();
    if (dataLen == 0) {
        return false;
    }

    QByteArray dmxData = data;
    if (dataLen > 512) {
        dataLen = 512;
        dmxData.resize(512);
    }

    // DMX 0 start code
    dataLen++;

    txdDmxPacket.clear();
    txdDmxPacket.append(SSS_CONTROL_PACKET_START_CODE_MSB);
    txdDmxPacket.append(SSS_CONTROL_PACKET_START_CODE_LSB);
    txdDmxPacket.append(CONTROL_BYTE_DMX_DATA);
    txdDmxPacket.append(dataLen & 0xFF);
    txdDmxPacket.append((dataLen >> 8) & 0xFF);
    txdDmxPacket.append((const char)0x00);
    txdDmxPacket.append(dmxData);
    txdDmxPacket.append(SSS_CONTROL_PACKET_END_CODE_MSB);
    txdDmxPacket.append(SSS_CONTROL_PACKET_END_CODE_LSB);
    emit queueDmxPacket(txdDmxPacket);

    if (dataChanged) {
        qDebug() << "DMX output data changed";
    }

    return true;

}
