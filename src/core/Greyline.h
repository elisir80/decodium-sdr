// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — la linea grigia, e dove puntare l'antenna.
//
// **A che cosa serve davvero.** Sulle bande basse le aperture buone non durano
// tutto il giorno: durano i minuti attorno all'alba e al tramonto, quando il
// percorso fra due stazioni corre lungo la fascia in cui il Sole sta appena
// sotto l'orizzonte. Lì la banda D — quella che di giorno assorbe gli 80 e i
// 160 metri — si è già dissolta, mentre la F è ancora ionizzata. Sapere dove
// passa quella fascia adesso è la differenza fra chiamare e chiamare al
// momento giusto.
//
// **Che cosa calcola, e che cosa no.** Qui c'è la geometria: posizione del
// Sole, terminatore, distanza angolare, rotta ortodromica. Non c'è nessuna
// previsione di propagazione — quella dipende dal flusso solare, dall'indice
// K, dall'antenna e dalla potenza, e un numero inventato su quelle basi
// sarebbe peggio di nessun numero.
//
// **Nessuna dipendenza da Qt Positioning, e non è una svista.** Il modulo
// originale usava `QGeoCoordinate`, che trascina Qt6::Positioning per portare
// due double e una distanza sferica. La distanza sferica sono quattro righe di
// trigonometria, e tenerla qui vuol dire che il motore si compila e si prova
// senza altro che Qt Core.
//
// **Precisione.** La declinazione sta entro il centesimo di grado fra il 1950
// e il 2050: è l'algoritmo NOAA in forma ridotta. Più di così non servirebbe,
// perché resta un errore **fisico** che nessun calcolo toglie — il terminatore
// radio non coincide con quello ottico. Gli strati ionizzati stanno a
// centinaia di chilometri di quota e restano illuminati quando la superficie
// è già al buio, quindi la linea grigia utile è spostata e sfumata di qualche
// grado. Per questo la fascia ha una semilarghezza regolabile invece di essere
// una riga.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointF>
#include <QVariantList>

#include <vector>

class QTimer;

namespace dsdr::core {

/// Un punto sulla Terra. `x` è la longitudine, `y` la latitudine — in
/// quest'ordine perché è quello di un piano cartesiano e di ogni formato
/// geografico, e invertirlo è l'errore che si fa una volta e si insegue per
/// mezza giornata.
using GeoPoint = QPointF;

class Greyline : public QObject
{
    Q_OBJECT

    /// Le quattro bande di crepuscolo, dalla più esterna alla più interna.
    ///
    /// Sono **dischi concentrici attorno al punto antisolare**, non anelli: un
    /// punto in cui il Sole sta a −h di altitudine dista (90 − h) gradi
    /// dall'antisolare. Disegnandoli dal più grande al più piccolo con la
    /// stessa trasparenza si sommano, e la sfumatura viene da sé senza
    /// gradienti.
    Q_PROPERTY(QVariantList civilBand READ civilBand NOTIFY changed)
    Q_PROPERTY(QVariantList nauticalBand READ nauticalBand NOTIFY changed)
    Q_PROPERTY(QVariantList astroBand READ astroBand NOTIFY changed)
    Q_PROPERTY(QVariantList nightBand READ nightBand NOTIFY changed)

    /// Il terminatore vero, quello ad altitudine zero.
    Q_PROPERTY(QVariantList terminator READ terminator NOTIFY changed)

    Q_PROPERTY(QPointF subsolar READ subsolar NOTIFY changed)
    Q_PROPERTY(QPointF antisolar READ antisolar NOTIFY changed)

    /// L'istante di riferimento, in UTC. Non valido significa «adesso»: è così
    /// che si guarda una mappa di propagazione, e mettere l'ora corrente come
    /// valore iniziale la congelerebbe al primo avvio.
    Q_PROPERTY(QDateTime utc READ utc WRITE setUtc RESET resetUtc NOTIFY changed)

    /// Di quanto si sta guardando avanti o indietro, in ore. È il comando che
    /// serve davvero: «com'è fra tre ore» si chiede più spesso di «com'era il
    /// 12 marzo alle 04:17».
    Q_PROPERTY(double hourOffset READ hourOffset WRITE setHourOffset NOTIFY changed)

    /// Punti per disco. Centottanta sono due gradi di rotta: su una mappa da
    /// mille pixel la spezzata non si distingue da una curva.
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY changed)

    /// Ogni quanto si ricalcola, in secondi. Zero lo ferma.
    ///
    /// Sessanta bastano: il terminatore si sposta un quarto di grado al
    /// minuto. Sotto i trenta non si aggiunge informazione, si aggiunge lavoro.
    Q_PROPERTY(int refreshSeconds READ refreshSeconds WRITE setRefreshSeconds
                   NOTIFY refreshSecondsChanged)

    /// Semilarghezza della fascia utile, in gradi di altitudine solare.
    Q_PROPERTY(double greylineHalfWidth READ greylineHalfWidth WRITE setGreylineHalfWidth
                   NOTIFY changed)

public:
    explicit Greyline(QObject *parent = nullptr);
    ~Greyline() override;

