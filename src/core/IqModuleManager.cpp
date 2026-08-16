// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/IqModuleManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

Q_LOGGING_CATEGORY(dsdrIqModules, "dsdr.iqmodules")

namespace dsdr::core {

namespace {
constexpr auto kDirectoryKey = "iqModules/directoriesV1";
constexpr auto kManualKey = "iqModules/manualPathsV1";
constexpr auto kEnabledKey = "iqModules/enabledPathsV1";

QStringList moduleFilters()
{
#if defined(Q_OS_MACOS)
    return {QStringLiteral("*.dylib"), QStringLiteral("*.so")};
#elif defined(Q_OS_WIN)
    return {QStringLiteral("*.dll")};
#else
    return {QStringLiteral("*.so")};
#endif
}

QString originForDirectory(const QString &directory, const QStringList &standard)
{
    if (!standard.isEmpty() && directory == standard.constFirst())
        return QStringLiteral("bundle");
    if (standard.size() > 1 && directory == standard.at(1))
        return QStringLiteral("user");
    return QStringLiteral("directory");
}
} // namespace

IqModuleManager::IqModuleManager(QObject *parent)
    : QObject(parent)
{
    load();
}

QString IqModuleManager::normalisePath(const QString &path)
{
    const QFileInfo info(path.trimmed());
    if (info.filePath().isEmpty())
        return {};
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool IqModuleManager::isModuleFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
#if defined(Q_OS_MACOS)
    return suffix == QLatin1String("dylib") || suffix == QLatin1String("so");
#elif defined(Q_OS_WIN)
    return suffix == QLatin1String("dll");
#else
    return suffix == QLatin1String("so");
#endif
}

QStringList IqModuleManager::standardDirectories()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return {appDir.absoluteFilePath(QStringLiteral("../PlugIns/DecodiumSdr")),
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + QStringLiteral("/modules")};
}

QStringList IqModuleManager::directories() const
{
    QStringList result = standardDirectories();
    for (const QString &directory : m_customDirectories) {
        if (!result.contains(directory))
            result.append(directory);
    }
    return result;
}

QStringList IqModuleManager::enabledPaths() const
{
    return m_enabledPaths;
}

QStringList IqModuleManager::activeNames() const
{
    QStringList names;
    for (const QVariant &value : m_catalog) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("loaded")).toBool())
            names.append(record.value(QStringLiteral("name")).toString());
    }
    return names;
}

int IqModuleManager::indexOfPath(const QString &path) const
{
    const QString normalised = normalisePath(path);
    for (int index = 0; index < m_catalog.size(); ++index) {
        if (m_catalog.at(index).toMap().value(QStringLiteral("path")).toString() == normalised)
            return index;
    }
    return -1;
}

void IqModuleManager::refreshState(QVariantMap &record) const
{
    const bool enabled = record.value(QStringLiteral("enabled")).toBool();
    const bool loaded = record.value(QStringLiteral("loaded")).toBool();
    const QString error = record.value(QStringLiteral("error")).toString();
    if (loaded)
        record.insert(QStringLiteral("state"), QStringLiteral("active"));
    else if (!enabled)
        record.insert(QStringLiteral("state"), QStringLiteral("disabled"));
    else if (!error.isEmpty())
        record.insert(QStringLiteral("state"), QStringLiteral("error"));
    else
        record.insert(QStringLiteral("state"), QStringLiteral("ready"));
}

void IqModuleManager::ensureRecord(const QString &path, const QString &origin, bool present)
{
    const QString normalised = normalisePath(path);
    if (normalised.isEmpty())
        return;
    const int index = indexOfPath(normalised);
    QVariantMap record = index >= 0 ? m_catalog.at(index).toMap() : QVariantMap{};
    record.insert(QStringLiteral("path"), normalised);
    record.insert(QStringLiteral("name"), record.value(QStringLiteral("name"),
                                                         QFileInfo(normalised).completeBaseName()));
    record.insert(QStringLiteral("origin"), origin);
    record.insert(QStringLiteral("present"), present);
    record.insert(QStringLiteral("enabled"), m_enabledPaths.contains(normalised));
    if (!record.contains(QStringLiteral("loaded")))
        record.insert(QStringLiteral("loaded"), false);
    if (!record.contains(QStringLiteral("error")))
        record.insert(QStringLiteral("error"), QString());
    refreshState(record);
    if (index < 0)
        m_catalog.append(record);
    else
        m_catalog[index] = record;
}

void IqModuleManager::rescan()
{
    // Prima di cercare segniamo gli elementi esterni come assenti; i moduli
    // attivi restano comunque visibili finché il DSP li tiene caricati.
    for (int index = 0; index < m_catalog.size(); ++index) {
        QVariantMap record = m_catalog.at(index).toMap();
        if (record.value(QStringLiteral("origin")).toString() != QLatin1String("manual"))
            record.insert(QStringLiteral("present"), false);
        m_catalog[index] = record;
    }

    const QStringList standard = standardDirectories();
    for (const QString &directoryPath : directories()) {
        const QDir directory(directoryPath);
        if (!directory.exists())
            continue;
        const QString origin = originForDirectory(directory.absolutePath(), standard);
        const QFileInfoList files = directory.entryInfoList(moduleFilters(),
                                                            QDir::Files | QDir::Readable,
                                                            QDir::Name);
        for (const QFileInfo &file : files)
            ensureRecord(file.absoluteFilePath(), origin, true);
    }
    for (const QString &path : m_manualPaths)
        ensureRecord(path, QStringLiteral("manual"), QFileInfo::exists(path));

    std::sort(m_catalog.begin(), m_catalog.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("path")).toString()
             < right.toMap().value(QStringLiteral("path")).toString();
    });
    save();
    publish();
}

