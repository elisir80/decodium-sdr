// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — catalogo persistente dei moduli IQ C ABI.
//
// Questo oggetto scopre e ricorda le librerie, ma non le esegue. Caricare una
// dylib/.so è un confine di fiducia e resta una decisione di SessionManager,
// che la inoltra al thread DSP. Così una scansione non esegue codice esterno.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace dsdr::core {

class IqModuleManager : public QObject
{
    Q_OBJECT

public:
    explicit IqModuleManager(QObject *parent = nullptr);

    QVariantList catalog() const { return m_catalog; }
    QStringList directories() const;
    QStringList enabledPaths() const;
    QStringList activeNames() const;

    void rescan();
    bool addDirectory(const QString &path);
    bool removeDirectory(const QString &path);
    bool addModule(const QString &path);
    bool forgetModule(const QString &path);
    bool setEnabled(const QString &path, bool enabled);

    /// Risultato dell'unica parte che esegue la libreria, tenuta fuori dalla
    /// discovery: `loaded` è lo stato osservato, `enabled` il desiderio
    /// persistente dell'operatore.
    void markLoadResult(const QString &path, bool loaded, const QString &name,
                        const QString &error = {});
    void markUnloaded(const QString &path);
    void markAllUnloaded();

signals:
    void catalogChanged();

private:
    static QString normalisePath(const QString &path);
    static bool isModuleFile(const QString &path);
    static QStringList standardDirectories();
    int indexOfPath(const QString &path) const;
    void ensureRecord(const QString &path, const QString &origin, bool present);
    void refreshState(QVariantMap &record) const;
    void load();
    void save() const;
    void publish();

    QVariantList m_catalog;
    QStringList m_customDirectories;
    QStringList m_manualPaths;
    QStringList m_enabledPaths;
};

} // namespace dsdr::core
