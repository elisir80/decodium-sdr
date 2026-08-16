// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/LocalRigctldDriver.h"

#include "hal/HalLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

namespace dsdr::hal::audiorig {

namespace {

constexpr int kTs940Model = 2011;
constexpr int kStartupTimeoutMs = 5000;

} // namespace

LocalRigctldDriver::LocalRigctldDriver() = default;

LocalRigctldDriver::~LocalRigctldDriver()
{
    close();
}

QString LocalRigctldDriver::rigctldExecutable()
{
    const QString configured = qEnvironmentVariable("DSDR_RIGCTLD_BIN").trimmed();
    if (!configured.isEmpty())
        return configured;

    // Dal Finder il PATH non eredita quello della shell Homebrew. Cerchiamo
    // inoltre il rigctld che un pacchetto DECODIUM puo' portare con se', così
    // «radio supportata da Hamlib» non significa «radio supportata soltanto
    // sul Mac dello sviluppatore».
    const QStringList bundledAndMacPaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/rigctld"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/rigctld.exe"),
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/../Resources/hamlib/bin/rigctld"),
        QStringLiteral("/opt/homebrew/bin/rigctld"),
        QStringLiteral("/usr/local/bin/rigctld"),
    };
    for (const QString &candidate : bundledAndMacPaths) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(QStringLiteral("rigctld"));
}

QVariantMap LocalRigctldDriver::serialDefaultsFromListing(const QString &listing)
{
    // Questi sono valori prudenti per un apparato generico: non alzano DTR o
    // RTS e non presuppongono che una radio del catalogo usi il CAT del TS-940.
    QVariantMap result{{QStringLiteral("baud"), 9600},
                       {QStringLiteral("dataBits"), 8},
                       {QStringLiteral("parity"), 0},
                       {QStringLiteral("stopBits"), 1},
                       {QStringLiteral("flowControl"), 0},
                       {QStringLiteral("dtr"), false},
                       {QStringLiteral("rts"), false}};

    const auto valueFor = [&listing](const QString &key) {
        const QRegularExpression expression(
            QStringLiteral(R"((?:^|\n)%1:[^\n]*\n\s*Default:.*?Value:\s*([^\s]+))")
                .arg(QRegularExpression::escape(key)),
            QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = expression.match(listing);
        return match.hasMatch() ? match.captured(1) : QString();
    };

    bool valid = false;
    const int baud = valueFor(QStringLiteral("serial_speed")).toInt(&valid);
    if (valid && baud > 0)
        result.insert(QStringLiteral("baud"), baud);

    valid = false;
    const int dataBits = valueFor(QStringLiteral("data_bits")).toInt(&valid);
    if (valid && dataBits >= 5 && dataBits <= 8)
        result.insert(QStringLiteral("dataBits"), dataBits);

    const QString stopBits = valueFor(QStringLiteral("stop_bits"));
    if (stopBits == QLatin1String("2"))
        result.insert(QStringLiteral("stopBits"), 2);
    else if (stopBits == QLatin1String("1.5"))
        result.insert(QStringLiteral("stopBits"), 15);

    const QString parity = valueFor(QStringLiteral("serial_parity")).toLower();
    if (parity == QLatin1String("even"))
        result.insert(QStringLiteral("parity"), 1);
    else if (parity == QLatin1String("odd"))
        result.insert(QStringLiteral("parity"), 2);
    else if (parity == QLatin1String("mark"))
        result.insert(QStringLiteral("parity"), 3);
    else if (parity == QLatin1String("space"))
        result.insert(QStringLiteral("parity"), 4);

    const QString handshake = valueFor(QStringLiteral("serial_handshake")).toLower();
    if (handshake == QLatin1String("hardware"))
        result.insert(QStringLiteral("flowControl"), 1);
    else if (handshake == QLatin1String("xonxoff"))
        result.insert(QStringLiteral("flowControl"), 2);
    return result;
}

QVariantMap LocalRigctldDriver::serialDefaultsForModel(int hamlibModel)
{
    if (hamlibModel <= 0)
        return serialDefaultsFromListing({});
    const QString executable = rigctldExecutable();
    if (executable.isEmpty())
        return serialDefaultsFromListing({});

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-m"), QString::number(hamlibModel),
                          QStringLiteral("-L")});
    process.start();
    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0)
        return serialDefaultsFromListing({});
    return serialDefaultsFromListing(QString::fromLocal8Bit(process.readAllStandardOutput()));
}

