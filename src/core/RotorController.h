// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il rotore, via `rotctld` (il demone rotori di Hamlib).
//
// **Perché rotctld e non i protocolli dei costruttori.** Hamlib parla con una
// quarantina di modelli di rotore — Yaesu GS-232, Prosistel, SPID, Alfa
// SPID, Green Heron, M2 — e nessuno di noi ha quei rotori sul tavolo per
// scriverne i driver. `rotctld` è il modo di riusare quel lavoro senza linkare
// hamlib e senza copiarne una riga: è un demone che tiene la seriale e accetta
// comandi su TCP.
//
//   rotctld -m 603 -r COM7 -s 9600 -t 4533
//
// È lo stesso mestiere che fa già `RigctldDriver` per il CAT, e per le stesse
// ragioni. Cambia il protocollo — pochi verbi — e cambia una cosa che conta
// molto di più.
//
// **Un rotore è una massa su un palo.** Girarlo non è come cambiare un filtro:
// c'è un motore, ci sono relè che chiudono, e c'è un'antenna che si muove in
// cima a una torre. Da questo discendono tutte le scelte di questa classe:
//
//  · **Non insegue niente da solo.** Puntare è un comando esplicito. Un rotore
//    che parte perché qualcuno ha toccato una mappa è una sorpresa su un palo,
//    e sui pali le sorprese costano.
//
//  · **Non si martella.** Un bersaglio nuovo mentre il rotore sta ancora
//    andando al precedente non si manda, se non è passato abbastanza tempo: i
//    relè di un controller hanno una vita finita, e trascinare un cursore
//    manderebbe cento posizioni in tre secondi.
//
//  · **La fermata passa davanti a tutto.** Se qualcuno preme STOP mentre un
//    comando è in coda, STOP parte per primo.
//
//  · **Se il collegamento cade mentre il rotore gira, il rotore continua.** Non
//    possiamo fermarlo e non fingiamo di poterlo fare: lo si dice, perché chi
//    opera possa scendere in torre o staccare l'alimentazione se serve.
//
// **Due dialetti, come per il CAT.** `rotctld` accetta il prefisso `+` e
// risponde con righe `Nome: valore` chiuse da `RPRT n`; i server minimi
// rispondono i soli valori, una riga per ciascuno, senza esito. Il dialetto si
// stabilisce all'apertura con una domanda di sola lettura.
#pragma once

#include <QDeadlineTimer>
#include <QObject>
#include <QQueue>
#include <QString>

class QTcpSocket;
class QTimer;

namespace dsdr::core {

class RotorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY settingsChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY settingsChanged)

    /// Dove punta l'antenna adesso, in gradi. Negativo finché non lo si sa.
    Q_PROPERTY(double azimuth READ azimuth NOTIFY positionChanged)
    Q_PROPERTY(double elevation READ elevation NOTIFY positionChanged)

    /// Se il rotore ha anche l'elevazione. Falso non vuol dire «zero gradi»:
    /// vuol dire che non c'è, e un indicatore d'elevazione fermo sullo zero
    /// sarebbe una bugia che nessuno può smentire guardandolo.
    Q_PROPERTY(bool hasElevation READ hasElevation NOTIFY settingsChanged)

    /// Dove gli si è detto di andare. Negativo: nessun comando in corso.
    Q_PROPERTY(double targetAzimuth READ targetAzimuth NOTIFY positionChanged)

    /// Se si sta muovendo. Non lo dichiara il rotore — lo si deduce dal fatto
    /// che la posizione è ancora lontana dal bersaglio, ed è l'unico modo
    /// perché il protocollo non ha un «sto andando».
    Q_PROPERTY(bool moving READ isMoving NOTIFY positionChanged)

    /// Il nome del rotore, se il server sa dirlo.
    Q_PROPERTY(QString model READ model NOTIFY connectionChanged)

    /// L'ultimo guaio, per chi opera. Vuoto se va tutto bene.
    Q_PROPERTY(QString trouble READ trouble NOTIFY troubleChanged)

    /// Ogni quanto si chiede la posizione, in millisecondi.
    Q_PROPERTY(int pollMs READ pollMs WRITE setPollMs NOTIFY settingsChanged)

