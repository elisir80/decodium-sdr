// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/RadioScout.h"
#include "hal/HalLog.h"

#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

namespace dsdr::hal {

namespace {

// ── OpenHPSDR ───────────────────────────────────────────────────────────────
//
// Il protocollo è pubblico e sta nella documentazione OpenHPSDR. La chiamata è
// un pacchetto di 63 byte in broadcast sulla porta 1024: due byte di
// sincronismo, un comando, e il resto a zero. Chi risponde dice chi è.
constexpr quint16 kHpsdrPort = 1024;
constexpr char kHpsdrSync0 = char(0xEF);
constexpr char kHpsdrSync1 = char(0xFE);
constexpr char kHpsdrDiscover = 0x02;
constexpr int kHpsdrProbeSize = 63;

/// Il byte del modello, all'offset 10 della risposta. I numeri sono quelli
/// assegnati dal progetto OpenHPSDR: sono un elenco, non una formula.
QString hpsdrBoardName(quint8 id)
{
    switch (id) {
    case 0x00: return QStringLiteral("Metis");
    case 0x01: return QStringLiteral("Hermes");
    case 0x02: return QStringLiteral("Griffin");
    case 0x04: return QStringLiteral("Angelia");
    case 0x05: return QStringLiteral("Orion");
    case 0x06: return QStringLiteral("Hermes-Lite");
    case 0x0A: return QStringLiteral("Orion Mk2");
    default:   break;
    }
    // Sconosciuta ma che parla il protocollo: si dice il numero invece di
    // inventare un nome. Un modello uscito dopo di noi resta riconoscibile.
    return QStringLiteral("OpenHPSDR 0x%1")
        .arg(QString::number(id, 16).toUpper());
}

// ── FlexRadio serie 6000 ────────────────────────────────────────────────────
//
// La radio si annuncia da sé, una volta al secondo, in broadcast sulla porta
// 4992: un pacchetto VITA-49 il cui carico utile è testo, coppie
// `chiave=valore` separate da spazi. Non si chiede niente — si ascolta.
constexpr quint16 kFlexPort = 4992;

/// Estrae il valore di una chiave dal testo dell'annuncio.
QString flexField(const QString &payload, const QString &key)
{
    const QString needle = key + QLatin1Char('=');
    const int start = payload.indexOf(needle);
    if (start < 0)
        return QString();
    const int from = start + needle.size();
    int end = payload.indexOf(QLatin1Char(' '), from);
    if (end < 0)
        end = payload.size();
    return payload.mid(from, end - from).trimmed();
}

} // namespace

RadioScout::RadioScout(QObject *parent)
    : QObject(parent)
{
}

RadioScout::~RadioScout()
{
    stop();
}

QByteArray RadioScout::hpsdrProbe()
{
    QByteArray probe(kHpsdrProbeSize, '\0');
    probe[0] = kHpsdrSync0;
    probe[1] = kHpsdrSync1;
    probe[2] = kHpsdrDiscover;
    return probe;
}

ScoutedRadio RadioScout::parseHpsdrReply(const QByteArray &datagram,
                                         const QHostAddress &sender)
{
    ScoutedRadio radio;

    // Undici byte è il minimo per arrivare al modello. Più corta, la risposta
    // non è una risposta.
    if (datagram.size() < 11)
        return radio;
    if (datagram.at(0) != kHpsdrSync0 || datagram.at(1) != kHpsdrSync1)
        return radio;

    // 0x02 «libera», 0x03 «già in uso da qualcun altro». Entrambe sono
    // risposte, e la seconda è un'informazione che vale doppio: spiega perché
    // la radio non si apre.
    const auto status = static_cast<quint8>(datagram.at(2));
    if (status != 0x02 && status != 0x03)
        return radio;

    QStringList mac;
    for (int i = 3; i < 9; ++i) {
        mac << QStringLiteral("%1").arg(static_cast<quint8>(datagram.at(i)), 2, 16,
                                        QLatin1Char('0')).toUpper();
    }

    const auto firmware = static_cast<quint8>(datagram.at(9));
    const auto board = static_cast<quint8>(datagram.at(10));

    radio.family = QStringLiteral("OpenHPSDR");
    radio.model = hpsdrBoardName(board);
    radio.address = sender.toString();
    radio.identity = mac.join(QLatin1Char(':'));
    radio.detail = QStringLiteral("firmware %1.%2 · %3")
                       .arg(firmware / 10)
                       .arg(firmware % 10)
                       .arg(status == 0x03 ? QObject::tr("in uso da un altro programma")
                                           : QObject::tr("libera"));
    return radio;
}

ScoutedRadio RadioScout::parseFlexAnnounce(const QByteArray &datagram,
                                           const QHostAddress &sender)
{
    ScoutedRadio radio;

    // Il carico utile è testo dentro un involucro binario: invece di
    // decodificare l'intestazione VITA-49 — che cambia fra le versioni del
    // protocollo — si cerca il testo. È più robusto, ed è tutto ciò che serve.
    const int start = datagram.indexOf("model=");
    if (start < 0)
        return radio;

    const QString payload = QString::fromLatin1(datagram.mid(start));
    const QString model = flexField(payload, QStringLiteral("model"));
    if (model.isEmpty())
        return radio;

    const QString serial = flexField(payload, QStringLiteral("serial"));
    const QString nickname = flexField(payload, QStringLiteral("nickname"));
    const QString version = flexField(payload, QStringLiteral("version"));
    const QString status = flexField(payload, QStringLiteral("status"));
    const QString ip = flexField(payload, QStringLiteral("ip"));

    radio.family = QStringLiteral("FlexRadio");
    radio.model = model;
    radio.address = ip.isEmpty() ? sender.toString() : ip;
    // Il numero di serie se c'è; altrimenti l'indirizzo, che è meno stabile ma
    // meglio di niente: senza identità la stessa radio comparirebbe una volta
    // al secondo per tutta la durata dell'ascolto.
    radio.identity = serial.isEmpty() ? radio.address : serial;

    QStringList detail;
    if (!nickname.isEmpty())
        detail << nickname;
    if (!version.isEmpty())
        detail << QStringLiteral("SmartSDR %1").arg(version);
    if (!status.isEmpty())
        detail << status;
    radio.detail = detail.join(QStringLiteral(" · "));
    return radio;
}

void RadioScout::start(int seconds)
{
    stop();
    m_seen.clear();

    // ── OpenHPSDR: si chiede ────────────────────────────────────────────
    m_hpsdr = std::make_unique<QUdpSocket>();
    if (m_hpsdr->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress)) {
        connect(m_hpsdr.get(), &QUdpSocket::readyRead, this, &RadioScout::readHpsdr);
    } else {
        qCWarning(dsdrHal) << "scout: socket HPSDR non aperto:" << m_hpsdr->errorString();
        m_hpsdr.reset();
    }

