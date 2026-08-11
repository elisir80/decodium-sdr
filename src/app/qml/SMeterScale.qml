// SPDX-License-Identifier: GPL-3.0-or-later
// La scala S, in un posto solo.
//
// Tre strumenti dicono quanto è forte il segnale: la barra sotto il VFO, il
// quadrante a lancetta e quello della colonna. Se ognuno si calcola i punti S
// per conto suo, prima o poi dicono numeri diversi guardando lo stesso segnale
// — e non c'è modo, per chi legge, di sapere quale dei tre ha ragione.
//
// La convenzione è quella IARU Region 1: **sei decibel per punto S**, e oltre
// S9 si conta in decibel e basta. Non è una scelta estetica — è il motivo per
// cui due operatori che si scambiano un rapporto parlano della stessa cosa.
//
// ── Dove cade S9 ────────────────────────────────────────────────────────────
//
// Su una radio tarata S9 è −73 dBm all'antenna, e non c'è niente da decidere.
// Qui i livelli sono dBFS: dipendono dal guadagno della catena, dall'attenuatore,
// dal device. Non esiste un livello assoluto che valga S9 su tutti.
//
// Ancorarlo al tetto della dinamica — S9 sessanta decibel sotto il fondo scala
// — sembrava innocuo e non lo era: su un ricevitore con guadagno alto ogni
// segnale finisce oltre S9 e l'ago resta appoggiato al fermo, che è il modo più
// efficace di rendere inutile uno strumento. È successo davvero, sulla 1.1.3.
//
// Il riferimento lo passa quindi chi chiama, e chi chiama lo ricava dal fondo
// di rumore: è l'unica cosa misurata che dica dove si trova la catena. Un
// segnale al livello del rumore vale S1, e da lì si contano i sei decibel per
// punto. Non è una taratura assoluta — non lo sarebbe nemmeno fingendo — ma è
// una scala che si muove insieme al ricevitore invece che contro.
pragma Singleton

import QtQuick

QtObject {
    /// Decibel per punto S.
    readonly property real dbPerUnit: 6

    /// Decibel oltre S9 coperti dal quadrante.
    readonly property real plusRangeDb: 60

    /// Quanto sta S1 sopra il fondo di rumore. Un segnale che si distingue
    /// appena dal rumore è un S1: sotto, non lo si sente.
    readonly property real s1AboveFloorDb: 3

    /// Il livello a cui il segnale vale S9, ricavato dal fondo di rumore.
    ///
    /// Da S1 a S9 ci sono otto gradini da sei decibel: quarantotto in tutto.
    function s9From(noiseFloorDb) {
        if (!isFinite(noiseFloorDb))
            return -80
        return noiseFloorDb + s1AboveFloorDb + 8 * dbPerUnit
    }

    /// Punti S continui: 9 vale S9, 19 vale S9+60 dB.
    ///
    /// Continua a contare in unità da sei decibel anche oltre S9, così una sola
    /// grandezza descrive tutta la scala; chi la disegna decide poi quanto arco
    /// dare a ciascun tratto.
    function units(levelDb, s9Db) {
        if (!isFinite(levelDb) || !isFinite(s9Db))
            return 0
        const u = 9 + (levelDb - s9Db) / dbPerUnit
        return Math.max(0, Math.min(9 + plusRangeDb / dbPerUnit, u))
    }

    /// Il livello a cui cade un punto S: l'inverso di [units], per chi deve
    /// piazzare una tacca invece di leggere un valore.
    function levelFor(u, s9Db) {
        return s9Db + (u - 9) * dbPerUnit
    }

    /// Decibel oltre S9, zero se il segnale non ci arriva.
    function plusDb(levelDb, s9Db) {
        return Math.max(0, (units(levelDb, s9Db) - 9) * dbPerUnit)
    }

    /// La lettura da mostrare: «S7», oppure «S9+20» quando si è oltre.
    ///
    /// Oltre S9 il numero si arrotonda alla decina, che è come si legge un
    /// rapporto: nessuno passa «S9+17».
    function readout(levelDb, s9Db) {
        const u = units(levelDb, s9Db)
        if (u < 9)
            return "S" + Math.max(0, Math.round(u))
        const plus = Math.round(plusDb(levelDb, s9Db) / 10) * 10
        return plus <= 0 ? "S9" : "S9+" + plus
    }
}
