// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BandConditions.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QtEndian>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {

/// Le bande, e solo i confini: il registro non ha bisogno di sapere dove si
/// entra la prima volta né come sono divisi i segmenti.
///
/// **Deve restare d'accordo con `BandPlan.qml`**, che è la tabella che vede
/// l'operatore. Sono due elenchi e non uno perché quello sta in QML e questo
/// serve al C++, e finché è così vale la regola di sempre: se si aggiunge una
/// banda di là, va aggiunta anche di qua — altrimenti su quella banda il
/// registro non annota niente e nessuno capisce perché.
struct BandRange
{
    const char *name;
    qint64 start;
    qint64 end;
};

constexpr BandRange kBands[] = {
    {"160m", 1'810'000, 2'000'000},
    {"80m", 3'500'000, 3'800'000},
    {"60m", 5'351'500, 5'366'500},
    {"40m", 7'000'000, 7'200'000},
    {"30m", 10'100'000, 10'150'000},
    {"20m", 14'000'000, 14'350'000},
    {"17m", 18'068'000, 18'168'000},
    {"15m", 21'000'000, 21'450'000},
    {"12m", 24'890'000, 24'990'000},
    {"10m", 28'000'000, 29'700'000},
    {"6m", 50'000'000, 52'000'000},
    {"2m", 144'000'000, 146'000'000},
    {"70cm", 430'000'000, 440'000'000},
};

constexpr int kBandCount = static_cast<int>(std::size(kBands));

/// Quanti minuti dura un quarto d'ora. Sì, quindici — la costante c'è perché
/// il numero compare in tre conti diversi e vederli disallineati sarebbe un
/// errore silenzioso.
constexpr int kBucketMinutes = 24 * 60 / BandConditions::kBuckets;

/// Ogni quante misure si riscrive il file.
constexpr int kSaveEvery = 240;

constexpr quint32 kMagic = 0x44534443;   // "DSDC"
constexpr quint16 kFormat = 1;

float medianOf(std::vector<float> &values)
{
    if (values.empty())
        return std::numeric_limits<float>::quiet_NaN();
    // La mediana e non la media: un quarto d'ora di fondo con dentro tre
    // secondi di accensione di un frigorifero ha una media che parla del
    // frigorifero. La mediana parla della banda.
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle),
                     values.end());
    return values[middle];
}

} // namespace

BandConditions::BandConditions(QObject *parent)
    : QObject(parent)
{
    load();
}

BandConditions::~BandConditions()
{
    flush();
    save();
}

QString BandConditions::storagePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("condizioni.dat"));
}

int BandConditions::bandFor(qint64 hz)
{
    for (int i = 0; i < kBandCount; ++i) {
        if (hz >= kBands[i].start && hz <= kBands[i].end)
            return i;
    }
    return -1;
}

QString BandConditions::bandNameFor(int index)
{
    if (index < 0 || index >= kBandCount)
        return QString();
    return QString::fromLatin1(kBands[index].name);
}

quint64 BandConditions::keyFor(qint64 julianDay, int band)
{
    return static_cast<quint64>(julianDay) * 64 + static_cast<quint64>(band);
}

int BandConditions::currentBucket() const
{
    const QTime now = QDateTime::currentDateTimeUtc().time();
    return (now.hour() * 60 + now.minute()) / kBucketMinutes;
}

