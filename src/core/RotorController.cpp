// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/RotorController.h"

#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>

#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {

/// Quanto si aspetta prima di poter mandare un altro puntamento.
///
/// Non è una limitazione dell'interfaccia: è che ogni comando di posizione
/// chiude dei relè in un controller, e quelli hanno una vita finita. Un secondo
/// e mezzo è più che sufficiente a chi punta a mano e taglia via le raffiche di
/// chi trascina un cursore.
constexpr int kMoveCooldownMs = 1500;

/// Oltre questo il server non risponde più: si considera caduto.
constexpr int kReplyTimeoutMs = 4000;

/// Quanto si aspetta la risposta alla domanda che stabilisce il dialetto.
///
/// Molto meno del resto, e per una ragione precisa: un server minimo al `+`
/// non risponde **niente**, e il silenzio è la risposta. Aspettarlo quattro
/// secondi vorrebbe dire quattro secondi di attesa a ogni collegamento con
/// mezzo ecosistema — e chi guarda penserebbe che non funzioni.
constexpr int kNegotiationTimeoutMs = 1200;

double normalizeAzimuth(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;
    return degrees;
}

/// La differenza fra due azimut, sempre fra 0 e 180: 359 e 1 distano due gradi,
/// non trecentocinquantotto.
double azimuthDelta(double a, double b)
{
    const double diff = std::abs(normalizeAzimuth(a) - normalizeAzimuth(b));
    return std::min(diff, 360.0 - diff);
}

/// Da una riga del dialetto esteso al suo valore. `Azimuth: 123.4` → `123.4`.
QString valueOf(const QString &line)
{
    const int colon = line.indexOf(QLatin1Char(':'));
    return colon >= 0 ? line.mid(colon + 1).trimmed() : line.trimmed();
}

} // namespace

RotorController::RotorController(QObject *parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(m_pollMs);
    connect(m_pollTimer, &QTimer::timeout, this, &RotorController::poll);
}

RotorController::~RotorController()
{
    // Non si ferma il rotore chiudendo il programma, e vale la pena dirlo: se
    // sta andando da qualche parte, ce lo si è mandato apposta, e interrompere
    // a metà lo lascerebbe puntato in mezzo al nulla senza che nessuno lo abbia
    // chiesto.
    disconnectFromRotor();
}

bool RotorController::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState
        && !m_negotiating;
}

bool RotorController::isMoving() const
{
    if (m_targetAzimuth < 0.0 || m_azimuth < 0.0)
        return false;
    return azimuthDelta(m_azimuth, m_targetAzimuth) > kArrivedDeg;
}

void RotorController::setHost(const QString &host)
{
    if (m_host == host)
        return;
    m_host = host;
    emit settingsChanged();
}

void RotorController::setPort(int port)
{
    port = std::clamp(port, 1, 65535);
    if (m_port == port)
        return;
    m_port = port;
    emit settingsChanged();
}

void RotorController::setPollMs(int ms)
{
    // Sotto i duecento millisecondi si interroga un rotore più in fretta di
    // quanto si muova: gira attorno ai cinque gradi al secondo, e più fitto
    // sarebbe traffico che non aggiunge una cifra.
    ms = std::clamp(ms, 200, 5000);
    if (m_pollMs == ms)
        return;
    m_pollMs = ms;
    m_pollTimer->setInterval(ms);
    emit settingsChanged();
}

void RotorController::setTrouble(const QString &message)
{
    if (m_trouble == message)
        return;
    m_trouble = message;
    emit troubleChanged();
}

void RotorController::connectToRotor()
{
    if (m_socket)
        disconnectFromRotor();

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &RotorController::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RotorController::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        m_pollTimer->stop();
        if (isMoving()) {
            // Il rotore sta ancora andando, e da qui non lo si ferma più.
            // Dirlo è l'unica cosa utile che si può fare: chi opera saprà se
            // deve scendere in torre o staccare l'alimentazione.
            setTrouble(tr("Collegamento caduto mentre il rotore girava: "
                          "continuerà fino al bersaglio da solo."));
        }
        emit connectionChanged();
    });

    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_queue.clear();
        m_awaiting = false;
        m_replyLines.clear();
        m_incoming.clear();

        // Si prova prima l'esteso, che è quello che non si può fraintendere.
        m_extended = true;
        m_negotiating = true;
        send({QByteArrayLiteral("\\get_pos"), 2, Pending::Kind::Position});
    });

    setTrouble(QString());
    m_socket->connectToHost(m_host, static_cast<quint16>(m_port));
}

void RotorController::disconnectFromRotor()
{
    m_pollTimer->stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_queue.clear();
    m_awaiting = false;
    m_azimuth = -1.0;
    m_targetAzimuth = -1.0;
    m_model.clear();
    emit positionChanged();
    emit connectionChanged();
}

