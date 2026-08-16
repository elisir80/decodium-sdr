// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/NewcatDriver.h"
#include "hal/backends/audiorig/CatSerialPort.h"
#include "hal/HalLog.h"

#include <QElapsedTimer>
#include <QSerialPort>
#include <QThread>

namespace dsdr::hal::audiorig {

namespace {

/// I codici di modo newcat. La tabella è del protocollo, non nostra: la
/// riscriviamo qui perché è l'unico posto in cui i due vocabolari si toccano.
///
/// DATA-U e DATA-L diventano DigU e DigL — è la coppia giusta: i modi dati
/// della radio hanno la banda larga e nessuna elaborazione della voce, che è
/// esattamente ciò che DigU/DigL significano da noi.
struct ModeMapping
{
    char code;
    DemodMode mode;
};

constexpr ModeMapping kModes[] = {
    {'1', DemodMode::Lsb},
    {'2', DemodMode::Usb},
    {'3', DemodMode::Cw},
    {'4', DemodMode::Fm},
    {'5', DemodMode::Am},
    {'6', DemodMode::DigL},   // RTTY-LSB
    {'7', DemodMode::Cwr},
    {'8', DemodMode::DigL},   // DATA-LSB
    {'9', DemodMode::DigU},   // RTTY-USB
    {'A', DemodMode::Nfm},    // DATA-FM
    {'B', DemodMode::Nfm},    // FM-N
    {'C', DemodMode::DigU},   // DATA-USB
    {'D', DemodMode::Am},     // AM-N
    {'E', DemodMode::DigU},   // C4FM/PSK, per noi una portante dati
};

} // namespace

NewcatDriver::NewcatDriver() = default;
NewcatDriver::~NewcatDriver()
{
    close();
}

QList<int> NewcatDriver::candidateBaudRates() const
{
    // Dalla più probabile alla meno: 38400 è il valore che chi usa il CAT
    // imposta quasi sempre, 4800 è il predefinito di fabbrica di molte Yaesu.
    return {38400, 4800, 9600, 19200, 57600, 115200};
}

DemodMode NewcatDriver::modeFromCode(char code)
{
    for (const ModeMapping &m : kModes) {
        if (m.code == code)
            return m.mode;
    }
    return DemodMode::Usb;
}

char NewcatDriver::codeFromMode(DemodMode mode)
{
    switch (mode) {
    case DemodMode::Lsb:  return '1';
    case DemodMode::Usb:  return '2';
    case DemodMode::Cw:   return '3';
    case DemodMode::Cwr:  return '7';
    case DemodMode::Fm:   return '4';
    case DemodMode::Nfm:  return 'B';
    case DemodMode::Am:
    case DemodMode::Sam:  return '5';
    case DemodMode::DigU: return 'C';   // DATA-USB
    case DemodMode::DigL: return '8';   // DATA-LSB
    case DemodMode::Dsb:
    case DemodMode::Iq:
        // Non esistono su una radio tradizionale: si sceglie la cosa più
        // vicina invece di rifiutare, perché il canale è già stato creato e
        // lasciarlo senza modo sarebbe peggio.
        return '2';
    }
    return '2';
}

QString NewcatDriver::modelFromId(const QByteArray &reply)
{
    // `ID0670;` → 0670. Il numero è del modello, e ci serve per scegliere il
    // profilo (velocità, larghezze, device audio atteso).
    if (!reply.startsWith("ID") || reply.size() < 7)
        return QString();

    const QByteArray code = reply.mid(2, 4);
    if (code == "0670")
        return QStringLiteral("FT-991A");
    if (code == "0650")
        return QStringLiteral("FT-891");
    if (code == "0761")
        return QStringLiteral("FT-710");
    if (code == "0681")
        return QStringLiteral("FT-DX10");
    // Sconosciuta ma che risponde newcat: si usa lo stesso, dicendo il codice.
    return QStringLiteral("Yaesu %1").arg(QString::fromLatin1(code));
}

int NewcatDriver::probe(const QString &portName)
{
    // Una sola apertura per tutta la sonda. Ogni `open()` su Windows alza DTR
    // e RTS per qualche millisecondo prima che si possano abbassare, e su una
    // radio con «CAT RTS» attivo quelle linee sono il PTT: aprire sei volte
    // per provare sei velocità vuol dire sei colpi di trasmissione.
    CatSerialConfig serial;
    serial.baudRate = candidateBaudRates().value(0, 38400);
    serial.flowControl = 0;
    if (!openPort(portName, serial))
        return -1;

    for (int rate : candidateBaudRates()) {
        m_port->setBaudRate(rate);
        m_port->clear();

        if (identify()) {
            qCInfo(dsdrHal) << "newcat:" << m_model << "su" << portName
                            << rate << "baud";
            return rate;
        }
    }

    m_error = QStringLiteral("nessuna radio newcat su %1").arg(portName);
    close();
    return -1;
}

bool NewcatDriver::open(const QString &portName, const CatSerialConfig &serial)
{
    close();

    QList<CatSerialConfig> attempts;
    if (serial.flowControl == -1) {
        // L'automatico conserva il comportamento che già funzionava sul
        // campo: prima linee basse e nessun flow-control, poi RTS/CTS per
        // le Yaesu con «CAT RTS». Una scelta esplicita non viene cambiata.
        CatSerialConfig withoutFlow = serial;
        withoutFlow.flowControl = 0;
        attempts.append(withoutFlow);
        CatSerialConfig hardwareFlow = serial;
        hardwareFlow.flowControl = 1;
        attempts.append(hardwareFlow);
    } else {
        attempts.append(serial);
    }

    for (const CatSerialConfig &attempt : attempts) {
        if (!openPort(portName, attempt))
            return false;

        if (identify()) {
            qCInfo(dsdrHal) << "newcat:" << m_model << "su" << portName
                            << attempt.baudRate << "baud"
                            << (attempt.flowControl == 1 ? "con handshake RTS/CTS" : "");
            return true;
        }
        close();
    }

    m_error = QStringLiteral("nessuna radio newcat su %1 a %2 baud")
                  .arg(portName).arg(serial.baudRate);
    return false;
}

bool NewcatDriver::openPort(const QString &portName, const CatSerialConfig &serial)
{
    m_port = std::make_unique<QSerialPort>();
    m_port->setPortName(portName);
    if (!configureSerialPort(m_port.get(), serial, &m_error)) {
        m_port.reset();
        return false;
    }

    if (!m_port->open(QIODevice::ReadWrite)) {
        m_error = m_port->errorString();
        m_port.reset();
        return false;
    }

    if (!applySerialControlLines(m_port.get(), serial, &m_error)) {
        close();
        return false;
    }
    m_port->clear();
    return true;
}

bool NewcatDriver::identify()
{
    // Centocinquanta millisecondi e non trecento: una radio che c'è risponde
    // in venti, e questa attesa la si paga per ogni velocità di ogni porta che
    // radio non è.
    constexpr int kProbeTimeoutMs = 150;

    QByteArray id = ask("ID;", kProbeTimeoutMs);
    if (id.isEmpty()) {
        // Un comando lasciato a metà da un programma chiuso male tiene la
        // radio in attesa del resto, e la nostra domanda finisce dentro
        // quello. Il punto e virgola lo chiude.
        //
        // Si fa **solo dopo** un tentativo fallito, e non prima come faceva la
        // prima stesura: mandato a una radio che sta bene, il terminatore da
        // solo la fa ammutolire, e il difetto si presentava come «il CAT non
        // risponde alla velocità giusta e risponde a caso a quelle sbagliate».
        m_port->write(";");
        m_port->waitForBytesWritten(100);
        QThread::msleep(60);
        m_port->clear(QSerialPort::Input);
        id = ask("ID;", kProbeTimeoutMs);
    }

    m_model = modelFromId(id);
    if (m_model.isEmpty())
        return false;

    m_error.clear();
    return true;
}

void NewcatDriver::close()
{
    if (m_port && m_port->isOpen())
        m_port->close();
    m_port.reset();
    m_model.clear();
}

bool NewcatDriver::isOpen() const
{
    return m_port && m_port->isOpen();
}

QByteArray NewcatDriver::ask(const QByteArray &command, int timeoutMs)
{
    if (!isOpen())
        return {};

    m_port->clear(QSerialPort::Input);
    if (m_port->write(command) != command.size())
        return {};
    m_port->waitForBytesWritten(timeoutMs);

    QByteArray reply;
    QElapsedTimer clock;
    clock.start();
    while (!reply.endsWith(';')) {
        if (clock.elapsed() > timeoutMs) {
            qCDebug(dsdrHal) << "newcat: timeout su" << command << "ricevuto" << reply;
            return {};
        }
        if (m_port->waitForReadyRead(20))
            reply += m_port->readAll();
        if (reply.size() > 64)
            return {};
    }
    return reply;
}

bool NewcatDriver::tell(const QByteArray &command)
{
    if (!isOpen())
        return false;
    if (m_port->write(command) != command.size())
        return false;
    return m_port->waitForBytesWritten(300);
}

bool NewcatDriver::poll(CatState &state)
{
    if (!isOpen())
        return false;

    state.pttKnown = false;
    state.transmitting = false;

    // `FA` + nove cifre + `;` sono dodici caratteri, non tredici. Con la
    // soglia sbagliata la risposta veniva scartata sempre, la frequenza
    // restava a zero e il panadattatore non aveva dove ancorarsi — senza che
    // niente segnalasse un errore.
    const QByteArray fa = ask("FA;");
    if (fa.size() >= 12 && fa.startsWith("FA"))
        state.frequencyHz = fa.mid(2, 9).toLongLong();
    else if (fa.isEmpty())
        return false;   // la radio ha smesso di rispondere: CAT perso

    const QByteArray md = ask("MD0;");
    if (md.size() >= 5 && md.startsWith("MD"))
        state.mode = modeFromCode(md.at(3));

    const QByteArray tx = ask("TX;");
    if (tx.size() >= 4 && tx.startsWith("TX")) {
        state.transmitting = tx.at(2) != '0';
        state.pttKnown = true;
    }

    // L'S-meter si legge solo in ricezione: in trasmissione lo stesso comando
    // risponde con la potenza, e prenderlo per segnale farebbe schizzare il
    // misuratore a ogni PTT.
    if (!state.transmitting) {
        const QByteArray sm = ask("SM0;");
        if (sm.size() >= 7 && sm.startsWith("SM"))
            state.sMeterRaw = sm.mid(3, 3).toInt();
    }

    return true;
}

bool NewcatDriver::setFrequency(qint64 hz)
{
    if (hz < 0)
        return false;
    // Nove cifre con gli zeri davanti: `FA007100000;`.
    return tell(QByteArray("FA") + QByteArray::number(hz).rightJustified(9, '0') + ";");
}

bool NewcatDriver::setMode(DemodMode mode)
{
    return tell(QByteArray("MD0") + codeFromMode(mode) + ";");
}

bool NewcatDriver::setPtt(bool transmit)
{
    // TX1 trasmette per comando CAT, TX0 torna in ricezione. TX2 userebbe il
    // piedino PTT, che è la strada di chi ha un'interfaccia esterna: non è
    // questo il caso, e sceglierla qui lascerebbe la radio in ricezione senza
    // dire niente.
    return tell(transmit ? "TX1;" : "TX0;");
}

} // namespace dsdr::hal::audiorig