QVariantList LocalRigctldDriver::availableModels(QString *error)
{
    static QVariantList cached;
    static bool queried = false;
    static QString cachedError;
    if (queried) {
        if (error)
            *error = cachedError;
        return cached;
    }
    queried = true;

    const QString executable = rigctldExecutable();
    if (executable.isEmpty()) {
        cachedError = QObject::tr("rigctld non trovato nel PATH. Installa Hamlib oppure imposta DSDR_RIGCTLD_BIN.");
        if (error)
            *error = cachedError;
        return cached;
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-l")});
    process.start();
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        cachedError = QObject::tr("rigctld non riesce a elencare i modelli: %1")
                          .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        if (error)
            *error = cachedError;
        return cached;
    }

    const QRegularExpression linePattern(
        QStringLiteral(R"(^\s*(\d+)\s{2,}(.*?)\s{2,}\d{8}(?:\.\d+)?\s{2,})"));
    const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput())
                                  .split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = linePattern.match(line);
        if (!match.hasMatch())
            continue;
        const int id = match.captured(1).toInt();
        const QString label = match.captured(2).simplified();
        if (id <= 0 || label.isEmpty())
            continue;
        cached.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("label"),
                                   QStringLiteral("%1 · %2").arg(id).arg(label)}});
    }
    if (cached.isEmpty())
        cachedError = QObject::tr("rigctld non ha restituito alcun modello utilizzabile.");
    if (error)
        *error = cachedError;
    return cached;
}

QString LocalRigctldDriver::serialParityName(int parity)
{
    switch (parity) {
    case 1: return QStringLiteral("Even");
    case 2: return QStringLiteral("Odd");
    case 3: return QStringLiteral("Mark");
    case 4: return QStringLiteral("Space");
    default: return QStringLiteral("None");
    }
}

QString LocalRigctldDriver::serialHandshakeName(int flowControl)
{
    switch (flowControl) {
    case 1: return QStringLiteral("Hardware");
    case 2: return QStringLiteral("XONXOFF");
    default: return QStringLiteral("None");
    }
}

QString LocalRigctldDriver::serialControlLineStateName(int flowControl, bool asserted)
{
    // RTS e' gestito dal driver con il flow-control hardware. `OFF` non e'
    // un valore neutro: tiene la linea bassa e una radio come il TS-940S non
    // puo' rispondere sul CAT.
    if (flowControl == 1)
        return QStringLiteral("Unset");
    return asserted ? QStringLiteral("ON") : QStringLiteral("OFF");
}

QString LocalRigctldDriver::serialDevicePathForRigctld(const QString &portName)
{
#ifdef Q_OS_WIN
    return portName;
#else
    if (portName.isEmpty() || portName.startsWith(QLatin1Char('/')))
        return portName;
    return QStringLiteral("/dev/%1").arg(portName);
#endif
}

QString LocalRigctldDriver::serialStopBitsName(int stopBits)
{
    if (stopBits == 2)
        return QStringLiteral("2");
    if (stopBits == 15)
        return QStringLiteral("1.5");
    return QStringLiteral("1");
}

quint16 LocalRigctldDriver::findFreeLoopbackPort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