void RotorController::send(const Pending &pending)
{
    m_queue.enqueue(pending);
    flush();
}

void RotorController::flush()
{
    if (m_awaiting || m_queue.isEmpty() || !m_socket
        || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const Pending &next = m_queue.head();

    // Il prefisso `+` chiede le risposte in forma estesa. Lo si manda solo a
    // chi lo capisce: un server minimo lo prenderebbe come parte del nome del
    // comando, e da lì in poi non risponderebbe più a niente.
    QByteArray line = next.command;
    if (m_extended)
        line.prepend('+');
    line.append('\n');

    m_socket->write(line);
    m_awaiting = true;
    m_replyLines.clear();
    const quint64 ticket = ++m_ticket;

    // Un server che smette di rispondere non chiude il socket: resta lì. Senza
    // questo, la coda si bloccherebbe per sempre e l'indicatore resterebbe sul
    // valore di mezz'ora prima.
    const int timeout = m_negotiating ? kNegotiationTimeoutMs : kReplyTimeoutMs;
    QTimer::singleShot(timeout, this, [this, ticket] {
        if (!m_awaiting || ticket != m_ticket)
            return;

        m_awaiting = false;
        m_queue.clear();

        if (m_negotiating && m_extended) {
            // Il silenzio **è** la risposta: un server minimo al `+` non dice
            // niente. Si riprova con il dialetto corto, una volta sola.
            fallBackToShortDialect();
            return;
        }

        setTrouble(tr("Il rotore non risponde."));
        if (m_negotiating) {
            m_negotiating = false;
            emit connectionChanged();
        }
    });
}

void RotorController::onSocketError()
{
    setTrouble(m_socket ? m_socket->errorString() : tr("Collegamento non riuscito."));
    m_pollTimer->stop();
    m_negotiating = false;
    emit connectionChanged();
}

void RotorController::onReadyRead()
{
    if (!m_socket)
        return;

    m_incoming += m_socket->readAll();

    // Si lavora a righe complete: una risposta può arrivare spezzata in due
    // pacchetti, e mezza riga interpretata è peggio di nessuna riga.
    int newline = m_incoming.indexOf('\n');
    while (newline >= 0) {
        const QString text = QString::fromLatin1(m_incoming.left(newline)).trimmed();
        m_incoming.remove(0, newline + 1);
        newline = m_incoming.indexOf('\n');

        if (text.isEmpty())
            continue;

        if (!m_awaiting || m_queue.isEmpty()) {
            // Roba che arriva senza che nessuno l'abbia chiesta: si butta.
            // Prenderla per buona sfaserebbe il dialogo di un giro, per sempre.
            continue;
        }

        if (text.startsWith(QLatin1String("RPRT"))) {
            const int rprt = text.mid(4).trimmed().toInt();
            const Pending pending = m_queue.dequeue();
            const QStringList lines = m_replyLines;
            m_replyLines.clear();
            m_awaiting = false;
            handleReply(lines, rprt, pending);
            flush();
            continue;
        }

        m_replyLines.append(text);

        // Nel dialetto corto le letture non hanno terminatore: si contano le
        // righe attese, e quando ci sono tutte il discorso è chiuso.
        if (!m_extended && m_replyLines.size() >= m_queue.head().expectedValues
            && m_queue.head().expectedValues > 0) {
            const Pending pending = m_queue.dequeue();
            const QStringList lines = m_replyLines;
            m_replyLines.clear();
            m_awaiting = false;
            handleReply(lines, 0, pending);
            flush();
        }
    }
}

void RotorController::handleReply(const QStringList &lines, int rprt,
                                  const Pending &pending)
{
    if (m_negotiating) {
        m_negotiating = false;

        const bool good = rprt == 0 && lines.size() >= 2;
        if (!good && m_extended) {
            // L'esteso non ha funzionato: si riprova con il corto, una volta
            // sola. Rifiutati entrambi, il problema non è il dialetto.
            m_extended = false;
            m_negotiating = true;
            m_queue.clear();
            m_awaiting = false;
            send({QByteArrayLiteral("\\get_pos"), 2, Pending::Kind::Position});
            return;
        }

        if (!good) {
            // Il caso vero, e capita: sulla 4533 di una stazione ci può stare
            // un server **rigctl** invece di rotctld — le due porte sono
            // vicine e i due demoni si somigliano. Chiedere `get_pos` a una
            // radio non dà una posizione: dà un rifiuto, e questo è il punto
            // in cui lo si dice invece di restare collegati a un indicatore
            // che non si muoverà mai.
            const QString detail = lines.value(0).trimmed();
            setTrouble(detail.isEmpty()
                           ? tr("Dall'altra parte non c'è un rotore.")
                           : tr("Dall'altra parte non c'è un rotore: %1").arg(detail));
            disconnectFromRotor();
            return;
        }

        qCInfo(dsdrCore) << "rotore: collegato a" << m_host << m_port
                         << (m_extended ? "(esteso)" : "(corto)");
        setTrouble(QString());
        emit connectionChanged();

        // Con il dialetto esteso si può chiedere che rotore sia, e se ha
        // l'elevazione. Con quello corto non si può, e non si inventa.
        if (m_extended)
            send({QByteArrayLiteral("\\dump_caps"), 0, Pending::Kind::Capabilities});

        m_pollTimer->start();
    }

    switch (pending.kind) {
    case Pending::Kind::Position: {
        if (lines.size() < 2)
            return;
        const double az = valueOf(lines.at(0)).toDouble();
        const double el = valueOf(lines.at(1)).toDouble();
        const bool moved = !qFuzzyCompare(m_azimuth + 1.0, az + 1.0)
            || !qFuzzyCompare(m_elevation + 1.0, el + 1.0);
        m_azimuth = normalizeAzimuth(az);
        m_elevation = el;
        if (moved || m_targetAzimuth >= 0.0)
            emit positionChanged();
        break;
    }

    case Pending::Kind::Capabilities: {
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("Model name:")))
                m_model = valueOf(line);
            // Un rotore di solo azimut dichiara un intervallo di elevazione
            // nullo. È l'unico modo di saperlo senza chiederlo all'operatore,
            // e senza, l'indicatore d'elevazione resterebbe fermo sullo zero
            // sembrando un dato.
            if (line.startsWith(QLatin1String("Max Elevation:"))) {
                const double maxEl = valueOf(line).toDouble();
                m_hasElevation = maxEl > 0.5;
            }
        }
        emit settingsChanged();
        emit connectionChanged();
        break;
    }

    case Pending::Kind::Acknowledge:
        // Solo il fallimento parla. Cancellare il messaggio quando l'esito è
        // buono sembrava pulito e cancellava il messaggio sbagliato: il
        // «aspetta un attimo» di un puntamento rifiutato arriva **dopo** che è
        // partito quello precedente, e l'esito di quello lo spazzava via prima
        // che qualcuno lo leggesse.
        if (rprt != 0)
            setTrouble(tr("Il rotore ha rifiutato il comando (%1).").arg(rprt));
        break;
    }
}