    // ── FlexRadio: si ascolta ───────────────────────────────────────────
    //
    // ShareAddress e ReuseAddressHint perché su quella porta c'è quasi sempre
    // anche SmartSDR: prendersela in esclusiva significherebbe far sparire la
    // radio dal programma del costruttore, che è un modo pessimo di farsi
    // installare.
    m_flex = std::make_unique<QUdpSocket>();
    if (m_flex->bind(QHostAddress::AnyIPv4, kFlexPort,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(m_flex.get(), &QUdpSocket::readyRead, this, &RadioScout::readFlex);
    } else {
        qCWarning(dsdrHal) << "scout: porta Flex non ascoltabile:" << m_flex->errorString();
        m_flex.reset();
    }

    // La chiamata si ripete: un pacchetto solo si perde con niente, e una
    // radio accesa mezzo secondo dopo di noi resterebbe invisibile.
    m_probeTimer = new QTimer(this);
    m_probeTimer->setInterval(1000);
    connect(m_probeTimer, &QTimer::timeout, this, &RadioScout::broadcastHpsdr);
    m_probeTimer->start();
    broadcastHpsdr();

    m_stopTimer = new QTimer(this);
    m_stopTimer->setSingleShot(true);
    connect(m_stopTimer, &QTimer::timeout, this, [this] {
        stop();
        emit finished();
    });
    m_stopTimer->start(seconds * 1000);
}

void RadioScout::stop()
{
    if (m_probeTimer) {
        m_probeTimer->stop();
        m_probeTimer->deleteLater();
        m_probeTimer = nullptr;
    }
    if (m_stopTimer) {
        m_stopTimer->stop();
        m_stopTimer->deleteLater();
        m_stopTimer = nullptr;
    }
    m_hpsdr.reset();
    m_flex.reset();
}

bool RadioScout::isScanning() const
{
    return m_probeTimer != nullptr;
}

void RadioScout::broadcastHpsdr()
{
    if (!m_hpsdr)
        return;

    const QByteArray probe = hpsdrProbe();
    m_hpsdr->writeDatagram(probe, QHostAddress::Broadcast, kHpsdrPort);

    // E anche sul broadcast di ogni interfaccia. Su una macchina con più reti
    // — una scheda cablata, una Wi-Fi, una virtuale di qualche macchina
    // ospite — il broadcast globale non esce sempre da tutte, e la radio sta
    // proprio su quella che non è stata scelta.
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (!interface.flags().testFlag(QNetworkInterface::IsUp)
            || !interface.flags().testFlag(QNetworkInterface::CanBroadcast))
            continue;
        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            if (entry.broadcast().isNull())
                continue;
            m_hpsdr->writeDatagram(probe, entry.broadcast(), kHpsdrPort);
        }
    }
}

void RadioScout::readHpsdr()
{
    while (m_hpsdr && m_hpsdr->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(m_hpsdr->pendingDatagramSize()), '\0');
        QHostAddress sender;
        m_hpsdr->readDatagram(datagram.data(), datagram.size(), &sender);
        report(parseHpsdrReply(datagram, sender));
    }
}

void RadioScout::readFlex()
{
    while (m_flex && m_flex->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(m_flex->pendingDatagramSize()), '\0');
        QHostAddress sender;
        m_flex->readDatagram(datagram.data(), datagram.size(), &sender);
        report(parseFlexAnnounce(datagram, sender));
    }
}

void RadioScout::report(const ScoutedRadio &radio)
{
    if (!radio.isValid())
        return;

    // Una radio si annuncia ogni secondo: senza questo, l'elenco si
    // riempirebbe della stessa riga.
    const QString key = radio.family + QLatin1Char('/') + radio.identity;
    if (m_seen.contains(key))
        return;
    m_seen.append(key);

    qCInfo(dsdrHal) << "scout:" << radio.family << radio.model
                    << "su" << radio.address << radio.detail;
    emit radioFound(radio);
}

} // namespace dsdr::hal
