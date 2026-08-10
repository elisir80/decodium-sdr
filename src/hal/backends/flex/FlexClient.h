// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il collegamento a un FlexRadio serie 6000.
//
// Apre il canale di comando su TCP 4992, fa la stretta di mano e raccoglie
// quello che la radio dice di sé. Non riceve campioni: quello è il flusso
// VITA-49 su UDP, ed è la metà del protocollo che manca (vedi
// `docs/backends/flex.md`).
//
// Serve già così. Chi ha un Flex e non sa se il problema è la rete, il
// firewall o il programma, con questo ha una risposta: il collegamento si
// apre, la radio dice versione, modello e nominativo, ed elenca le fette che
// ha aperte. È la stessa domanda a cui risponde il rilevamento, un passo più
// in là — dal «c'è» al «ci parlo».
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <memory>

class QTcpSocket;

namespace dsdr::hal::flex {

class FlexClient : public QObject
{
    Q_OBJECT

public:
    explicit FlexClient(QObject *parent = nullptr);
    ~FlexClient() override;

    void connectTo(const QString &address, int timeoutMs = 4000);
    void disconnectFrom();
    bool isConnected() const;

    /// Manda un comando e restituisce il numero d'ordine con cui tornerà la
    /// risposta. Zero se il collegamento non è aperto.
    quint32 send(const QString &command);

    QString version() const { return m_version; }
    QString handle() const { return m_handle; }

    /// Quello che la radio ha detto di sé, raccolto dagli stati che manda da
    /// sé senza che nessuno chieda.
    QHash<QString, QString> radioFields() const { return m_radio; }
    QStringList slices() const;

signals:
    void connected();
    void disconnected();
    void failed(const QString &reason);

    /// La radio ha detto qualcosa di sé: versione, modello, fette.
    void described(const QString &summary);

    void responseReceived(quint32 sequence, quint32 code, const QString &payload);

private slots:
    void readLines();

private:
    void handleLine(const QString &raw);

    std::unique_ptr<QTcpSocket> m_socket;
    QByteArray m_buffer;
    QString m_version;
    QString m_handle;
    QHash<QString, QString> m_radio;
    QHash<QString, QString> m_slices;
    quint32 m_sequence = 0;
};

} // namespace dsdr::hal::flex
