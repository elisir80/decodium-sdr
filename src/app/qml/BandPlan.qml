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
    ]

    /// Segmenti d'uso dentro le bande, secondo il piano IARU Regione 1.
    ///
    /// Serve a sapere dove si sta: su una banda che non si frequenta, o su un
    /// segmento regolato diversamente da come lo si ricorda, è la differenza
    /// fra chiamare dove si può e dove non si dovrebbe.
    ///
    /// Attenzione a cosa è e cosa non è: è il piano *volontario* della
    /// Regione 1, non la licenza. I limiti che contano davvero sono quelli
    /// della propria amministrazione e della propria patente, e in Regione 2
    /// e 3 i confini sono altri. Qui è una guida alla lettura dello spettro.
    ///
    /// `kind` vale "cw", "digi", "beacon" o "phone".
    readonly property var segments: [
        { band: "160m", start:  1810000, end:  1838000, kind: "cw" },
        { band: "160m", start:  1838000, end:  1843000, kind: "digi" },
        { band: "160m", start:  1843000, end:  2000000, kind: "phone" },

        { band: "80m",  start:  3500000, end:  3570000, kind: "cw" },
        { band: "80m",  start:  3570000, end:  3600000, kind: "digi" },
        { band: "80m",  start:  3600000, end:  3800000, kind: "phone" },

        { band: "60m",  start:  5351500, end:  5354000, kind: "cw" },
        { band: "60m",  start:  5354000, end:  5366000, kind: "phone" },

        { band: "40m",  start:  7000000, end:  7040000, kind: "cw" },
        { band: "40m",  start:  7040000, end:  7050000, kind: "digi" },
        { band: "40m",  start:  7050000, end:  7200000, kind: "phone" },

        { band: "30m",  start: 10100000, end: 10130000, kind: "cw" },
        { band: "30m",  start: 10130000, end: 10150000, kind: "digi" },

        { band: "20m",  start: 14000000, end: 14070000, kind: "cw" },
        { band: "20m",  start: 14070000, end: 14099000, kind: "digi" },
        { band: "20m",  start: 14099000, end: 14101000, kind: "beacon" },
        { band: "20m",  start: 14101000, end: 14350000, kind: "phone" },

        { band: "17m",  start: 18068000, end: 18095000, kind: "cw" },
        { band: "17m",  start: 18095000, end: 18109000, kind: "digi" },
        { band: "17m",  start: 18109000, end: 18111000, kind: "beacon" },
        { band: "17m",  start: 18111000, end: 18168000, kind: "phone" },

        { band: "15m",  start: 21000000, end: 21070000, kind: "cw" },
        { band: "15m",  start: 21070000, end: 21149000, kind: "digi" },
        { band: "15m",  start: 21149000, end: 21151000, kind: "beacon" },
        { band: "15m",  start: 21151000, end: 21450000, kind: "phone" },

        { band: "12m",  start: 24890000, end: 24915000, kind: "cw" },
        { band: "12m",  start: 24915000, end: 24929000, kind: "digi" },
        { band: "12m",  start: 24929000, end: 24931000, kind: "beacon" },
        { band: "12m",  start: 24931000, end: 24990000, kind: "phone" },

        { band: "10m",  start: 28000000, end: 28070000, kind: "cw" },
        { band: "10m",  start: 28070000, end: 28190000, kind: "digi" },
        { band: "10m",  start: 28190000, end: 28225000, kind: "beacon" },
        { band: "10m",  start: 28225000, end: 29700000, kind: "phone" },

        { band: "6m",   start: 50000000, end: 50100000, kind: "cw" },
        { band: "6m",   start: 50100000, end: 50500000, kind: "phone" },
        { band: "6m",   start: 50500000, end: 52000000, kind: "digi" },
    ]

    /// Nome leggibile di un tipo di segmento.
    function segmentLabel(kind) {
        switch (kind) {
        case "cw":     return qsTr("CW")
        case "digi":   return qsTr("DATI")
        case "beacon": return qsTr("FARI")
        case "phone":  return qsTr("FONIA")
        }
        return ""
    }

    /// Segmenti che toccano l'intervallo richiesto, in hertz.
    ///
    /// Chi disegna la striscia riceve solo quello che gli serve: filtrare a
    /// monte evita di istanziare decine di rettangoli invisibili a ogni
    /// cambio di sintonia.
    function segmentsIn(startHz, endHz) {
        const result = []
        if (!(endHz > startHz))
            return result
        for (let i = 0; i < segments.length; ++i) {
            const s = segments[i]
            if (s.end > startHz && s.start < endHz)
                result.push(s)
        }
        return result
    }

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
