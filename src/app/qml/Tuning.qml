// SPDX-License-Identifier: GPL-3.0-or-later
// Passo di sintonia.
//
// Un singleton perché il passo è uno solo per tutta l'applicazione: la
// rotellina sullo spettro, quella sul pannello del canale e i pulsanti della
// targa devono muovere la frequenza della stessa quantità. Tenerne una copia
// per componente vorrebbe dire scoprire un giorno che due rotelline si
// comportano diversamente.
//
// Prima il passo era cablato in due punti — cento hertz, dieci con Shift,
// mille con Ctrl — e non c'era modo di sapere quale fosse senza provarlo.
pragma Singleton

import QtCore
import QtQuick

QtObject {
    id: root

    /// I passi disponibili, in hertz. Sono quelli che si usano davvero: uno
    /// per il battimento fine in CW, dieci e cento per cercare in banda, mille
    /// e diecimila per spostarsi fra i segmenti.
    readonly property var steps: [1, 10, 100, 1000, 10000]

    /// Passo corrente, in hertz.
    property int stepHz: 100

    /// Etichetta breve di un passo.
    function label(hz) {
        if (hz >= 1000)
            return (hz / 1000) + qsTr("k")
        return String(hz)
    }

    /// Passo successivo o precedente nell'elenco, senza uscirne.
    ///
    /// Il ciclo non gira: arrivati a diecimila hertz un ulteriore scatto
    /// riporterebbe a un hertz, e su una manopola che si usa al volo è il
    /// genere di sorpresa che fa perdere il segnale.
    function shift(direction) {
        const at = steps.indexOf(stepHz)
        const next = Math.max(0, Math.min(steps.length - 1, (at < 0 ? 2 : at) + direction))
        stepHz = steps[next]
    }

    /// Il passo sopravvive alla chiusura: è una preferenza di chi opera, non
    /// uno stato della sessione.
    property Settings persisted: Settings {
        category: "tuning"
        property alias stepHz: root.stepHz
    }
}