bool LocalRigctldDriver::openForBaud(const QString &portName, CatSerialConfig serial)
{
    close();
    const QString executable = rigctldExecutable();
    if (executable.isEmpty()) {
        m_error = QObject::tr("rigctld non trovato. Installa Hamlib o imposta DSDR_RIGCTLD_BIN.");
        return false;
    }
    if (serial.hamlibModel <= 0) {
        m_error = QObject::tr("Modello Hamlib mancante: seleziona la radio prima di connetterti.");
        return false;
    }
    const quint16 tcpPort = findFreeLoopbackPort();
    if (tcpPort == 0) {
        m_error = QObject::tr("Impossibile riservare una porta locale per rigctld.");
        return false;
    }

    // I parametri della seriale vengono passati a Hamlib, cioe' al processo
    // che possiede davvero il cavo USB. Non apriamo mai la stessa porta anche
    // nel client: due proprietari del CAT genererebbero byte intercalati.
    const QStringList configuration = {
        QStringLiteral("data_bits=%1").arg(serial.dataBits),
        QStringLiteral("stop_bits=%1").arg(serialStopBitsName(serial.stopBits)),
        QStringLiteral("serial_parity=%1").arg(serialParityName(serial.parity)),
        QStringLiteral("serial_handshake=%1").arg(serialHandshakeName(serial.flowControl)),
        QStringLiteral("dtr_state=%1").arg(serialControlLineStateName(serial.flowControl,
                                                                         serial.dtr)),
        QStringLiteral("rts_state=%1").arg(serialControlLineStateName(serial.flowControl,
                                                                         serial.rts)),
        QStringLiteral("timeout=150"),
        QStringLiteral("retry=1"),
        QStringLiteral("cache_timeout=0"),
    };

    const QString serialDevice = serialDevicePathForRigctld(portName);
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(executable);
    m_process->setArguments({QStringLiteral("-m"), QString::number(serial.hamlibModel),
                             QStringLiteral("-r"), serialDevice,
                             QStringLiteral("-s"), QString::number(serial.baudRate),
                             QStringLiteral("-t"), QString::number(tcpPort),
                             QStringLiteral("-T"), QStringLiteral("127.0.0.1"),
                             QStringLiteral("-C"), configuration.join(QLatin1Char(','))});
    m_process->start();
    if (!m_process->waitForStarted(1500)) {
        m_error = QObject::tr("Impossibile avviare rigctld: %1").arg(processError());
        m_process.reset();
        return false;
    }

    const QString endpoint = QStringLiteral("127.0.0.1:%1").arg(tcpPort);
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < kStartupTimeoutMs) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            m_error = QObject::tr("avvio rigctld annullato");
            close();
            return false;
        }
        if (m_remote.open(endpoint, CatSerialConfig{})) {
            m_model = QStringLiteral("Hamlib %1").arg(m_remote.radioModel());
            if (m_remote.radioModel().isEmpty())
                m_model = QStringLiteral("Hamlib model %1").arg(serial.hamlibModel);
            m_error.clear();
            qCInfo(dsdrHal) << "hamlib locale: rigctld avviato per modello"
                            << serial.hamlibModel << "su" << serialDevice
                            << "endpoint" << endpoint;
            return true;
        }
        QThread::msleep(80);
    }

    const QString detail = processError().isEmpty() ? m_remote.errorString() : processError();
    m_error = QObject::tr("rigctld non ha aperto la radio (modello %1, porta %2, %3 baud, %4N%5, %6): %7")
                  .arg(serial.hamlibModel)
                  .arg(portName)
                  .arg(serial.baudRate)
                  .arg(serial.dataBits)
                  .arg(serial.stopBits)
                  .arg(serialHandshakeName(serial.flowControl))
                  .arg(detail.isEmpty() ? QObject::tr("nessuna risposta CAT") : detail);
    close();
    return false;
}

bool LocalRigctldDriver::open(const QString &portName, const CatSerialConfig &serial)
{
    return openForBaud(portName, serial);
}

void LocalRigctldDriver::close()
{
    m_remote.close();
    if (!m_process)
        return;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    m_process.reset();
    m_model.clear();
}

bool LocalRigctldDriver::isOpen() const
{
    return m_process && m_process->state() == QProcess::Running && m_remote.isOpen();
}

QString LocalRigctldDriver::errorString() const
{
    return m_error.isEmpty() ? m_remote.errorString() : m_error;
}

QList<int> LocalRigctldDriver::candidateBaudRates() const
{
    // 4800/8N2/RTS-CTS e' il default del TS-940S. Le altre servono agli
    // apparati Hamlib piu' recenti quando l'utente sceglie «automatica».
    return {4800, 9600, 19200, 38400, 57600, 115200};
}

int LocalRigctldDriver::probe(const QString &portName)
{
    CatSerialConfig serial;
    serial.baudRate = 4800;
    serial.dataBits = 8;
    serial.stopBits = 2;
    serial.flowControl = 1;
    serial.hamlibModel = kTs940Model;
    return open(portName, serial) ? serial.baudRate : -1;
}

QString LocalRigctldDriver::processError() const
{
    if (!m_process)
        return {};
    const QString standard = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
    if (!standard.isEmpty())
        return standard;
    if (m_process->state() == QProcess::Running)
        return {};
    if (m_process->error() != QProcess::UnknownError && !m_process->errorString().isEmpty())
        return m_process->errorString();
    return QObject::tr("rigctld terminato con codice %1").arg(m_process->exitCode());
}

} // namespace dsdr::hal::audiorig
