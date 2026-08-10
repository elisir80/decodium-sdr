// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/neural/ModelStore.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace dsdr::dsp::neural {

namespace {

/// Un modello DeepFilterNet è un archivio di pochi megabyte. Un file più
/// piccolo di così è un download interrotto o un segnaposto: dirlo subito
/// costa niente, scoprirlo scegliendolo costa una sessione.
constexpr qint64 kMinimumBytes = 64 * 1024;

} // namespace

ModelStore::ModelStore(QObject *parent)
    : QAbstractListModel(parent)
{
    m_directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/models");
    refresh();
}

QString ModelStore::engineForFile(const QString &fileName)
{
    const QString lower = fileName.toLower();

    // I modelli di DeepFilterNet sono archivi tar: è l'estensione che li
    // distingue, non il nome, perché il nome lo sceglie chi li allena.
    if (lower.endsWith(QStringLiteral(".tar.gz")) || lower.endsWith(QStringLiteral(".tar")))
        return QStringLiteral("dfn3");
    if (lower.endsWith(QStringLiteral(".rnnn")))
        return QStringLiteral("rnnoise");
    return QString();
}

void ModelStore::refresh()
{
    beginResetModel();
    m_models.clear();

    QDir dir(m_directory);
    if (!dir.exists()) {
        // Si crea: chi cerca la cartella per metterci un file deve trovarla,
        // non doverla indovinare.
        QDir().mkpath(m_directory);
    }

    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);

    for (const QFileInfo &entry : entries) {
        const QString engine = engineForFile(entry.fileName());
        if (engine.isEmpty())
            continue;   // non è un modello: non è un errore, è un altro file

        NrModel model;
        model.name = entry.completeBaseName();
        model.path = entry.absoluteFilePath();
        model.bytes = entry.size();
        model.engineId = engine;

        if (model.bytes < kMinimumBytes) {
            model.usable = false;
            model.problem = tr("File troppo piccolo (%1 byte): probabilmente "
                               "un download interrotto.").arg(model.bytes);
        }

        m_models.append(model);
    }

    endResetModel();
    emit changed();
}

QString ModelStore::pathAt(int row) const
{
    if (row < 0 || row >= m_models.size())
        return QString();
    return m_models.at(row).path;
}

int ModelStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_models.size();
}

QVariant ModelStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_models.size())
        return QVariant();

    const NrModel &model = m_models.at(index.row());
    switch (role) {
    case NameRole:    return model.name;
    case PathRole:    return model.path;
    case EngineRole:  return model.engineId;
    case UsableRole:  return model.usable;
    case ProblemRole: return model.problem;
    case SizeRole:    return model.bytes;
    default:          return QVariant();
    }
}

QHash<int, QByteArray> ModelStore::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {EngineRole, "engineId"},
        {UsableRole, "usable"},
        {ProblemRole, "problem"},
        {SizeRole, "bytes"},
    };
}

} // namespace dsdr::dsp::neural
