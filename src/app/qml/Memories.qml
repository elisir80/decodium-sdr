// SPDX-License-Identifier: GPL-3.0-or-later
// Memorie e bandstack: dove si vuole tornare, e dove si era rimasti.
//
// Stavano dentro `FrequencyPanel`, che è un pannello della colonna laterale.
// Finché il solo modo di cambiare banda era quello, andava bene; ora la targa
// del ricevitore lo fa a sua volta, e due copie della stessa lista sono due
// liste che divergono al primo salvataggio — si memorizza una frequenza dalla
// targa e nel pannello non c'è.
//
// Un singleton perché queste sono cose dell'operatore, non di un pannello: si
// portano dietro anche quando il pannello è chiuso.
pragma Singleton

import QtCore
import QtQuick
import DecodiumSdr

QtObject {
    id: root

    /// Dove si era rimasti su ogni banda, per nome di banda.
    ///
    /// È il comportamento che ogni operatore si aspetta: tornare sui quaranta
    /// non vuol dire ripartire dal bordo inferiore, che di solito è vuoto.
    property var bandStack: ({})

    /// Le memorie, in ordine di inserimento.
    ///
    /// Ogni voce ha sempre `frequency` e `label`; da questa versione porta
    /// anche `mode`, `filterLowHz` e `filterHighHz` quando è stata salvata da
    /// un canale. Le voci vecchie non li hanno, e vanno bene lo stesso: si
    /// richiama la frequenza e si lascia stare il resto. Cancellare le memorie
    /// di qualcuno per cambiare il formato del file non è un'opzione.
    property var entries: []

    property Settings store: Settings {
        // La stessa categoria di prima: le memorie già salvate si ritrovano al
        // loro posto, senza migrazioni.
        category: "frequency-manager"
        property string bandStackJson: "{}"
        property string favoritesJson: "[]"
    }

    /// Se il ricevitore collegato arriva a quella frequenza.
    ///
    /// Una copertura non dichiarata non vincola niente: meglio un pulsante che
    /// prova e fallisce di uno spento per un dato che il backend non ha mai
    /// fornito (CONSTITUTION §7).
    function reachable(hz) {
        const caps = Session.capabilities
        if (caps.maxFrequency <= caps.minFrequency)
            return true
        return hz >= caps.minFrequency && hz <= caps.maxFrequency
    }

    /// Porta la sintonia dove si chiede, annotando prima dove si era.
    function goTo(hz) {
        if (!Session.connected || !reachable(hz))
            return false

        const leaving = BandPlan.bandAt(Session.centerFrequency)
        if (leaving) {
            // Riassegnazione e non mutazione in luogo: `bandStack` è una
            // property var, e chi ci si lega non vede cambiare un oggetto
            // modificato dall'interno.
            const next = Object.assign({}, bandStack)
            next[leaving.name] = Session.centerFrequency
            bandStack = next
        }

        // Centro e ricevitore insieme: si premeva «20m», la finestra si
        // spostava sui quattordici megahertz e RX 1 restava sui quaranta,
        // fuori da tutto ciò che si vedeva.
        Session.tuneTo(Math.round(hz))
        persist()
        return true
    }

    /// La banda che contiene la sintonia attuale, o null.
    readonly property var currentBand: BandPlan.bandAt(Session.centerFrequency)

    /// Entra in una banda: dove la si era lasciata, o dove c'è traffico.
    function selectBand(band) {
        if (!band)
            return false
        // Premere la banda in cui si è già riporta al punto di partenza: è la
        // via d'uscita quando ci si è persi in fondo alla banda.
        const target = (currentBand && currentBand.name === band.name)
            ? band.home
            : (bandStack[band.name] !== undefined ? bandStack[band.name] : band.home)
        return goTo(target)
    }

    function bandNamed(name) {
        for (let i = 0; i < BandPlan.bands.length; ++i) {
            if (BandPlan.bands[i].name === name)
                return BandPlan.bands[i]
        }
        return null
    }

    /// Le bande che il ricevitore collegato copre davvero.
    readonly property var reachableBands: {
        const list = []
        for (let i = 0; i < BandPlan.bands.length; ++i) {
            if (reachable(BandPlan.bands[i].home))
                list.push(BandPlan.bands[i])
        }
        return list
    }

    function indexOf(hz) {
        const target = Math.round(hz)
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].frequency === target)
                return i
        }
        return -1
    }

    /// Memorizza un canale così com'è: frequenza, modo e filtro.
    ///
    /// Una memoria di sola frequenza richiamata su una banda in SSB riporta il
    /// numero giusto e il suono sbagliato — e il modo è proprio la cosa che si
    /// dimentica di rimettere.
    ///
    /// I valori arrivano da chi chiama e non si leggono dal modello: il modello
    /// dei canali non espone le righe a QML se non attraverso un delegate, e
    /// chi ha questi numeri sotto gli occhi — la targa — li ha già tutti.
    function storeChannel(hz, mode, modeName, filterLowHz, filterHighHz) {
        const target = Math.round(hz)
        if (!Session.connected || indexOf(target) >= 0)
            return false        // già memorizzata: non se ne fa un doppione

        const next = entries.slice()
        next.push({
            frequency: target,
            label: labelFor(target, modeName),
            mode: mode,
            filterLowHz: filterLowHz,
            filterHighHz: filterHighHz,
        })
        entries = next
        persist()
        return true
    }

    /// Memorizza la sintonia corrente, senza modo: è ciò che faceva il
    /// pulsante del pannello, e continua a farlo.
    function storeCurrent() {
        if (!Session.connected)
            return false

        const hz = Math.round(Session.centerFrequency)
        if (indexOf(hz) >= 0)
            return false
        const next = entries.slice()
        next.push({ frequency: hz, label: labelFor(hz, "") })
        entries = next
        persist()
        return true
    }

    /// Richiama una memoria su un canale: frequenza, e il modo e il filtro se
    /// la voce li porta con sé.
    function recall(entry, row) {
        if (!entry || !goTo(entry.frequency))
            return false

        if (row >= 0 && entry.mode !== undefined) {
            Session.setChannelMode(row, entry.mode)
            if (entry.filterLowHz !== undefined && entry.filterHighHz !== undefined)
                Session.setChannelFilter(row, entry.filterLowHz, entry.filterHighHz)
        }
        return true
    }

    function remove(hz) {
        const target = Math.round(hz)
        entries = entries.filter(item => item.frequency !== target)
        persist()
    }

    function labelFor(hz, modeName) {
        const mhz = (hz / 1e6).toFixed(6)
        return modeName ? mhz + " " + modeName : mhz + " MHz"
    }

    /// Le etichette, per i menu che vogliono un elenco di stringhe.
    readonly property var labels: entries.map(item => item.label)

    function persist() {
        store.bandStackJson = JSON.stringify(bandStack)
        store.favoritesJson = JSON.stringify(entries)
    }

    Component.onCompleted: {
        try {
            const savedBands = JSON.parse(store.bandStackJson)
            if (savedBands && typeof savedBands === "object")
                bandStack = savedBands
        } catch (error) {
            bandStack = ({})
        }
        try {
            const saved = JSON.parse(store.favoritesJson)
            if (Array.isArray(saved))
                entries = saved
        } catch (error) {
            entries = []
        }
    }
}