bool IqModuleManager::addDirectory(const QString &path)
{
    const QDir directory(path.trimmed());
    if (!directory.exists())
        return false;
    const QString normalised = QFileInfo(directory.absolutePath()).canonicalFilePath();
    const QString finalPath = normalised.isEmpty() ? directory.absolutePath() : normalised;
    if (standardDirectories().contains(finalPath) || m_customDirectories.contains(finalPath))
        return true;
    m_customDirectories.append(finalPath);
    rescan();
    qCInfo(dsdrIqModules) << "cartella moduli IQ aggiunta:" << finalPath;
    return true;
}

bool IqModuleManager::removeDirectory(const QString &path)
{
    const QString normalised = normalisePath(path);
    const int index = m_customDirectories.indexOf(normalised);
    if (index < 0)
        return false;
    m_customDirectories.removeAt(index);
    save();
    rescan();
    return true;
}

bool IqModuleManager::addModule(const QString &path)
{
    const QString normalised = normalisePath(path);
    if (normalised.isEmpty() || !QFileInfo::exists(normalised) || !isModuleFile(normalised))
        return false;
    if (!m_manualPaths.contains(normalised))
        m_manualPaths.append(normalised);
    ensureRecord(normalised, QStringLiteral("manual"), true);
    save();
    publish();
    qCInfo(dsdrIqModules) << "modulo IQ registrato:" << normalised;
    return true;
}

bool IqModuleManager::forgetModule(const QString &path)
{
    const QString normalised = normalisePath(path);
    const int index = indexOfPath(normalised);
    if (index < 0)
        return false;
    m_manualPaths.removeAll(normalised);
    m_enabledPaths.removeAll(normalised);
    m_catalog.removeAt(index);
    save();
    publish();
    return true;
}

bool IqModuleManager::setEnabled(const QString &path, bool enabled)
{
    const QString normalised = normalisePath(path);
    if (normalised.isEmpty())
        return false;
    if (indexOfPath(normalised) < 0)
        ensureRecord(normalised, QStringLiteral("manual"), QFileInfo::exists(normalised));
    if (enabled) {
        if (!m_enabledPaths.contains(normalised))
            m_enabledPaths.append(normalised);
    } else {
        m_enabledPaths.removeAll(normalised);
    }
    const int index = indexOfPath(normalised);
    QVariantMap record = m_catalog.at(index).toMap();
    record.insert(QStringLiteral("enabled"), enabled);
    if (!enabled)
        record.insert(QStringLiteral("error"), QString());
    refreshState(record);
    m_catalog[index] = record;
    save();
    publish();
    return true;
}

void IqModuleManager::markLoadResult(const QString &path, bool loaded, const QString &name,
                                     const QString &error)
{
    const QString normalised = normalisePath(path);
    if (normalised.isEmpty())
        return;
    if (indexOfPath(normalised) < 0)
        ensureRecord(normalised, QStringLiteral("manual"), QFileInfo::exists(normalised));
    const int index = indexOfPath(normalised);
    QVariantMap record = m_catalog.at(index).toMap();
    record.insert(QStringLiteral("loaded"), loaded);
    if (!name.isEmpty())
        record.insert(QStringLiteral("name"), name);
    record.insert(QStringLiteral("error"), loaded ? QString() : error);
    refreshState(record);
    m_catalog[index] = record;
    save();
    publish();
}

void IqModuleManager::markUnloaded(const QString &path)
{
    const int index = indexOfPath(path);
    if (index < 0)
        return;
    QVariantMap record = m_catalog.at(index).toMap();
    record.insert(QStringLiteral("loaded"), false);
    refreshState(record);
    m_catalog[index] = record;
    save();
    publish();
}

void IqModuleManager::markAllUnloaded()
{
    bool changed = false;
    for (int index = 0; index < m_catalog.size(); ++index) {
        QVariantMap record = m_catalog.at(index).toMap();
        if (!record.value(QStringLiteral("loaded")).toBool())
            continue;
        record.insert(QStringLiteral("loaded"), false);
        refreshState(record);
        m_catalog[index] = record;
        changed = true;
    }
    if (!changed)
        return;
    save();
    publish();
}

void IqModuleManager::load()
{
    QSettings settings;
    m_customDirectories = settings.value(QString::fromLatin1(kDirectoryKey)).toStringList();
    m_manualPaths = settings.value(QString::fromLatin1(kManualKey)).toStringList();
    m_enabledPaths = settings.value(QString::fromLatin1(kEnabledKey)).toStringList();

    for (QString &path : m_customDirectories)
        path = normalisePath(path);
    for (QString &path : m_manualPaths)
        path = normalisePath(path);
    for (QString &path : m_enabledPaths)
        path = normalisePath(path);
    m_customDirectories.removeAll(QString());
    m_manualPaths.removeAll(QString());
    m_enabledPaths.removeAll(QString());
    m_customDirectories.removeDuplicates();
    m_manualPaths.removeDuplicates();
    m_enabledPaths.removeDuplicates();
    rescan();
}

void IqModuleManager::save() const
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kDirectoryKey), m_customDirectories);
    settings.setValue(QString::fromLatin1(kManualKey), m_manualPaths);
    settings.setValue(QString::fromLatin1(kEnabledKey), m_enabledPaths);
    settings.sync();
}

void IqModuleManager::publish()
{
    emit catalogChanged();
}

} // namespace dsdr::core
