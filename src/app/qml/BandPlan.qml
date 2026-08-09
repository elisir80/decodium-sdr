// SPDX-License-Identifier: GPL-3.0-or-later
// Piano bande e interpretazione della frequenza digitata.
//
// Un singleton perché il piano bande è un fatto, non uno stato: le stesse
// tabelle servono al pannello di sintonia, alla validazione e — più avanti —
// alla colorazione dei segmenti sullo spettro.
pragma Singleton

import QtQuick

QtObject {
    id: root

    /// Bande amatoriali fino ai 6 metri. `start`/`end` delimitano la banda,
    /// `home` è la frequenza in cui si entra la prima volta: non il bordo
    /// inferiore, che di solito è vuoto, ma il tratto dove c'è traffico.
    readonly property var bands: [
        { name: "160m", start:  1810000, end:  2000000, home:  1840000 },
        { name: "80m",  start:  3500000, end:  3800000, home:  3620000 },
        { name: "60m",  start:  5351500, end:  5366500, home:  5357000 },
        { name: "40m",  start:  7000000, end:  7200000, home:  7100000 },
        { name: "30m",  start: 10100000, end: 10150000, home: 10136000 },
        { name: "20m",  start: 14000000, end: 14350000, home: 14100000 },
        { name: "17m",  start: 18068000, end: 18168000, home: 18100000 },
        { name: "15m",  start: 21000000, end: 21450000, home: 21150000 },
        { name: "12m",  start: 24890000, end: 24990000, home: 24940000 },
        { name: "10m",  start: 28000000, end: 29700000, home: 28400000 },
        { name: "6m",   start: 50000000, end: 52000000, home: 50150000 },
        { name: "FM 88–108", start: 88000000, end: 108000000, home: 100000000 },
    ]

    /// Emissioni orarie e stazioni di riferimento: comode per verificare che
    /// l'antenna e la catena di ricezione funzionino davvero.
    readonly property var references: [
        { name: "WWV 5",   frequency:  5000000 },
        { name: "WWV 10",  frequency: 10000000 },
        { name: "WWV 15",  frequency: 15000000 },
        { name: "FT8 40m", frequency:  7074000 },
        { name: "FT8 20m", frequency: 14074000 },
    ]

    /// Banda che contiene la frequenza, o null.
    function bandAt(hz) {
        for (let i = 0; i < bands.length; ++i) {
            if (hz >= bands[i].start && hz <= bands[i].end)
                return bands[i]
        }
        return null
    }

    /// Interpreta ciò che l'operatore digita, in hertz. Restituisce -1 se il
    /// testo non ha senso.
    ///
    /// Le convenzioni sono quelle in uso fra radioamatori, e sono ambigue
    /// solo in apparenza:
    ///
    ///   "14.225"      → 14,225 MHz   (punto singolo, campo MHz corto)
    ///   "14225"       → 14,225 MHz   (senza punto: sono kHz)
    ///   "14225.5"     → 14,2255 MHz  (cinque cifre prima del punto: kHz)
    ///   "7.100.000"   → 7,1 MHz      (formato del display, MHz.kHz.Hz)
    ///   "3620"        → 3,620 MHz
    ///
    /// La regola che le tiene insieme: fino a quattro cifre prima del punto si
    /// stanno scrivendo megahertz, da cinque in su chilohertz.
    function parseFrequency(text) {
        let clean = String(text).trim().replace(/\s/g, "").replace(/,/g, ".")
        if (clean.length === 0)
            return -1
        if (!/^[0-9.]+$/.test(clean))
            return -1

        const dots = (clean.match(/\./g) || []).length

        // Formato del display: MHz.kHz.Hz — i punti sono separatori.
        if (dots >= 2) {
            const digits = clean.replace(/\./g, "")
            const hz = Number(digits)
            return isFinite(hz) ? hz : -1
        }

        if (dots === 0) {
            // Nessun punto: sono chilohertz. È il modo in cui si scrive una
            // frequenza a voce ("tremilaseicentoventi").
            const khz = Number(clean)
            return isFinite(khz) ? Math.round(khz * 1000) : -1
        }

        const dot = clean.indexOf(".")
        const value = Number(clean)
        if (!isFinite(value))
            return -1

        // Fino a quattro cifre prima del punto: megahertz. Da cinque: kHz,
        // che conserva la scorciatoia storica "14225.0" = 14,225 MHz.
        return dot <= 4 ? Math.round(value * 1e6) : Math.round(value * 1000)
    }

    /// Formatta in MHz con i separatori del display.
    function formatFrequency(hz) {
        const mhz = Math.floor(hz / 1e6)
        const khz = Math.floor((hz % 1e6) / 1000)
        const rest = Math.round(hz % 1000)
        return mhz + "." + String(khz).padStart(3, "0") + "." + String(rest).padStart(3, "0")
    }
}
