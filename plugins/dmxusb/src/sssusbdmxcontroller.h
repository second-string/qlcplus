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

#ifndef SSSUSBDMXCONTROLLER_H
#define SSSUSBDMXCONTROLLER_H

#include "dmxusbwidget.h"

#define SSS_ESTA_ID (0x09B6)

typedef enum {
    CONTROL_BYTE_PING = 0x00,                      // 0x00
    CONTROL_BYTE_REBOOT,                           // 0x01
    CONTROL_BYTE_RESET_CONFIG,                     // 0x02
    CONTROL_BYTE_GET_CONFIG,                       // 0x03
                                                   //
    CONTROL_BYTE_DMX_DATA = 0x10,                  // 0x10
                                                   //
    CONTROL_BYTE_SET_DATA_DIRECTION = 0x20,        // 0x20
    CONTROL_BYTE_SET_TX_MODE,                      // 0x21
    CONTROL_BYTE_SET_RX_MODE,                      // 0x22
    CONTROL_BYTE_TX_CADENCE,                       // 0x23
    CONTROL_BYTE_SET_BREAK_TIME,                   // 0x24
    CONTROL_BYTE_SET_MAB_TIME,                     // 0x25
    CONTROL_BYTE_SET_TEST_MODE_TYPE,               // 0x26
    CONTROL_BYTE_SET_STATIC_DMX_PACKET,            // 0x27
    CONTROL_BYTE_SET_INPUT_COMPAT_MODE,            // 0x28
                                                   //
    CONTROL_BYTE_ENTTEC_GET_WIDGET_PARAMS = 0x30,  // 0x30
    CONTROL_BYTE_ENTTEC_GET_WIDGET_SERIAL = 0x31,  // 0x31
                                                   //
    CONTROL_BYTE_SET_SERIAL = 0xE0,                // 0xE0
    CONTROL_BYTE_ENTER_BL,                         // 0xE1

    CONTROL_BYTE_COUNT,
} ControlByte_t;


class SSSUSBDMXControllerOutputThread : public QThread
{
    Q_OBJECT
    public:
        SSSUSBDMXControllerOutputThread(DMXInterface *iface);
        virtual ~SSSUSBDMXControllerOutputThread();
        void quitWhenReady();
        void reset();

    public slots:
        void handleNewControlPacket(QByteArray packetBytes);
        void handleDmxPacket(QByteArray packetBytes);

    protected:
        void run() override;

    private:
        DMXInterface *iface;
        bool aborted;
        bool controlPacketAvailable;
        bool dmxPacketAvailable;
        QByteArray controlPacket;
        QByteArray dmxPacket;
};

class SSSUSBDMXControllerInputThread : public QThread
{
    Q_OBJECT
    public:
        SSSUSBDMXControllerInputThread(DMXInterface *iface);
        virtual ~SSSUSBDMXControllerInputThread();
        void quitWhenReady();
        void reset();

    signals:
        void controlPacketRxd(quint8 controlByte, QByteArray packetBytes);
        void dmxPacketRxd(QByteArray packetBytes);

    protected:
        void run() override;

    private:
        DMXInterface *iface;
        bool aborted;
        bool expect_rx;
};

class SSSUSBDMXController : public QObject, public DMXUSBWidget
{
    Q_OBJECT

    public:
        SSSUSBDMXController(DMXInterface *iface, qint32 outputLine, qint32 inputLine);
        virtual ~SSSUSBDMXController();

        Type type() const override;
        bool open(quint32 line = 0, bool input = false) override;
        bool close(quint32 line = 0, bool input = false) override;
        /*
        bool isOpen();
        void setOutputsNumber(int num);
        int outputsNumber();
        int openOutputLines();
        QStringList outputNames();
        int outputFrequency();
        void setOutputFrequency(int frequency);
        void setInputsNumber(int num);
        int inputsNumber();
        QStringList inputNames();
        int openInputLines();
        */
        // QString serial() const;
        // QString name() const;
        QString uniqueName(ushort line = 0, bool input = false) const override;
        // QString realName() const;
        // QString vendor() const;
        // QString additionalInfo() const { return QString(); }
        bool writeUniverse(quint32 universe, quint32 output, const QByteArray& data, bool dataChanged) override;

    public slots:
        void handleControlPacketResponse(quint8 controlByte, QByteArray controlPacketData);
        void handleDmxIn(QByteArray dmxInBytes);

    signals:
        void queueControlPacket(QByteArray packetBytes);
        void queueDmxPacket(QByteArray packetBytes);
        void dmxInputValueChanged(quint32 universe, quint32 input, quint32 channel, uchar value);

        // Private signal to allow us to send a packet, get the reponse in our slot, then unblock ourselves with new response saved
        void unblock();

    private:

        typedef struct {
            // quint8 startByteMsb;
            // quint8 startByteLsb;
            quint8 configResponseVersion;
            quint16  estaMan;
            quint8  fwVersionMajor;
            quint8  fwVersionMinor;
            quint8  fwVersionPatch;
            quint8  txMode;
            quint8  txCadenceHz;
            quint8  deviceConfigVersion;
            quint8  currentTestPattern;
            quint16 breakTimeUs;
            quint16 mabTimeUs;
            QByteArray     serial;
            quint8  dataDirection;
            // quint8 endByteMsb;
            // quint8 endByteLsb;
        } ConfigResponse_t;

        // typedef struct {
        //     quint8 startByteMsb;
        //     quint8 startByteLsb;
        //     ControlByte_t controlByte;
        //     quint16 dataLen;
        //     QByteArray data;
        //     quint8 endByteMsb;
        //     quint8 endByteLsb;
        // } Packet_t;

        SSSUSBDMXControllerOutputThread *outputThread;
        SSSUSBDMXControllerInputThread *inputThread;

        ControlByte_t rxdControlByte;
        QByteArray rxdControlPacket;
        QByteArray txdDmxPacket;

        bool readConfig(DMXInterface *iface, ConfigResponse_t &configResponse);
};

#endif
