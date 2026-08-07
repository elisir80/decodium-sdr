// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — registro dei backend disponibili.
//
// I backend si registrano qui a build-time (RNF-06). Il core interroga il
// registro e non include mai l'header di un backend concreto: è ciò che rende
// verificabile la regola "nessun #include di un backend sopra la HAL"
// (CONSTITUTION §4) con un semplice grep in CI.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace dsdr::hal {

class IRadioBackend;

struct BackendInfo
{
    QString id;
    QString displayName;
    QString description;
};

class BackendRegistry
{
public:
    using Factory = std::function<IRadioBackend *(QObject *)>;

    static BackendRegistry &instance();

    void registerBackend(const BackendInfo &info, Factory factory);

    QStringList backendIds() const;
    QList<BackendInfo> backends() const;
    BackendInfo info(const QString &id) const;
    bool contains(const QString &id) const;

    /// Costruisce un'istanza del backend. Restituisce nullptr se l'id non è
    /// registrato (backend escluso dalla build).
    IRadioBackend *create(const QString &id, QObject *parent = nullptr) const;

private:
    BackendRegistry() = default;

    QHash<QString, Factory> m_factories;
    QHash<QString, BackendInfo> m_info;
    QStringList m_order; ///< ordine di registrazione = ordine in UI
};

/// Registra tutti i backend abilitati dalle opzioni DSDR_BACKEND_*.
/// Va chiamata una sola volta all'avvio, prima di usare il registro.
void registerBuiltinBackends();

} // namespace dsdr::hal