    QVariantList civilBand() const { return m_civil; }
    QVariantList nauticalBand() const { return m_nautical; }
    QVariantList astroBand() const { return m_astro; }
    QVariantList nightBand() const { return m_night; }
    QVariantList terminator() const { return m_civil; }

    QPointF subsolar() const { return m_subsolar; }
    QPointF antisolar() const { return m_antisolar; }

    QDateTime utc() const { return m_utc; }
    void setUtc(const QDateTime &t);
    void resetUtc();

    double hourOffset() const { return m_hourOffset; }
    void setHourOffset(double hours);

    int resolution() const { return m_resolution; }
    void setResolution(int points);

    int refreshSeconds() const { return m_refreshSeconds; }
    void setRefreshSeconds(int seconds);

    double greylineHalfWidth() const { return m_halfWidth; }
    void setGreylineHalfWidth(double degrees);

    /// L'istante a cui si riferisce il calcolo in corso.
    Q_INVOKABLE QDateTime referenceTime() const;

    // ── Interrogazioni su un punto ───────────────────────────────────────

    /// Altitudine del Sole in gradi. Negativa sotto l'orizzonte.
    Q_INVOKABLE double sunAltitude(double lat, double lon) const;

    /// Se il punto sta nella fascia utile.
    Q_INVOKABLE bool inGreyline(double lat, double lon) const;

    /// Che cosa sta succedendo lì: 0 giorno, 1 linea grigia, 2 notte.
    Q_INVOKABLE int condition(double lat, double lon) const;

    // ── Rotta e distanza ─────────────────────────────────────────────────

    /// Rotta ortodromica iniziale, in gradi da nord.
    ///
    /// «Iniziale» conta: su un percorso lungo la rotta cambia strada facendo,
    /// e quella da mettere nel rotore è quella di partenza. È il numero che un
    /// controller di rotore mostra, e chiamarlo «azimut» senza dire quale
    /// sarebbe una comodità pagata da chi punta l'antenna.
    Q_INVOKABLE double bearing(double fromLat, double fromLon,
                               double toLat, double toLon) const;

    /// La rotta lunga: la stessa direzione, dall'altra parte.
    ///
    /// Non è un vezzo. Sulle bande alte, quando la via breve passa sopra la
    /// calotta polare disturbata, la via lunga è spesso l'unica che porta il
    /// segnale — e sono centottanta gradi di rotore, non un ritocco.
    Q_INVOKABLE double longPathBearing(double fromLat, double fromLon,
                                       double toLat, double toLon) const;

    /// Distanza in chilometri lungo l'ortodromia.
    Q_INVOKABLE double distanceKm(double fromLat, double fromLon,
                                  double toLat, double toLon) const;

    /// I punti dell'ortodromia fra due stazioni, per disegnarla.
    ///
    /// Spezzata dove scavalca l'antimeridiano: in equirettangolare una rotta
    /// che salta da +179 a −179 disegnerebbe una riga che attraversa tutta la
    /// mappa, e sembrerebbe un percorso.
    Q_INVOKABLE QVariantList greatCircle(double fromLat, double fromLon,
                                         double toLat, double toLon,
                                         int points = 90) const;

    /// Quanti chilometri di quel percorso stanno nella fascia grigia.
    ///
    /// È la misura che dice se conviene chiamare adesso, e non si legge da una
    /// mappa a occhio: un percorso può attraversare il terminatore di
    /// sbieco per migliaia di chilometri o tagliarlo di netto in duecento.
    Q_INVOKABLE double greylineOverlapKm(double fromLat, double fromLon,
                                         double toLat, double toLon) const;

    // ── Locatore Maidenhead ──────────────────────────────────────────────
    //
    // Sta qui perché chi opera pensa in locatori, non in gradi decimali: un
    // corrispondente dice «JN71» e non «41.0 nord, 14.2 est».

    /// Da locatore a coordinate — il centro del quadrato. Un locatore non
    /// valido restituisce un punto non valido, e chi lo riceve deve dirlo
    /// invece di piazzare una stazione nel golfo di Guinea.
    Q_INVOKABLE QPointF fromLocator(const QString &locator) const;

    /// Da coordinate a locatore, sei caratteri.
    Q_INVOKABLE QString toLocator(double lat, double lon) const;

public slots:
    void recompute();

signals:
    void changed();
    void refreshSecondsChanged();

private:
    /// Declinazione del Sole e longitudine del punto subsolare, in gradi.
    static void solarPosition(const QDateTime &utc, double &declination,
                              double &subsolarLon);

    /// Il cerchio geodetico di raggio `radiusDeg` attorno a un punto.
    QVariantList circleAround(const QPointF &center, double radiusDeg) const;

    QVariantList m_civil, m_nautical, m_astro, m_night;
    QPointF m_subsolar, m_antisolar;

    QDateTime m_utc;
    double m_hourOffset = 0.0;
    int m_resolution = 180;
    int m_refreshSeconds = 60;
    double m_halfWidth = 6.0;

    /// Declinazione dell'istante calcolato: serve a `sunAltitude`, che
    /// altrimenti dovrebbe rifare tutta la posizione solare a ogni punto.
    double m_declination = 0.0;
    double m_subsolarLon = 0.0;

    QTimer *m_timer = nullptr;
};

} // namespace dsdr::core
