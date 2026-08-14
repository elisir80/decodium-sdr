// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — traduzione sicura dei parametri seriali CAT in Qt.
//
// I valori arrivano dalla UI e possono anche provenire da configurazioni
// salvate di versioni precedenti. Non devono quindi diventare un valore Qt a
// caso: una riga seriale impostata male sembra una radio muta, non un errore.
#pragma once

#include "hal/backends/audiorig/ICatDriver.h"

#include <QSerialPort>
#include <QString>

namespace dsdr::hal::audiorig {

struct QtSerialPortConfig
{
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
};

inline bool toQtSerialPortConfig(const CatSerialConfig &serial,
                                 QtSerialPortConfig *result,
                                 QString *error = nullptr)
{
    if (!result) {
        if (error)
            *error = QStringLiteral("configurazione seriale assente");
        return false;
    }

    QtSerialPortConfig converted;
    switch (serial.dataBits) {
    case 5: converted.dataBits = QSerialPort::Data5; break;
    case 6: converted.dataBits = QSerialPort::Data6; break;
    case 7: converted.dataBits = QSerialPort::Data7; break;
    case 8: converted.dataBits = QSerialPort::Data8; break;
    default:
        if (error)
            *error = QStringLiteral("bit dati non validi: %1").arg(serial.dataBits);
        return false;
    }

    switch (serial.parity) {
    case 0: converted.parity = QSerialPort::NoParity; break;
    case 1: converted.parity = QSerialPort::EvenParity; break;
    case 2: converted.parity = QSerialPort::OddParity; break;
    case 3: converted.parity = QSerialPort::MarkParity; break;
    case 4: converted.parity = QSerialPort::SpaceParity; break;
    default:
        if (error)
            *error = QStringLiteral("parità non valida: %1").arg(serial.parity);
        return false;
    }

    switch (serial.stopBits) {
    case 1:  converted.stopBits = QSerialPort::OneStop; break;
    case 15: converted.stopBits = QSerialPort::OneAndHalfStop; break;
    case 2:  converted.stopBits = QSerialPort::TwoStop; break;
    default:
        if (error)
            *error = QStringLiteral("bit di stop non validi: %1").arg(serial.stopBits);
        return false;
    }

    switch (serial.flowControl) {
    case -1: // l'automatismo del driver parte prudente, senza flow-control.
    case 0:  converted.flowControl = QSerialPort::NoFlowControl; break;
    case 1:  converted.flowControl = QSerialPort::HardwareControl; break;
    case 2:  converted.flowControl = QSerialPort::SoftwareControl; break;
    default:
        if (error)
            *error = QStringLiteral("handshake non valido: %1").arg(serial.flowControl);
        return false;
    }

    *result = converted;
    return true;
}

inline bool configureSerialPort(QSerialPort *port, const CatSerialConfig &serial,
                                QString *error = nullptr)
{
    QtSerialPortConfig config;
    if (!port || !toQtSerialPortConfig(serial, &config, error))
        return false;
    if (serial.baudRate <= 0) {
        if (error)
            *error = QStringLiteral("velocità seriale non valida");
        return false;
    }

    const bool configured = port->setBaudRate(serial.baudRate)
        && port->setDataBits(config.dataBits)
        && port->setParity(config.parity)
        && port->setStopBits(config.stopBits)
        && port->setFlowControl(config.flowControl);
    if (!configured && error)
        *error = port->errorString();
    return configured;
}

inline bool applySerialControlLines(QSerialPort *port, const CatSerialConfig &serial,
                                    QString *error = nullptr)
{
    if (!port)
        return false;

    // Con RTS/CTS, Qt è il proprietario di RTS. Cercare di forzarlo dopo
    // l'apertura spezza l'handshake e su alcune Yaesu equivale a dire alla
    // radio di non rispondere più.
    const bool hardwareFlow = serial.flowControl == 1;
    if (!hardwareFlow && !port->setRequestToSend(serial.rts) && serial.rts) {
        if (error)
            *error = QStringLiteral("la porta non consente RTS alto: %1").arg(port->errorString());
        return false;
    }
    if (!port->setDataTerminalReady(serial.dtr) && serial.dtr) {
        if (error)
            *error = QStringLiteral("la porta non consente DTR alto: %1").arg(port->errorString());
        return false;
    }
    return true;
}

} // namespace dsdr::hal::audiorig
