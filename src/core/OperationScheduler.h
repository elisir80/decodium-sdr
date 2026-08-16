// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — pianificatore delle sole operazioni di ricezione.
//
// Non è un timer nel DSP: vive sul thread della sessione e consegna azioni
// discrete al SessionManager. In questo modo una scadenza non può rubare tempo
// al flusso IQ e, soprattutto, l'elenco delle azioni non contiene mai PTT o TX.
#pragma once

#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace dsdr::core {

class OperationScheduler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)

public:
    explicit OperationScheduler(QObject *parent = nullptr);

    QVariantList jobs() const { return m_jobs; }
    static QStringList supportedActions();

    /// Aggiunge una scadenza UTC ISO-8601. Restituisce l'id del lavoro, oppure
    /// una stringa vuota se l'azione o la data non sono sicure/valide.
    Q_INVOKABLE QString schedule(const QString &action, const QString &whenUtc,
                                 const QVariantMap &arguments = {});
    Q_INVOKABLE bool cancel(const QString &id);
    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE void clearHistory();

    /// Punto di iniezione deterministico per i test. L'app usa il timer
    /// periodico, non deve mai dormire aspettando una scadenza.
    void evaluateAt(const QDateTime &nowUtc);

    /// Il SessionManager chiude una consegna dopo aver verificato che
    /// l'operazione abbia davvero avuto effetto sul backend/registratore.
    void complete(const QString &id, bool succeeded, const QString &message);

signals:
    void jobsChanged();
    void jobDue(const QString &id, const QString &action, const QVariantMap &arguments);

private:
    static bool isSupportedAction(const QString &action);
    static QDateTime parseUtc(const QString &whenUtc);
    static QString normaliseAction(const QString &action);
    int indexOf(const QString &id) const;
    void load();
    void save() const;
    void publish();

    QVariantList m_jobs;
    QTimer m_timer;
};

} // namespace dsdr::core
