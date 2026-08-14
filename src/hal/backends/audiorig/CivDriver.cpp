// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/CivDriver.h"
#include "hal/backends/audiorig/CatSerialPort.h"
#include "hal/HalLog.h"

#include <QElapsedTimer>
#include <QSerialPort>

namespace dsdr::hal::audiorig {

namespace {

constexpr char kPreamble = char(0xFE);
constexpr char kEnd = char(0xFD);

/// L'indirizzo del controllore. 0xE0 è la convenzione di Icom per un computer,
/// e le radio rispondono a chi si è annunciato con questo.
constexpr quint8 kControllerAddress = 0xE0;

/// Una cifra BCD per volta: nibble alto e nibble basso.
inline quint8 packBcd(int high, int low)
{
    return static_cast<quint8>(((high & 0x0F) << 4) | (low & 0x0F));
}

} // namespace

CivDriver::CivDriver() = default;
CivDriver::~CivDriver()
{
    close();
}

QList<int> CivDriver::candidateBaudRates() const
{
    // 19200 è il predefinito dell'IC-7300 e di gran parte delle Icom recenti;
    // 115200 lo impostano quelli che usano il CI-V per i dati.
    return {19200, 115200, 9600, 38400, 4800};
}

QList<quint8> CivDriver::candidateAddresses()
{
    return {0x94,   // IC-7300
            0x98,   // IC-7610
            0x8E,   // IC-7851 / IC-7850
            0xA2,   // IC-9700
            0xA4,   // IC-705
            0x7A,   // IC-9100
            0x60,   // IC-7600
            0x88};  // IC-7100
}

QString CivDriver::modelFromAddress(quint8 address)
{
    switch (address) {
    case 0x94: return QStringLiteral("IC-7300");
    case 0x98: return QStringLiteral("IC-7610");
    case 0x8E: return QStringLiteral("IC-7851");
    case 0xA2: return QStringLiteral("IC-9700");
    case 0xA4: return QStringLiteral("IC-705");
    case 0x7A: return QStringLiteral("IC-9100");
    case 0x60: return QStringLiteral("IC-7600");
    case 0x88: return QStringLiteral("IC-7100");
    default:   break;
    }
    // Una Icom che risponde a un indirizzo che non conosciamo si usa lo
    // stesso: il modello è un'etichetta, il protocollo è quello.
    return QStringLiteral("Icom 0x%1").arg(QString::number(address, 16).toUpper());
}

QByteArray CivDriver::frequencyToBcd(qint64 hz)
{
    QByteArray bcd(5, char(0));
    if (hz < 0)
        return bcd;

    // Dal byte meno significativo: unità e decine, poi centinaia e migliaia, e
    // così via. È l'ordine del protocollo, ed è l'inverso di come si scrive un
    // numero: invertirlo dà una frequenza plausibile — 14,074 che diventa
    // 47,041 — e la si attribuisce alla radio invece che al codice.
    qint64 value = hz;
    for (int i = 0; i < 5; ++i) {
        const int low = static_cast<int>(value % 10);
        value /= 10;
        const int high = static_cast<int>(value % 10);
        value /= 10;
        bcd[i] = static_cast<char>(packBcd(high, low));
    }
    return bcd;
}

qint64 CivDriver::bcdToFrequency(const QByteArray &bcd)
{
    if (bcd.size() < 5)
        return -1;

    qint64 hz = 0;
    qint64 scale = 1;
    for (int i = 0; i < 5; ++i) {
        const auto byte = static_cast<quint8>(bcd.at(i));
        const int low = byte & 0x0F;
        const int high = (byte >> 4) & 0x0F;
        // Una cifra maggiore di nove non è un numero: è un pacchetto
        // sbagliato, e restituire comunque un valore lo farebbe passare per
        // buono.
        if (low > 9 || high > 9)
            return -1;
        hz += low * scale;
        scale *= 10;
        hz += high * scale;
        scale *= 10;
    }
    return hz;
}

DemodMode CivDriver::modeFromCode(quint8 code)
{
    switch (code) {
    case 0x00: return DemodMode::Lsb;
    case 0x01: return DemodMode::Usb;
    case 0x02: return DemodMode::Am;
    case 0x03: return DemodMode::Cw;
    case 0x04: return DemodMode::DigL;   // RTTY, banda laterale inferiore
    case 0x05: return DemodMode::Fm;
    case 0x06: return DemodMode::Nfm;
    case 0x07: return DemodMode::Cwr;
    case 0x08: return DemodMode::DigU;   // RTTY-R
    case 0x17: return DemodMode::DigU;   // DV, per noi una portante dati
    default:   break;
    }
    return DemodMode::Usb;
}

quint8 CivDriver::codeFromMode(DemodMode mode)
{
    switch (mode) {
    case DemodMode::Lsb:  return 0x00;
    case DemodMode::Usb:  return 0x01;
    case DemodMode::Am:
    case DemodMode::Sam:  return 0x02;
    case DemodMode::Cw:   return 0x03;
    case DemodMode::Cwr:  return 0x07;
    case DemodMode::Fm:   return 0x05;
    case DemodMode::Nfm:  return 0x06;
    // I modi dati non hanno un codice proprio: su una Icom si trasmette in
    // banda laterale con l'ingresso audio USB, e il modo resta USB o LSB.
    // Mandare il codice della RTTY porterebbe la radio dove non si voleva.
    case DemodMode::DigU: return 0x01;
    case DemodMode::DigL: return 0x00;
    case DemodMode::Dsb:
    case DemodMode::Iq:   return 0x01;
    }
    return 0x01;
}

QByteArray CivDriver::buildFrame(quint8 radioAddress, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(kPreamble);
    frame.append(kPreamble);
    frame.append(static_cast<char>(radioAddress));
    frame.append(static_cast<char>(kControllerAddress));
    frame.append(payload);
    frame.append(kEnd);
    return frame;
}

int CivDriver::probe(const QString &portName)
{
    // Una sola apertura per tutta la sonda: ogni `open()` su Windows alza DTR
    // e RTS per qualche millisecondo, e su molte interfacce quelle linee sono
    // il PTT. Cinque velocità aperte cinque volte sono cinque colpi di
    // trasmissione su una radio che nessuno stava usando.
    CatSerialConfig serial;
    serial.baudRate = candidateBaudRates().value(0, 19200);
    if (!openPort(portName, serial))
        return -1;

    for (int rate : candidateBaudRates()) {
        m_port->setBaudRate(rate);
        m_port->clear();

        if (findAddress()) {
            qCInfo(dsdrHal) << "civ:" << m_model << "su" << portName
                            << rate << "baud, indirizzo"
                            << QString::number(m_address, 16);
            return rate;
        }
    }

    m_error = QStringLiteral("nessuna radio CI-V su %1").arg(portName);
    close();
    return -1;
}

bool CivDriver::open(const QString &portName, const CatSerialConfig &serial)
{
    close();
    if (!openPort(portName, serial))
        return false;
    if (findAddress()) {
        qCInfo(dsdrHal) << "civ:" << m_model << "su" << portName << serial.baudRate
                        << "baud, indirizzo" << QString::number(m_address, 16);
        return true;
    }
    m_error = QStringLiteral("nessuna radio CI-V su %1 a %2 baud")
                  .arg(portName).arg(serial.baudRate);
    close();
    return false;
}

bool CivDriver::openPort(const QString &portName, const CatSerialConfig &serial)
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

bool CivDriver::findAddress()
{
    // Senza l'indirizzo giusto la radio non risponde, e non c'è modo di
    // distinguere «indirizzo sbagliato» da «non è una Icom»: si provano.
    for (quint8 address : candidateAddresses()) {
        m_address = address;
        const QByteArray reply = ask(QByteArray(1, char(0x03)), 150);
        if (reply.size() >= 6 && static_cast<quint8>(reply.at(4)) == 0x03) {
            m_model = modelFromAddress(address);
            m_error.clear();
            return true;
        }
    }
    return false;
}

void CivDriver::close()
{
    if (m_port && m_port->isOpen())
        m_port->close();
    m_port.reset();
    m_model.clear();
    m_address = 0;
}

bool CivDriver::isOpen() const
{
    return m_port && m_port->isOpen();
}

QByteArray CivDriver::ask(const QByteArray &payload, int timeoutMs)
{
    if (!m_port || !m_port->isOpen())
        return {};

    const QByteArray frame = buildFrame(m_address, payload);
    m_port->clear(QSerialPort::Input);
    if (m_port->write(frame) != frame.size())
        return {};
    m_port->waitForBytesWritten(timeoutMs);

    QByteArray buffer;
    QElapsedTimer clock;
    clock.start();

    while (clock.elapsed() <= timeoutMs) {
        if (m_port->waitForReadyRead(20))
            buffer += m_port->readAll();

        // Le risposte arrivano una dopo l'altra, e la prima è quasi sempre la
        // nostra eco: sul bus tutti sentono tutti. Si scorrono i telai finché
        // non ne arriva uno indirizzato a noi — cioè con il controllore come
        // destinatario e la radio come mittente.
        int end = buffer.indexOf(kEnd);
        while (end >= 0) {
            const QByteArray incoming = buffer.left(end + 1);
            buffer.remove(0, end + 1);

            if (incoming.size() >= 6
                && static_cast<quint8>(incoming.at(2)) == kControllerAddress
                && static_cast<quint8>(incoming.at(3)) == m_address) {
                return incoming;
            }
            end = buffer.indexOf(kEnd);
        }

        if (buffer.size() > 256)
            return {};
    }
    return {};
}

bool CivDriver::tell(const QByteArray &payload)
{
    if (!m_port || !m_port->isOpen())
        return false;

    // Anche i comandi di scrittura hanno una risposta — 0xFB se accettato,
    // 0xFA se rifiutato — e leggerla è l'unico modo di sapere se la radio ha
    // fatto quello che le si è chiesto.
    const QByteArray reply = ask(payload);
    if (reply.size() < 6)
        return false;
    return static_cast<quint8>(reply.at(4)) == 0xFB;
}

bool CivDriver::poll(CatState &state)
{
    if (!isOpen())
        return false;

    const QByteArray frequency = ask(QByteArray(1, char(0x03)));
    if (frequency.isEmpty())
        return false;   // la radio ha smesso di rispondere: CAT perso
    if (frequency.size() >= 10 && static_cast<quint8>(frequency.at(4)) == 0x03) {
        const qint64 hz = bcdToFrequency(frequency.mid(5, 5));
        if (hz > 0)
            state.frequencyHz = hz;
    }

    const QByteArray mode = ask(QByteArray(1, char(0x04)));
    if (mode.size() >= 7 && static_cast<quint8>(mode.at(4)) == 0x04)
        state.mode = modeFromCode(static_cast<quint8>(mode.at(5)));

    QByteArray pttQuery;
    pttQuery.append(char(0x1C));
    pttQuery.append(char(0x00));
    const QByteArray ptt = ask(pttQuery);
    if (ptt.size() >= 8 && static_cast<quint8>(ptt.at(4)) == 0x1C)
        state.transmitting = static_cast<quint8>(ptt.at(6)) != 0x00;

    // L'S-meter si legge solo in ricezione, come sulle Yaesu: in trasmissione
    // lo stesso comando parla di potenza.
    if (!state.transmitting) {
        QByteArray meterQuery;
        meterQuery.append(char(0x15));
        meterQuery.append(char(0x02));
        const QByteArray meter = ask(meterQuery);
        if (meter.size() >= 9 && static_cast<quint8>(meter.at(4)) == 0x15) {
            // Due byte BCD, da 0000 a 0255: le cifre stanno nei nibble, non
            // nel valore del byte. Leggere i byte come numeri darebbe un
            // misuratore che salta.
            const auto high = static_cast<quint8>(meter.at(6));
            const auto low = static_cast<quint8>(meter.at(7));
            state.sMeterRaw = ((high >> 4) & 0x0F) * 1000 + (high & 0x0F) * 100
                            + ((low >> 4) & 0x0F) * 10 + (low & 0x0F);
        }
    }

    return true;
}

bool CivDriver::setFrequency(qint64 hz)
{
    if (hz < 0)
        return false;
    QByteArray payload;
    payload.append(char(0x05));
    payload.append(frequencyToBcd(hz));
    return tell(payload);
}

bool CivDriver::setMode(DemodMode mode)
{
    QByteArray payload;
    payload.append(char(0x06));
    payload.append(static_cast<char>(codeFromMode(mode)));
    return tell(payload);
}

bool CivDriver::setPtt(bool transmit)
{
    QByteArray payload;
    payload.append(char(0x1C));
    payload.append(char(0x00));
    payload.append(static_cast<char>(transmit ? 0x01 : 0x00));
    return tell(payload);
}

} // namespace dsdr::hal::audiorig
