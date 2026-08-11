// SPDX-License-Identifier: GPL-3.0-or-later
// Marcatori sullo spettro: «questo torna».
//
// Non sono memorie e non sono canali. Una memoria è un posto dove si va, un
// marcatore è un posto che si tiene d'occhio: la portante che compare tutti i
// giorni alla stessa ora, il disturbo che si vuole capire da dove viene, la
// frequenza su cui un corrispondente ha detto che riprova fra un'ora. Restano
// disegnati sullo spettro e non ci si sintonizza sopra.
//
// Un singleton perché sopravvivono al pannello che li disegna e alla sessione
// che li ha visti nascere.
pragma Singleton

import QtCore
import QtQuick

QtObject {
    id: root

    /// I marcatori, ciascuno con `frequency` in hertz e una `note` breve.
    property var entries: []

    property Settings store: Settings {
        category: "spectrum-markers"
        property string markersJson: "[]"
    }

    /// Quanto vicino deve essere un clic per contare come «su quel marcatore».
    ///
    /// In hertz e non in pixel: chi ci clicca sopra lo fa con lo zoom che ha, e
    /// una tolleranza in pixel a piena banda coprirebbe mezza banda amatoriale.
    /// Cinquanta hertz sono meno della larghezza di qualunque segnale che si
    /// voglia marcare, quindi due marcatori distinti restano distinti.
    readonly property int toleranceHz: 50

    function indexNear(hz) {
        let best = -1
        let bestDistance = toleranceHz
        for (let i = 0; i < entries.length; ++i) {
            const distance = Math.abs(entries[i].frequency - hz)
            if (distance <= bestDistance) {
                best = i
                bestDistance = distance
            }
        }
        return best
    }

    /// Mette un marcatore, o toglie quello che c'è già lì.
    ///
    /// Lo stesso gesto nei due versi: chi ne ha messo uno per sbaglio lo toglie
    /// ripetendo quello che ha appena fatto, senza cercare un comando.
    function toggle(hz, note) {
        const target = Math.round(hz)
        const existing = indexNear(target)
        if (existing >= 0) {
            const kept = entries.slice()
            kept.splice(existing, 1)
            entries = kept
            persist()
            return false
        }

        const next = entries.slice()
        next.push({ frequency: target, note: note === undefined ? "" : note })
        // In ordine di frequenza: l'elenco si legge come si legge lo spettro.
        next.sort((a, b) => a.frequency - b.frequency)
        entries = next
        persist()
        return true
    }

    function clear() {
        entries = []
        persist()
    }

    /// L'etichetta di un marcatore: la nota se c'è, altrimenti la frequenza.
    ///
    /// In kHz con tre decimali e non in MHz: un marcatore lo si legge accanto
    /// ad altri marcatori vicini, e le prime cifre — uguali per tutti — non
    /// aiutano a distinguerli.
    function label(entry) {
        if (entry.note)
            return entry.note
        return (entry.frequency / 1000).toFixed(3)
    }

    function persist() {
        store.markersJson = JSON.stringify(entries)
    }

    Component.onCompleted: {
        try {
            const saved = JSON.parse(store.markersJson)
            if (Array.isArray(saved))
                entries = saved
        } catch (error) {
            entries = []
        }
    }
}