void RotorController::fallBackToShortDialect()
{
    m_extended = false;
    m_negotiating = true;
    m_queue.clear();
    m_awaiting = false;
    m_replyLines.clear();
    m_incoming.clear();
    send({QByteArrayLiteral("\\get_pos"), 2, Pending::Kind::Position});
}

void RotorController::poll()
{
    if (!isConnected() || m_queue.size() > 2)
        return;
    send({QByteArrayLiteral("\\get_pos"), 2, Pending::Kind::Position});
}

void RotorController::pointTo(double azimuthDeg, double elevationDeg)
{
    if (!isConnected()) {
        setTrouble(tr("Nessun rotore collegato."));
        return;
    }

    if (!m_nextMoveAllowed.hasExpired()) {
        // Non è un capriccio: ogni comando di posizione chiude dei relè, e
        // trascinare un cursore ne manderebbe cento in tre secondi.
        setTrouble(tr("Un comando di puntamento per volta: aspetta un attimo."));
        return;
    }

    setTrouble(QString());
    m_targetAzimuth = normalizeAzimuth(azimuthDeg);
    m_targetElevation = m_hasElevation ? std::clamp(elevationDeg, 0.0, 90.0) : 0.0;
    m_nextMoveAllowed = QDeadlineTimer(kMoveCooldownMs);

    const QByteArray command = QStringLiteral("\\set_pos %1 %2")
                                   .arg(m_targetAzimuth, 0, 'f', 1)
                                   .arg(m_targetElevation, 0, 'f', 1)
                                   .toLatin1();
    qCInfo(dsdrCore) << "rotore: punta a" << m_targetAzimuth << m_targetElevation;
    send({command, 0, Pending::Kind::Acknowledge});
    emit positionChanged();
}

void RotorController::stop()
{
    if (!isConnected())
        return;

    // Davanti a tutto. Se qualcuno preme STOP mentre un puntamento è in coda,
    // il puntamento non deve partire: sarebbe l'esatto contrario di quello che
    // è stato chiesto, e il rotore ripartirebbe subito dopo essersi fermato.
    m_queue.clear();
    m_awaiting = false;
    m_replyLines.clear();
    m_targetAzimuth = -1.0;
    setTrouble(QString());

    qCInfo(dsdrCore) << "rotore: ferma";
    send({QByteArrayLiteral("\\stop"), 0, Pending::Kind::Acknowledge});
    emit positionChanged();
}

void RotorController::park()
{
    if (!isConnected())
        return;
    m_targetAzimuth = -1.0;
    send({QByteArrayLiteral("\\park"), 0, Pending::Kind::Acknowledge});
    emit positionChanged();
}

} // namespace dsdr::core
