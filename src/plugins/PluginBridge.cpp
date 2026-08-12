// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugins/PluginBridge.h"

#include "plugins/PluginProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRandomGenerator>

#include <cstring>

Q_LOGGING_CATEGORY(dsdrPlugins, "dsdr.plugins")

namespace dsdr::plugins {

namespace {

/// Quanto si concede all'ospite per restituire un blocco.
///
/// Venti millisecondi sono già il doppio della durata di un blocco a 48 kHz:
/// oltre, non è più un plugin lento, è un plugin fermo. E un blocco che non
/// torna in tempo va lasciato passare com'è — aspettare vorrebbe dire un buco
/// nella trasmissione, che si sente molto più di uno stadio saltato.
constexpr int kProcessTimeoutMs = 20;

QString hostPath()
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    return QDir(dir).filePath(kHostExecutable + QLatin1String(".exe"));
#else
    return QDir(dir).filePath(kHostExecutable);
#endif
}

} // namespace

PluginBridge::PluginBridge(QObject *parent)
    : QObject(parent)
{
    m_scratch.resize(static_cast<std::size_t>(kMaxBlockFrames) * kChannels);
}

PluginBridge::~PluginBridge()
{
    stop();
}

bool PluginBridge::hostAvailable()
{
    return QFileInfo::exists(hostPath());
}

bool PluginBridge::start()
{
    if (isRunning())
        return true;

    if (!hostAvailable()) {
        m_lastError = tr("L'ospite dei plugin non è installato accanto al programma.");
        return false;
    }

    // La chiave del segmento è diversa a ogni avvio: due copie del programma
    // aperte insieme — che su una stazione con due radio è normale — si
    // scriverebbero addosso i campioni a vicenda, e il risultato sarebbe una
    // voce che ne contiene un'altra.
    m_key = QStringLiteral("dsdr-vst-%1-%2")
                .arg(QCoreApplication::applicationPid())
                .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));

    m_shared = std::make_unique<QSharedMemory>(m_key);
    if (!m_shared->create(kSharedBytes)) {
        m_lastError = tr("Memoria condivisa non disponibile: %1").arg(m_shared->errorString());
        m_shared.reset();
        return false;
    }

    m_process = std::make_unique<QProcess>();
    m_process->setProgram(hostPath());
    m_process->setArguments({m_key});
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);

    connect(m_process.get(), &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
                if (status == QProcess::CrashExit || code != 0)
                    handleDeath();
            });

    m_process->start();
    if (!m_process->waitForStarted(3000)) {
        m_lastError = tr("L'ospite dei plugin non parte: %1").arg(m_process->errorString());
        m_process.reset();
        m_shared.reset();
        return false;
    }

    if (!m_process->waitForReadyRead(3000)) {
        m_lastError = tr("L'ospite dei plugin è partito e non si presenta.");
        stop();
        return false;
    }

    const QByteArray hello = m_process->readLine().trimmed();
    if (!hello.startsWith(kRepReady.data())) {
        m_lastError = tr("L'ospite dei plugin ha detto «%1» invece di presentarsi.")
                          .arg(QString::fromUtf8(hello));
        stop();
        return false;
    }

    qCInfo(dsdrPlugins) << "ospite avviato:" << hello;
    m_lastError.clear();
    emit stateChanged();
    return true;
}

void PluginBridge::stop()
{
    m_live.store(false, std::memory_order_release);

    if (m_process) {
        // Prima si chiede, poi si insiste. Un ospite che sta finendo un blocco
        // merita il tempo di finirlo: ammazzarlo subito lascerebbe il segmento
        // a metà scrittura, e alla prossima lettura ci sarebbe dentro mezza
        // voce vecchia.
        disconnect(m_process.get(), nullptr, this, nullptr);
        if (m_process->state() == QProcess::Running) {
            m_process->write(kCmdQuit.data());
            m_process->write("\n");
            m_process->waitForBytesWritten(200);
            if (!m_process->waitForFinished(1000))
                m_process->kill();
        }
        m_process.reset();
    }

    m_shared.reset();
    m_loadedPath.clear();
    m_loadedName.clear();
    m_parameters.clear();
    emit stateChanged();
}

bool PluginBridge::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void PluginBridge::handleDeath()
{
    ++m_crashes;
    m_live.store(false, std::memory_order_release);

    const QString what = m_loadedName.isEmpty()
        ? tr("Il plugin ha portato giù il suo ospite.")
        : tr("«%1» ha portato giù il suo ospite: il blocco è in bypass e la "
             "trasmissione continua.").arg(m_loadedName);
    m_lastError = what;

    qCWarning(dsdrPlugins) << "ospite morto" << m_crashes << "volte:" << m_loadedPath;

    // Non si riparte da soli. Un plugin che va in crash a ogni blocco
    // diventerebbe un ciclo di riavvii che consuma la macchina mentre chi la
    // guarda non capisce perché si è fermata.
    emit hostDied(what);
    emit stateChanged();
}

QStringList PluginBridge::command(const QString &line, int timeoutMs)
{
    if (!isRunning())
        return {};

    m_process->write(line.toUtf8());
    m_process->write("\n");
    if (!m_process->waitForBytesWritten(timeoutMs))
        return {};

    QStringList out;
    while (true) {
        if (!m_process->canReadLine() && !m_process->waitForReadyRead(timeoutMs))
            return out;

        while (m_process->canReadLine()) {
            const QString reply = QString::fromUtf8(m_process->readLine()).trimmed();
            if (reply.isEmpty())
                continue;
            out.append(reply);
            // `ok`, `err` e `done` chiudono il discorso; tutto il resto è
            // materiale che li precede.
            if (reply.startsWith(kRepOk) || reply.startsWith(kRepError)
                || reply.startsWith(kRepDone)) {
                return out;
            }
        }
    }
}