public:
    explicit RotorController(QObject *parent = nullptr);
    ~RotorController() override;

    bool isConnected() const;
    QString host() const { return m_host; }
    void setHost(const QString &host);
    int port() const { return m_port; }
    void setPort(int port);

    double azimuth() const { return m_azimuth; }
    double elevation() const { return m_elevation; }
    bool hasElevation() const { return m_hasElevation; }
    double targetAzimuth() const { return m_targetAzimuth; }
    bool isMoving() const;
    QString model() const { return m_model; }
    QString trouble() const { return m_trouble; }

    int pollMs() const { return m_pollMs; }
    void setPollMs(int ms);

    /// La porta di fabbrica di `rotctld`. Non è la stessa del CAT: 4532 è
    /// rigctld, 4533 è rotctld, e scambiarle vuol dire chiedere una frequenza
    /// a un rotore.
    static constexpr int kDefaultPort = 4533;

    /// Quanto vicino al bersaglio si considera «arrivato», in gradi. Tre è la
    /// tolleranza tipica di un potenziometro di rotore: pretendere meno vuol
    /// dire un indicatore che dice «in movimento» per sempre.
    static constexpr double kArrivedDeg = 3.0;

public slots:
    void connectToRotor();
    void disconnectFromRotor();

    /// Punta. È l'unico comando che mette in moto qualcosa, ed è sempre
    /// esplicito: niente in questa classe lo chiama da sé.
    void pointTo(double azimuthDeg, double elevationDeg = 0.0);

    /// Ferma dove si trova. Passa davanti a qualunque cosa sia in coda.
    void stop();

    /// Al parcheggio, se il rotore ne ha uno.
    void park();

signals:
    void connectionChanged();
    void positionChanged();
    void settingsChanged();
    void troubleChanged();

private slots:
    void onReadyRead();
    void onSocketError();
    void poll();

private:
    /// Un comando in attesa di risposta.
    struct Pending
    {
        QByteArray command;
        /// Quante righe di valore aspettarsi. Serve solo al dialetto corto,
        /// che per le letture non manda un terminatore e va contato.
        int expectedValues = 0;
        /// Che cosa farne. Vuoto significa «basta che sia andata».
        enum class Kind { Position, Capabilities, Acknowledge } kind = Kind::Acknowledge;
    };

    void send(const Pending &pending);
    void flush();
    void handleReply(const QStringList &lines, int rprt, const Pending &pending);

    /// Ripiega sul dialetto corto. Una volta sola: rifiutati entrambi, il
    /// problema non è il dialetto.
    void fallBackToShortDialect();
    void setTrouble(const QString &message);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_pollTimer = nullptr;

    QString m_host = QStringLiteral("127.0.0.1");
    int m_port = kDefaultPort;
    int m_pollMs = 500;

    /// Se si sta parlando in forma estesa. Si stabilisce all'apertura.
    bool m_extended = true;

    /// Finché è vero si sta ancora decidendo il dialetto, e le risposte non
    /// vanno prese per buone.
    bool m_negotiating = false;

    double m_azimuth = -1.0;
    double m_elevation = 0.0;
    double m_targetAzimuth = -1.0;
    double m_targetElevation = 0.0;
    bool m_hasElevation = false;
    QString m_model;
    QString m_trouble;

    QByteArray m_incoming;
    QStringList m_replyLines;
    QQueue<Pending> m_queue;
    bool m_awaiting = false;

    /// Numera i comandi mandati. Il guinzaglio che scatta è quello del comando
    /// giusto: senza, la scadenza di uno vecchio spegnerebbe l'attesa di uno
    /// nuovo, e il dialogo si sfaserebbe di un giro.
    quint64 m_ticket = 0;

    /// Quando si potrà mandare il prossimo puntamento. I relè di un controller
    /// hanno una vita finita, e trascinare un cursore ne manderebbe cento in
    /// tre secondi.
    QDeadlineTimer m_nextMoveAllowed;
};

} // namespace dsdr::core
