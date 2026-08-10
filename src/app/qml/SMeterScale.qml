// SPDX-License-Identifier: GPL-3.0-or-later
// La scala S, in un posto solo.
//
// Tre strumenti dicono quanto è forte il segnale: la barra sotto il VFO, il
// quadrante a lancetta e il display LCD della colonna. Se ognuno si calcola i
// punti S per conto suo, prima o poi dicono numeri diversi guardando lo stesso
// segnale — e non c'è modo, per chi legge, di sapere quale dei tre ha ragione.
// È già successo dentro un solo strumento: il quadrante disegnava la tacca
// «+60» là dove il suo testo leggeva «S9+18 dB».
//
// La convenzione è quella IARU Region 1: **sei decibel per punto S**, e oltre
// S9 si conta in decibel e basta. Non è una scelta estetica — è il motivo per
// cui due operatori che si scambiano un rapporto parlano della stessa cosa.
//
// Il fondo scala coincide con il tetto della dinamica: con i valori di
// fabbrica (−140 dBFS di fondo, −20 di tetto) i sessanta decibel oltre S9
// finiscono esattamente sul tetto, e i dodici decibel che avanzano sotto S1
// sono lo spazio in cui vive il rumore.
pragma Singleton

import QtQuick

QtObject {
    /// Decibel per punto S.
    readonly property real dbPerUnit: 6

    /// Decibel oltre S9 coperti dal quadrante.
    readonly property real plusRangeDb: 60

    /// Livello a cui il segnale vale S9.
    function s9Level(ceilingDb) {
        return ceilingDb - plusRangeDb
    }

    /// Punti S continui: 9 vale S9, 19 vale S9+60 dB.
    ///
    /// Continua a contare in unità da sei decibel anche oltre S9, così una sola
    /// grandezza descrive tutta la scala; chi la disegna decide poi quanto arco
    /// dare a ciascun tratto.
    function units(levelDb, ceilingDb) {
        if (!isFinite(levelDb) || !isFinite(ceilingDb))
            return 0
        const u = 9 + (levelDb - s9Level(ceilingDb)) / dbPerUnit
        return Math.max(0, Math.min(9 + plusRangeDb / dbPerUnit, u))
    }

    /// Il livello a cui cade un punto S: l'inverso di [units], per chi deve
    /// piazzare una tacca invece di leggere un valore.
    function levelFor(u, ceilingDb) {
        return s9Level(ceilingDb) + (u - 9) * dbPerUnit
    }

    /// Decibel oltre S9, zero se il segnale non ci arriva.
    function plusDb(levelDb, ceilingDb) {
        return Math.max(0, (units(levelDb, ceilingDb) - 9) * dbPerUnit)
    }

    /// La lettura da mostrare: «S7», oppure «S9+20» quando si è oltre.
    ///
    /// Oltre S9 il numero si arrotonda alla decina, che è come si legge un
    /// rapporto: nessuno passa «S9+17».
    function readout(levelDb, ceilingDb) {
        const u = units(levelDb, ceilingDb)
        if (u < 9)
            return "S" + Math.max(0, Math.round(u))
        const plus = Math.round(plusDb(levelDb, ceilingDb) / 10) * 10
        return plus <= 0 ? "S9" : "S9+" + plus
    }
}