void BandConditions::observe(qint64 centerHz, double floorDbfs,
                             double gainReductionDb, const QString &deviceKey)
{
    const int band = bandFor(centerHz);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const qint64 day = nowUtc.date().toJulianDay();
    const int bucket = (nowUtc.time().hour() * 60 + nowUtc.time().minute())
        / kBucketMinutes;

    const quint32 device = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(
        QCryptographicHash::hash(deviceKey.toUtf8(), QCryptographicHash::Md5)
            .constData()));

    const bool bandChanged = band != m_bandIndex;
    const bool deviceChanged = device != m_device;

    if (bandChanged || deviceChanged || day != m_pendingDay
        || bucket != m_pendingBucket) {
        // Il quarto d'ora in corso si chiude prima di cambiare qualunque cosa:
        // metà su una banda e metà su un'altra non è una misura di nessuna
        // delle due.
        closeBucket();
        m_pendingDay = day;
        m_pendingBucket = bucket;
    }

    if (bandChanged) {
        m_bandIndex = band;
        m_bandName = bandNameFor(band);
    }
    m_device = device;

    if (band < 0) {
        if (bandChanged) {
            recompute();
            emit changed();
        }
        return;
    }

    // Un fondo che non è ancora stato stimato non è un fondo: l'inseguitore
    // parte dal valore di riposo e scende, e i primi secondi dopo la
    // connessione dicono soltanto da dove è partito. La misura si scarta, ma
    // la banda si riconosce lo stesso — il pannello deve poter dire «40m»
    // subito, non fra dieci secondi.
    if (!std::isfinite(floorDbfs) || floorDbfs <= -139.0) {
        if (bandChanged || deviceChanged) {
            recompute();
            emit changed();
        }
        return;
    }

    // Riferito all'antenna: il fondo al convertitore meno il guadagno che la
    // guardia sta togliendo. È la sola forma in cui due giornate con
    // impostazioni diverse dicono la stessa cosa.
    m_pending.push_back(static_cast<float>(floorDbfs + gainReductionDb));

    if (++m_sinceSave >= kSaveEvery) {
        m_sinceSave = 0;
        save();
    }

    if (bandChanged || deviceChanged) {
        recompute();
        emit changed();
    } else if (m_pending.size() % 8 == 0) {
        // Un aggiornamento ogni otto misure: la mediana in corso si muove
        // piano, e ridisegnare a ogni campione sarebbe lavoro per un numero
        // che non cambia.
        emit changed();
    }
}

void BandConditions::closeBucket()
{
    if (m_pending.empty() || m_bandIndex < 0 || m_pendingBucket < 0)
        return;

    const float value = medianOf(m_pending);
    m_pending.clear();

    Day &day = m_days[keyFor(m_pendingDay, m_bandIndex)];
    if (day.device == 0) {
        day.floor.fill(std::numeric_limits<float>::quiet_NaN());
        day.device = m_device;
    }

    if (day.device != m_device) {
        // Ricevitore cambiato in corso di giornata. Non si mescolano: quella
        // che comincia adesso è un'altra giornata, e sovrascrivere quella di
        // prima cancellerebbe una misura buona con una che non le somiglia.
        // La si tiene ferma e si smette di scriverci sopra fino a domani.
        return;
    }

    day.floor[static_cast<std::size_t>(m_pendingBucket)] = value;
    prune();
    recompute();
    emit changed();
}

void BandConditions::flush()
{
    closeBucket();
}

void BandConditions::prune()
{
    if (m_days.size() <= static_cast<qsizetype>(kHistoryDays) * kBandCount)
        return;

    const qint64 oldest = QDate::currentDate().toJulianDay() - kHistoryDays;
    for (auto it = m_days.begin(); it != m_days.end();) {
        if (static_cast<qint64>(it.key() / 64) < oldest)
            it = m_days.erase(it);
        else
            ++it;
    }
}

QVariantList BandConditions::today() const
{
    QVariantList out;
    if (m_bandIndex < 0)
        return out;

    const qint64 day = QDateTime::currentDateTimeUtc().date().toJulianDay();
    const auto it = m_days.constFind(keyFor(day, m_bandIndex));
    out.reserve(kBuckets);
    for (int i = 0; i < kBuckets; ++i) {
        out.append(it == m_days.constEnd()
                       ? std::numeric_limits<double>::quiet_NaN()
                       : static_cast<double>(it->floor[static_cast<std::size_t>(i)]));
    }
    return out;
}

QVariantList BandConditions::yesterday() const
{
    QVariantList out;
    if (m_bandIndex < 0)
        return out;

    const qint64 day = QDateTime::currentDateTimeUtc().date().toJulianDay() - 1;
    const auto it = m_days.constFind(keyFor(day, m_bandIndex));
    out.reserve(kBuckets);
    for (int i = 0; i < kBuckets; ++i) {
        const bool usable = it != m_days.constEnd() && it->device == m_device;
        out.append(usable
                       ? static_cast<double>(it->floor[static_cast<std::size_t>(i)])
                       : std::numeric_limits<double>::quiet_NaN());
    }
    return out;
}

