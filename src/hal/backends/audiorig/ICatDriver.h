// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il piano di controllo di una radio tradizionale (SPEC-004 §2).
//
// Una radio senza uscita IF non è muta: sa dire su che frequenza è, con che
// modo, e quanto sta ricevendo. Lo dice via CAT, e questo è il seam interno
// dietro cui stanno i modi di chiederglielo — il driver nativo newcat per le
// Yaesu, hamlib per tutto il resto.
//
// Sta dentro il backend e non nella HAL: è un dettaglio di *come* `audiorig`
// parla con la sua radio, esattamente come il protocollo USB è un dettaglio
// del ColibriNANO. Sopra la HAL nessuno sa che esista un CAT.
//
// Tutti i metodi sono chiamati dal thread del controller, mai dal seam.
#pragma once

#include "common/Types.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <limits>

namespace dsdr::hal::audiorig {

/// Ciò che una lettura CAT restituisce. I campi che la radio non sa dire
/// restano al valore precedente: un driver non deve inventare.
struct CatState
{
    qint64 frequencyHz = 0;
    DemodMode mode = DemodMode::Usb;
    bool transmitting = false;
    /// Lettura grezza dell'S-meter, 0…255. La conversione in punti S dipende
    /// dal modello e sta nel profilo, non qui (SPEC-004 §8.2).
    int sMeterRaw = -1;

    /// Il livello in dBm, quando il driver lo sa davvero.
    ///
    /// La lettura grezza è quella che una radio manda sulla seriale: un numero
    /// fra 0 e 255 il cui significato dipende dal modello, e che senza il
    /// profilo di quel modello si può solo interpretare a occhio. Hamlib
    /// invece restituisce decibel rispetto a S9, cioè una misura tarata: farla
    /// passare per la scala grezza vorrebbe dire buttare via l'unica cosa che
    /// la rende utile, e riprenderla dall'altra parte con un'approssimazione.
    ///
    /// NaN quando non si sa: il chiamante allora ricade sulla lettura grezza.
    double signalDbm = std::numeric_limits<double>::quiet_NaN();
};

class ICatDriver
{
public:
    virtual ~ICatDriver() = default;

    /// Nome del driver, per la diagnostica e per il pannello.
    virtual QString driverId() const = 0;

    /// Apre la porta e verifica che dall'altra parte ci sia davvero una radio.
    /// Fallire qui è normale — si sondano porte che possono essere qualunque
    /// cosa — e non deve costare più di qualche centinaio di millisecondi.
    virtual bool open(const QString &portName, int baudRate) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    /// Modello riconosciuto, se la radio l'ha detto (`ID;` per le Yaesu).
    virtual QString radioModel() const = 0;

    /// Una lettura completa. Restituisce false se il dialogo si è interrotto:
    /// il chiamante lo tratta come perdita del CAT, che è una cosa seria —
    /// senza CAT il panadattatore non sa più dove sta.
    virtual bool poll(CatState &state) = 0;

    virtual bool setFrequency(qint64 hz) = 0;
    virtual bool setMode(DemodMode mode) = 0;
    virtual bool setPtt(bool transmit) = 0;

    /// Ultimo errore leggibile da una persona.
    virtual QString errorString() const = 0;

    /// Velocità da provare in fase di sonda, dalla più probabile alla meno.
    virtual QList<int> candidateBaudRates() const = 0;

    /// Sonda una porta provando le velocità note, **aprendola una volta
    /// sola**. Restituisce la velocità che ha funzionato, o −1.
    ///
    /// Che sia una volta sola non è un'ottimizzazione: è una questione di
    /// sicurezza. Su Windows l'apertura di una porta seriale alza DTR e RTS
    /// per qualche millisecondo prima che il programma possa abbassarli, e su
    /// molte interfacce — e su una Yaesu con «CAT RTS» attivo — quelle linee
    /// **sono il PTT**. Aprire e richiudere undici volte per provare undici
    /// velocità vuol dire undici colpi di trasmissione su una radio che
    /// nessuno stava usando.
    ///
    /// Trovandola, la porta resta aperta e pronta. Non trovandola, si chiude.
    virtual int probe(const QString &portName) = 0;
};

} // namespace dsdr::hal::audiorig
