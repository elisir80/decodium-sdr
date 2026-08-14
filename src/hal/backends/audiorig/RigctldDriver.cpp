// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/RigctldDriver.h"
#include "hal/HalLog.h"

#include <QElapsedTimer>
#include <QTcpSocket>

#include <algorithm>
#include <limits>

namespace dsdr::hal::audiorig {

namespace {

/// La porta di fabbrica di `rigctld`.
constexpr quint16 kDefaultPort = 4532;

/// Quanto si aspetta la connessione. Su 127.0.0.1 è immediata; da un'altra
/// macchina della rete di casa, mezzo secondo è già molto. Oltre non ha senso
/// aspettare: se `rigctld` non c'è, il sistema rifiuta subito, e se c'è ma non
/// risponde in mezzo secondo il polling a 5 Hz non funzionerebbe comunque.
constexpr int kConnectTimeoutMs = 600;

/// Il vocabolario dei modi di Hamlib e il nostro.
///
/// La tabella è del protocollo, non nostra: sta qui perché è l'unico punto in
/// cui i due vocabolari si toccano. `PKTUSB`/`PKTLSB` sono i modi «pacchetto»,
/// cioè la banda larga senza elaborazione della voce: è esattamente ciò che
/// DigU e DigL significano da noi, e sono quelli che una radio moderna usa per
/// le digitali. `RTTY`/`RTTYR` finiscono negli stessi, perché anche quelli
/// sono una portante dati e non una voce.
struct ModeMapping
{
    const char *name;
    DemodMode mode;
};

constexpr ModeMapping kModes[] = {
    {"USB", DemodMode::Usb},
    {"LSB", DemodMode::Lsb},
    {"CW", DemodMode::Cw},
    {"CWR", DemodMode::Cwr},
    {"AM", DemodMode::Am},
    {"AMS", DemodMode::Sam},
    {"FM", DemodMode::Fm},
    {"FMN", DemodMode::Nfm},
    {"WFM", DemodMode::Fm},
    {"DSB", DemodMode::Dsb},
    {"PKTUSB", DemodMode::DigU},
    {"PKTLSB", DemodMode::DigL},
    {"PKTFM", DemodMode::Nfm},
    {"RTTY", DemodMode::DigL},    // RTTY di Hamlib è su banda laterale inferiore
    {"RTTYR", DemodMode::DigU},
};

} // namespace

RigctldDriver::RigctldDriver() = default;

RigctldDriver::~RigctldDriver()
{
    close();
}

DemodMode RigctldDriver::modeFromName(const QString &name)
{
    const QString wanted = name.trimmed().toUpper();
    for (const ModeMapping &m : kModes) {
        if (wanted == QLatin1String(m.name))
            return m.mode;
    }
    return DemodMode::Usb;
}

QString RigctldDriver::nameFromMode(DemodMode mode)
{
    switch (mode) {
    case DemodMode::Usb:  return QStringLiteral("USB");
    case DemodMode::Lsb:  return QStringLiteral("LSB");
    case DemodMode::Cw:   return QStringLiteral("CW");
    case DemodMode::Cwr:  return QStringLiteral("CWR");
    case DemodMode::Am:   return QStringLiteral("AM");
    case DemodMode::Sam:  return QStringLiteral("AMS");
    case DemodMode::Fm:   return QStringLiteral("FM");
    case DemodMode::Nfm:  return QStringLiteral("FM");
    case DemodMode::Dsb:  return QStringLiteral("DSB");
    case DemodMode::DigU: return QStringLiteral("PKTUSB");
    case DemodMode::DigL: return QStringLiteral("PKTLSB");
    case DemodMode::Iq:
        break;
    }
    // L'IQ non esiste su una radio tradizionale: chiederlo vorrebbe dire farsi
    // rispondere «RPRT -9» e restare sul modo di prima. USB è il modo in cui la
    // radio è quasi sempre già, e non muove niente di sorprendente.
    return QStringLiteral("USB");
}

QString RigctldDriver::fieldValue(const QByteArray &reply, const QString &field)
{
    const QList<QByteArray> lines = reply.split('\n');
    const QByteArray prefix = field.toLatin1() + ':';
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith(prefix))
            continue;
        return QString::fromLatin1(trimmed.mid(prefix.size())).trimmed();
    }
    return QString();
}