QList<PluginInfo> PluginBridge::scan()
{
    QList<PluginInfo> found;
    // La scansione legge cartelle e apre file: dieci secondi non sono
    // generosità, sono quello che ci vuole su una macchina con cento plugin
    // installati e un disco che gira.
    const QStringList replies = command(kCmdScan, 10000);

    for (const QString &line : replies) {
        if (!line.startsWith(kRepPlugin))
            continue;
        const QStringList parts = line.mid(kRepPlugin.size() + 1).split(QLatin1Char('|'));
        if (parts.size() < 2)
            continue;
        PluginInfo info;
        info.path = parts.value(0);
        info.name = parts.value(1);
        info.vendor = parts.value(2);
        info.category = parts.value(3);
        found.append(info);
    }
    return found;
}

bool PluginBridge::load(const QString &path)
{
    m_live.store(false, std::memory_order_release);
    m_parameters.clear();
    m_loadedPath.clear();
    m_loadedName.clear();

    if (path.isEmpty()) {
        command(kCmdUnload);
        emit stateChanged();
        return true;
    }

    // Caricare vuol dire aprire una libreria altrui e farle costruire i suoi
    // oggetti: succede tutto dall'altra parte, e se va male torna una riga
    // invece di portarsi via il programma.
    const QStringList replies = command(QStringLiteral("%1 %2").arg(kCmdLoad, path), 10000);
    for (const QString &line : replies) {
        if (line.startsWith(kRepError)) {
            m_lastError = line.mid(kRepError.size() + 1);
            emit stateChanged();
            return false;
        }
        if (line.startsWith(kRepParameter)) {
            const QStringList parts =
                line.mid(kRepParameter.size() + 1).split(QLatin1Char('|'));
            if (parts.size() < 2)
                continue;
            PluginParameter p;
            p.index = parts.value(0).toInt();
            p.name = parts.value(1);
            p.unit = parts.value(2);
            p.value = parts.value(3).toDouble();
            m_parameters.append(p);
        }
        if (line.startsWith(kRepOk))
            m_loadedName = line.mid(kRepOk.size() + 1).trimmed();
    }

    if (replies.isEmpty() || !replies.constLast().startsWith(kRepOk)) {
        m_lastError = tr("Il plugin non ha risposto al caricamento.");
        emit stateChanged();
        return false;
    }

    m_loadedPath = path;
    if (m_loadedName.isEmpty())
        m_loadedName = QFileInfo(path).completeBaseName();

    prepare(m_sampleRate, m_maxFrames);
    m_lastError.clear();
    m_live.store(true, std::memory_order_release);
    emit stateChanged();
    qCInfo(dsdrPlugins) << "plugin caricato:" << m_loadedName
                        << m_parameters.size() << "parametri";
    return true;
}

void PluginBridge::prepare(double sampleRate, int maxFrames)
{
    m_sampleRate = sampleRate;
    m_maxFrames = std::min(maxFrames, kMaxBlockFrames);
    if (m_loadedPath.isEmpty())
        return;
    command(QStringLiteral("%1 %2 %3").arg(kCmdPrepare)
                .arg(sampleRate, 0, 'f', 0).arg(m_maxFrames));
}

void PluginBridge::setParameter(int index, double value)
{
    if (m_loadedPath.isEmpty())
        return;
    command(QStringLiteral("%1 %2 %3").arg(kCmdParam).arg(index).arg(value, 0, 'g', 6));
    for (PluginParameter &p : m_parameters) {
        if (p.index == index)
            p.value = value;
    }
}

void PluginBridge::process(float *audio, std::size_t frames) noexcept
{
    if (!m_enabled.load(std::memory_order_relaxed)
        || !m_live.load(std::memory_order_acquire)) {
        return;
    }
    if (!m_shared || !m_process || frames == 0
        || frames > static_cast<std::size_t>(kMaxBlockFrames)) {
        return;
    }

    auto *base = static_cast<char *>(m_shared->data());
    if (!base)
        return;

    SharedHeader header;
    header.frames = static_cast<int>(frames);
    header.sampleRate = m_sampleRate;
    std::memcpy(base, &header, sizeof(header));

    // La voce è una e i canali sono due: si duplica. Mandare un canale solo a
    // un plugin stereo vuol dire farne elaborare metà, e nessun plugin lo
    // segnala — semplicemente restituisce silenzio da una parte.
    auto *left = reinterpret_cast<float *>(base + sizeof(SharedHeader));
    auto *right = left + kMaxBlockFrames;
    for (std::size_t i = 0; i < frames; ++i) {
        left[i] = audio[i];
        right[i] = audio[i];
    }

    m_process->write(kCmdProcess.data());
    m_process->write("\n");
    if (!m_process->waitForBytesWritten(kProcessTimeoutMs))
        return;

    if (!m_process->waitForReadyRead(kProcessTimeoutMs)) {
        // Fuori tempo: si lascia passare quello che era entrato. Aspettare
        // vorrebbe dire un buco nella trasmissione, e un buco si sente molto
        // più di uno stadio saltato.
        return;
    }

    const QByteArray reply = m_process->readLine().trimmed();
    if (!reply.startsWith(kRepDone.data()))
        return;

    // Si riprende il canale sinistro: è la voce, e i due canali di un plugin
    // di studio su una sorgente mono portano la stessa cosa. Mediarli
    // costerebbe un giro in più per cancellare un'eventuale differenza di fase
    // che nessuno ha chiesto.
    for (std::size_t i = 0; i < frames; ++i)
        audio[i] = left[i];
}

} // namespace dsdr::plugins
