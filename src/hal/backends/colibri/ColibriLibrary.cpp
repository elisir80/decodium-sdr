// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/colibri/ColibriLibrary.h"
#include "hal/HalLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QMutexLocker>

namespace dsdr::hal::colibri {

int sampleRateIndex(int hz) noexcept
{
    int index = 0;
    for (const int rate : kSampleRatesHz) {
        if (rate == hz)
            return index;
        ++index;
    }
    return -1;
}

ColibriLibrary &ColibriLibrary::instance()
{
    static ColibriLibrary library;
    return library;
}

QStringList ColibriLibrary::candidatePaths(const QString &explicitPath)
{
    QStringList paths;
    if (!explicitPath.isEmpty())
        paths << explicitPath;

#ifdef Q_OS_WIN
    const QString base = QStringLiteral("colibrinano_lib.dll");
#else
    const QString base = QStringLiteral("libcolibrinano_lib.so");
#endif

    // Accanto all'eseguibile: è dove finisce nel pacchetto.
    paths << QDir(QCoreApplication::applicationDirPath()).filePath(base);

    // Nome nudo per ultimo: lascia cercare al sistema, che è il modo in cui si
    // trova un'installazione fatta dal costruttore senza doverla indovinare.
    paths << base;

    return paths;
}

bool ColibriLibrary::ensureLoaded(const QString &explicitPath, QString *error)
{
    QMutexLocker lock(&m_mutex);
    if (m_initialized)
        return true;

    const QStringList candidates = candidatePaths(explicitPath);
    for (const QString &candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load()) {
            m_path = candidate;
            break;
        }
    }

    if (!m_library.isLoaded()) {
        if (error) {
            *error = QStringLiteral("colibrinano_lib non trovata (cercata in: %1)")
                         .arg(candidates.join(QStringLiteral("; ")));
        }
        return false;
    }

    const auto resolve = [this](const char *name) { return m_library.resolve(name); };

    m_initialize = reinterpret_cast<FnVoid>(resolve("initialize"));
    m_finalize = reinterpret_cast<FnVoid>(resolve("finalize"));
    m_version = reinterpret_cast<FnVersion>(resolve("version"));
    m_information = reinterpret_cast<FnInformation>(resolve("information"));
    m_devices = reinterpret_cast<FnDevices>(resolve("devices"));
    m_open = reinterpret_cast<FnOpen>(resolve("open"));
    m_close = reinterpret_cast<FnClose>(resolve("close"));
    m_start = reinterpret_cast<FnStart>(resolve("start"));
    m_stop = reinterpret_cast<FnStop>(resolve("stop"));
    // Attenzione al nome: la libreria esporta "setPream", non "setPreamp".
    m_setPreamp = reinterpret_cast<FnSetPreamp>(resolve("setPream"));
    m_setFrequency = reinterpret_cast<FnSetFrequency>(resolve("setFrequency"));

    if (!m_initialize || !m_version || !m_information || !m_devices || !m_open || !m_close
        || !m_start || !m_stop || !m_setPreamp || !m_setFrequency) {
        if (error) {
            *error = QStringLiteral("%1 non è una colibrinano_lib: mancano dei simboli")
                         .arg(m_path);
        }
        m_library.unload();
        m_path.clear();
        return false;
    }

    m_initialize();
    m_initialized = true;

    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    m_version(major, minor, patch);
    qCInfo(dsdrHal) << "colibri: caricata" << m_path
                    << QStringLiteral("v%1.%2.%3").arg(major).arg(minor).arg(patch);

    return true;
}

// Nota deliberata: `finalize()` viene risolta ma non chiamata mai.
//
// La libreria la vuole "prima della chiusura del programma", ma invocarla
// durante la distruzione degli oggetti statici significherebbe entrare in una
// DLL che possiede handle FTDI ancora vivi, in un ordine che non controlliamo.
// Lasciare che sia la terminazione del processo a riprendersi le risorse è il
// rischio minore.

void ColibriLibrary::version(std::uint32_t &major, std::uint32_t &minor, std::uint32_t &patch)
{
    QMutexLocker lock(&m_mutex);
    major = minor = patch = 0;
    if (m_version)
        m_version(major, minor, patch);
}

QString ColibriLibrary::information()
{
    QMutexLocker lock(&m_mutex);
    if (!m_information)
        return {};

    char *text = nullptr;
    m_information(&text);
    return text ? QString::fromLatin1(text) : QString();
}

std::uint32_t ColibriLibrary::deviceCount()
{
    QMutexLocker lock(&m_mutex);
    if (!m_devices)
        return 0;

    std::uint32_t count = 0;
    m_devices(count);
    return count;
}

bool ColibriLibrary::open(ColibriDescriptor *out, std::uint32_t index)
{
    QMutexLocker lock(&m_mutex);
    return m_open ? m_open(out, index) : false;
}

void ColibriLibrary::close(ColibriDescriptor device)
{
    QMutexLocker lock(&m_mutex);
    if (m_close)
        m_close(device);
}

bool ColibriLibrary::start(ColibriDescriptor device, int rateIndex, ColibriRxCallback callback,
                           void *user)
{
    QMutexLocker lock(&m_mutex);
    return m_start ? m_start(device, rateIndex, callback, user) : false;
}

bool ColibriLibrary::stop(ColibriDescriptor device)
{
    QMutexLocker lock(&m_mutex);
    return m_stop ? m_stop(device) : false;
}

bool ColibriLibrary::setPreamp(ColibriDescriptor device, float db)
{
    QMutexLocker lock(&m_mutex);
    return m_setPreamp ? m_setPreamp(device, db) : false;
}

bool ColibriLibrary::setFrequency(ColibriDescriptor device, std::uint32_t hz)
{
    QMutexLocker lock(&m_mutex);
    return m_setFrequency ? m_setFrequency(device, hz) : false;
}

} // namespace dsdr::hal::colibri