QString RigctldDriver::modelFromCaps(const QByteArray &reply)
{
    // `\dump_caps` stampa `Model name:\tFT-991A`. Il nome della radio è quello
    // che si vuole vedere nell'elenco dei device: chi ha configurato rigctld
    // per la propria radio si aspetta di ritrovarne il nome, non «rigctld».
    const QString name = fieldValue(reply, QStringLiteral("Model name"));
    if (!name.isEmpty())
        return name;

    const QString mfg = fieldValue(reply, QStringLiteral("Mfg name"));
    return mfg.isEmpty() ? QString() : mfg;
}

bool RigctldDriver::splitEndpoint(const QString &endpoint, QString &host, quint16 &port)
{
    const QString text = endpoint.trimmed();
    if (text.isEmpty())
        return false;

    port = kDefaultPort;

    // IPv6 fra parentesi quadre: `[::1]:4532`. Senza le parentesi i due punti
    // dell'indirizzo sarebbero indistinguibili da quello della porta.
    if (text.startsWith(QLatin1Char('['))) {
        const int end = text.indexOf(QLatin1Char(']'));
        if (end < 0)
            return false;
        host = text.mid(1, end - 1);
        if (end + 1 < text.size() && text.at(end + 1) == QLatin1Char(':')) {
            bool ok = false;
            const uint value = text.mid(end + 2).toUInt(&ok);
            if (!ok || value == 0 || value > 65535)
                return false;
            port = static_cast<quint16>(value);
        }
        return !host.isEmpty();
    }

    const int colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon < 0) {
        host = text;
        return true;
    }

    host = text.left(colon);
    bool ok = false;
    const uint value = text.mid(colon + 1).toUInt(&ok);
    if (!ok || value == 0 || value > 65535)
        return false;
    port = static_cast<quint16>(value);
    return !host.isEmpty();
}

double RigctldDriver::dbmFromStrengthDb(int strengthDb)
{
    // S9 = −73 dBm: è la definizione IARU sotto i 30 MHz, ed è il punto a cui
    // Hamlib riferisce la sua misura.
    return -73.0 + static_cast<double>(strengthDb);
}

bool RigctldDriver::open(const QString &portName, const CatSerialConfig &serial)
{
    // Su una connessione di rete non esiste una velocità di linea: quella la
    // governa `rigctld`, che è chi ha la porta seriale in mano. Il seam la
    // porta comunque, perché agli altri driver serve.
    Q_UNUSED(serial)

    close();

    QString host;
    quint16 port = kDefaultPort;
    if (!splitEndpoint(portName, host, port)) {
        m_error = QStringLiteral("indirizzo non valido: atteso host o host:porta");
        return false;
    }

    m_socket = std::make_unique<QTcpSocket>();
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(kConnectTimeoutMs)) {
        m_error = m_socket->errorString();
        m_socket.reset();
        return false;
    }

    // Niente algoritmo di Nagle: i comandi sono pacchetti di dieci byte a cui
    // segue subito un'attesa di risposta, e mezzo decimo di secondo di attesa
    // per riempire un segmento moltiplicato per quattro letture a giro
    // ammazzerebbe il polling.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    const qint64 hz = negotiate();
    if (hz <= 0) {
        // Nessuna frequenza plausibile: dall'altra parte c'è qualcosa che
        // ascolta su quella porta, ma non è un CAT. Chiedere la frequenza e'
        // la prova giusta perchè è l'unica cosa che *ogni* rigctl
        // implementa: `dump_caps` no, e il server minimo di DECODIUM 4
        // risponde `RPRT -11`.
        m_error = QStringLiteral("nessun rigctl su %1:%2").arg(host).arg(port);
        close();
        return false;
    }

    // Il nome del modello è un di più: ce l'ha solo chi implementa
    // `dump_caps`, cioè rigctld vero. Senza, si dice quello che si sa — che
    // c'è una radio a quell'indirizzo — invece di inventarsi un modello.
    const Reply caps = ask("\\dump_caps", 0, 1200);
    m_model = caps.ok() ? modelFromCaps(caps.raw) : QString();
    if (m_model.isEmpty())
        m_model = QStringLiteral("Radio via rigctl");

    m_endpoint = host + QLatin1Char(':') + QString::number(port);
    m_strengthAvailable = true;
    m_error.clear();
    qCInfo(dsdrHal) << "rigctl: connesso a" << m_endpoint << "—" << m_model
                    << (m_dialect == Dialect::Extended ? "(protocollo esteso)"
                                                       : "(protocollo corto)");
    return true;
}