QVariantList BandConditions::typical() const
{
    QVariantList out;
    if (m_bandIndex < 0)
        return out;

    // La mediana delle giornate precedenti, quarto d'ora per quarto d'ora.
    //
    // La mediana e non «ieri», ed è la scelta che rende utile il confronto:
    // ieri può essere stato un temporale, e confrontarsi con un temporale non
    // dice niente. Quello che serve è «com'è di solito a quest'ora», e sette
    // giornate bastano a definirlo senza portarsi dentro il cambio di
    // stagione.
    constexpr int kTypicalDays = 7;

    const qint64 today = QDateTime::currentDateTimeUtc().date().toJulianDay();
    std::vector<float> samples;
    out.reserve(kBuckets);

    for (int bucket = 0; bucket < kBuckets; ++bucket) {
        samples.clear();
        for (int back = 1; back <= kTypicalDays; ++back) {
            const auto it = m_days.constFind(keyFor(today - back, m_bandIndex));
            if (it == m_days.constEnd() || it->device != m_device)
                continue;
            const float value = it->floor[static_cast<std::size_t>(bucket)];
            if (std::isfinite(value))
                samples.push_back(value);
        }
        out.append(samples.empty() ? std::numeric_limits<double>::quiet_NaN()
                                   : static_cast<double>(medianOf(samples)));
    }
    return out;
}

double BandConditions::nowDb() const
{
    if (m_pending.empty())
        return std::numeric_limits<double>::quiet_NaN();
    // Una copia perché la mediana riordina, e questo è un accessore const che
    // la UI chiama a ogni ridisegno: riordinare il vero significherebbe
    // rimescolare i campioni sotto a chi li sta ancora accumulando.
    std::vector<float> copy = m_pending;
    return static_cast<double>(medianOf(copy));
}

void BandConditions::recompute()
{
    m_typicalDays = 0;
    m_hasDeparture = false;
    m_departureDb = 0.0;

    if (m_bandIndex < 0)
        return;

    const qint64 today = QDateTime::currentDateTimeUtc().date().toJulianDay();
    for (int back = 1; back <= 7; ++back) {
        const auto it = m_days.constFind(keyFor(today - back, m_bandIndex));
        if (it != m_days.constEnd() && it->device == m_device)
            ++m_typicalDays;
    }

    // Lo scostamento si legge sul quarto d'ora appena chiuso, non su quello in
    // corso: quello in corso è mezzo pieno, e un numero che si assesta mentre
    // lo si guarda non si legge.
    const int bucket = currentBucket();
    const auto todayIt = m_days.constFind(keyFor(today, m_bandIndex));
    if (todayIt == m_days.constEnd())
        return;

    const float now = todayIt->floor[static_cast<std::size_t>(bucket)];
    if (!std::isfinite(now))
        return;

    std::vector<float> samples;
    for (int back = 1; back <= 7; ++back) {
        const auto it = m_days.constFind(keyFor(today - back, m_bandIndex));
        if (it == m_days.constEnd() || it->device != m_device)
            continue;
        const float value = it->floor[static_cast<std::size_t>(bucket)];
        if (std::isfinite(value))
            samples.push_back(value);
    }
    if (samples.empty())
        return;

    m_departureDb = static_cast<double>(now - medianOf(samples));
    m_hasDeparture = true;
}

void BandConditions::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

    quint32 magic = 0;
    quint16 format = 0;
    stream >> magic >> format;
    if (magic != kMagic || format != kFormat) {
        // Un file di un formato che non si conosce non si tenta di leggere a
        // metà: si lascia stare, e la storia riparte. Trenta giorni di fondo
        // di rumore non valgono un avvio che si rompe.
        qCWarning(dsdrCore) << "condizioni: formato non riconosciuto, si riparte";
        return;
    }

    quint32 count = 0;
    stream >> count;
    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        quint64 key = 0;
        quint32 device = 0;
        stream >> key >> device;

        Day day;
        day.device = device;
        for (int b = 0; b < kBuckets; ++b) {
            float value = 0.0f;
            stream >> value;
            day.floor[static_cast<std::size_t>(b)] = value;
        }
        m_days.insert(key, day);
    }

    prune();
    qCInfo(dsdrCore) << "condizioni: caricate" << m_days.size() << "giornate-banda";
}

void BandConditions::save() const
{
    const QString path = storagePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

    stream << kMagic << kFormat << static_cast<quint32>(m_days.size());
    for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it) {
        stream << it.key() << it->device;
        for (int b = 0; b < kBuckets; ++b)
            stream << it->floor[static_cast<std::size_t>(b)];
    }
}

} // namespace dsdr::core