void RigctldDriver::close()
{
    if (!m_socket)
        return;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        // `q` è il congedo del protocollo: chiude la sessione dalla parte del
        // demone invece di lasciargli un socket mezzo chiuso da raccogliere.
        m_socket->write("q\n");
        m_socket->waitForBytesWritten(100);
    }
    m_socket->abort();
    m_socket.reset();
}

bool RigctldDriver::isOpen() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

RigctldDriver::Reply RigctldDriver::ask(const QByteArray &command, int expectedValues,
                                        int timeoutMs)
{
    Reply reply;
    if (!isOpen())
        return reply;

    // Il prefisso `+` chiede le risposte in forma estesa. Lo si manda solo a
    // chi lo capisce: un server minimo lo prenderebbe come parte del nome del
    // comando, e a quel punto non risponderebbe più a niente.
    const QByteArray line = (m_dialect == Dialect::Extended ? "+" + command : command) + "\n";
    m_socket->write(line);
    if (!m_socket->waitForBytesWritten(timeoutMs)) {
        m_error = m_socket->errorString();
        return reply;
    }

    QByteArray pending;
    QElapsedTimer clock;
    clock.start();

    while (clock.elapsed() < timeoutMs) {
        if (!m_socket->waitForReadyRead(
                std::max(1, timeoutMs - static_cast<int>(clock.elapsed())))) {
            break;
        }
        pending += m_socket->readAll();

        // Si lavora a righe complete: una risposta può arrivare spezzata in
        // due pacchetti, e mezza riga interpretata è peggio di nessuna riga.
        int newline = pending.indexOf('\n');
        while (newline >= 0) {
            const QByteArray text = pending.left(newline).trimmed();
            pending.remove(0, newline + 1);
            newline = pending.indexOf('\n');

            reply.raw += text + '\n';

            if (text.startsWith("RPRT")) {
                // L'esito chiude sempre il discorso, in tutti e due i
                // dialetti. Senza aspettarlo si leggerebbe metà risposta e la
                // restante metà finirebbe in testa a quella dopo, sfasando il
                // dialogo di un giro — per sempre.
                reply.rprt = text.mid(4).trimmed().toInt();
                reply.answered = true;
                return reply;
            }

            if (text.isEmpty())
                continue;

            if (m_dialect == Dialect::Extended) {
                // `Nome: valore`. Le righe senza due punti — `dump_caps` ne e'
                // pieno — restano solo nella grezza.
                const int colon = text.indexOf(':');
                if (colon >= 0)
                    reply.values.append(QString::fromLatin1(text.mid(colon + 1)).trimmed());
                continue;
            }

            reply.values.append(QString::fromLatin1(text));
            if (expectedValues > 0 && reply.values.size() >= expectedValues) {
                // Il protocollo corto non manda un terminatore quando ha dei
                // valori da dare: finiti quelli, ha finito.
                reply.answered = true;
                return reply;
            }
        }
    }

    if (!reply.answered && reply.values.isEmpty())
        m_error = QStringLiteral("nessuna risposta da rigctl");
    return reply;
}

bool RigctldDriver::tell(const QByteArray &command)
{
    // I comandi che impostano qualcosa non restituiscono valori: rispondono
    // con il solo esito, in entrambi i dialetti.
    return ask(command).ok();
}

qint64 RigctldDriver::negotiate()
{
    // Una domanda sola, di sola lettura, e dice due cose: che dialetto parla
    // chi c'è dall'altra parte, e se è una radio.
    //
    // Si prova prima l'esteso, che è quello che non si può fraintendere. Un
    // server che lo capisce risponde `Frequency: 14074000`; uno minimo — e
    // sono la maggioranza fuori da hamlib — risponde il numero e basta, o
    // niente affatto. In tutti e tre i casi la mossa dopo è chiara.
    m_dialect = Dialect::Extended;
    Reply reply = ask("\\get_freq", 1, 900);
    if (reply.ok() && !reply.values.isEmpty()) {
        bool valid = false;
        const qint64 hz = reply.values.first().toLongLong(&valid);
        if (valid && hz > 0)
            return hz;
    }

    m_dialect = Dialect::Short;
    reply = ask("\\get_freq", 1, 900);
    if (!reply.answered || reply.values.isEmpty())
        return 0;

    bool valid = false;
    const qint64 hz = reply.values.first().toLongLong(&valid);
    return (valid && hz > 0) ? hz : 0;
}

int RigctldDriver::probe(const QString &portName)
{
    if (!open(portName, CatSerialConfig{}))
        return -1;

    // Zero e non una velocità: su una connessione di rete non ce n'è una, e
    // restituire un numero plausibile vorrebbe dire vederlo comparire nel
    // pannello accanto a «baud».
    return 0;
}

bool RigctldDriver::poll(CatState &state)
{
    if (!isOpen())
        return false;

    // Un valore: la frequenza.
    const Reply freq = ask("\\get_freq", 1);
    if (!freq.answered)
        return false;
    if (!freq.values.isEmpty()) {
        bool valid = false;
        const qint64 hz = freq.values.first().toLongLong(&valid);
        if (valid && hz > 0)
            state.frequencyHz = hz;
    }

    // Due: il modo e la larghezza. La larghezza non ci serve — i filtri sono
    // quelli della radio — ma va letta lo stesso: nel protocollo corto è una
    // riga, e lasciarla nel socket la farebbe leggere come risposta alla
    // domanda dopo.
    const Reply mode = ask("\\get_mode", 2);
    if (!mode.answered)
        return false;
    if (!mode.values.isEmpty())
        state.mode = modeFromName(mode.values.first());

    // Uno: il PTT.
    const Reply ptt = ask("\\get_ptt", 1);
    if (!ptt.answered)
        return false;
    if (!ptt.values.isEmpty())
        state.transmitting = ptt.values.first().toInt() != 0;

    // L'S-meter è l'unica lettura facoltativa: parecchi server non ce l'hanno
    // — quello di DECODIUM 4 risponde `RPRT -11`, «non implementato» — e
    // chiederla cinque volte al secondo per tutta la sessione è tempo speso a
    // farsi dire di no. Al primo rifiuto si smette.
    if (m_strengthAvailable) {
        const Reply level = ask("\\get_level STRENGTH", 1);
        if (!level.answered || level.values.isEmpty() || level.rprt != 0) {
            m_strengthAvailable = false;
            state.sMeterRaw = -1;
            state.signalDbm = std::numeric_limits<double>::quiet_NaN();
            qCInfo(dsdrHal) << "rigctl: la radio non riporta l'S-meter, "
                               "il livello resterà quello misurato sull'audio";
        } else {
            state.signalDbm = dbmFromStrengthDb(level.values.first().toInt());
        }
    }

    return true;
}

bool RigctldDriver::setFrequency(qint64 hz)
{
    if (hz <= 0)
        return false;
    return tell("\\set_freq " + QByteArray::number(hz));
}

bool RigctldDriver::setMode(DemodMode mode)
{
    // Larghezza zero: in Hamlib significa «quella normale per questo modo».
    // Imporne una nostra vorrebbe dire scavalcare i filtri che l'operatore ha
    // scelto sulla radio, che sono quelli buoni — noi il modo lo cambiamo, la
    // larghezza no.
    return tell("\\set_mode " + nameFromMode(mode).toLatin1() + " 0");
}

bool RigctldDriver::setPtt(bool transmit)
{
    return tell(transmit ? "\\set_ptt 1" : "\\set_ptt 0");
}

} // namespace dsdr::hal::audiorig
