// SPDX-License-Identifier: GPL-3.0-or-later
// La scala S e il quadrante che la disegna.
//
// Uno strumento sbagliato non fallisce: mostra un numero plausibile. Un S7 che
// è un S4 non ha alcun sintomo — si scopre solo passando un rapporto a
// qualcuno che ne passa uno diverso indietro. Per questo qui si verificano i
// numeri, non il fatto che il componente si istanzi.
//
// La geometria si presidia per la stessa ragione di tst_LevelScale: la
// posizione sull'arco nasce da divisioni fatte in JavaScript, e con il
// pannello chiuso — larghezza e altezza a zero — un raggio negativo non
// disegna niente e non dà alcun errore.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "SMeter"
    width: 400
    height: 400
    visible: true
    when: windowShown

    Component {
        id: meterComponent
        DecoMeterS { levelDb: -140 }
    }

    // ── La scala ─────────────────────────────────────────────────────────

    // Sei decibel per punto S: è la convenzione IARU, ed è ciò che rende
    // confrontabile un rapporto passato da due stazioni diverse.
    function test_one_s_unit_is_six_decibels() {
        const s9 = -80
        fuzzyCompare(SMeterScale.units(s9, s9), 9, 0.001)
        fuzzyCompare(SMeterScale.units(s9 - 6, s9), 8, 0.001)
        fuzzyCompare(SMeterScale.units(s9 - 48, s9), 1, 0.001)
    }

    // La taratura porta il rumore a S1: è la lettura giusta per un canale
    // vuoto, e il punto da cui si contano i sei decibel per gradino.
    function test_calibration_puts_the_noise_at_s1_data() {
        return [
            { tag: "ricevitore quieto", floor: -120 },
            { tag: "guadagno alto",     floor: -80 },
            { tag: "banda rumorosa",    floor: -60 },
        ]
    }

    function test_calibration_puts_the_noise_at_s1(data) {
        const s9 = SMeterScale.s9From(data.floor)
        fuzzyCompare(SMeterScale.units(data.floor, s9), 1, 0.6)

        // E un segnale quaranta decibel sopra il rumore vale lo stesso
        // rapporto su ogni ricevitore tarato allo stesso modo.
        const strong = SMeterScale.units(data.floor + 40, s9)
        fuzzyCompare(strong, 1 + 40 / SMeterScale.dbPerUnit, 0.6)
    }

    // E poi sta ferma. Questo è il difetto che la 1.1.6 aveva al posto del
    // precedente: la scala inseguiva il fondo di rumore, che è un inseguitore
    // di minimo dentro la banda del canale e si muove con la banda, con il
    // filtro e con quanto è occupato il canale. Lo stesso segnale dava due
    // rapporti diversi in due momenti diversi, ed è la sola cosa che un
    // S-meter non può fare.
    function test_the_scale_does_not_move_with_the_noise() {
        const meter = createTemporaryObject(meterComponent, testCase, {
            width: 320, height: 220, noiseFloorDb: -110, levelDb: -70
        })
        meter.calibrateFromFloor()
        wait(50)

        const reference = meter.s9ReferenceDb
        const reading = meter.units

        // Il rumore sale di venti decibel — succede cambiando banda, o
        // togliendo l'attenuatore — e il segnale resta dov'è.
        meter.noiseFloorDb = -90
        wait(120)

        compare(meter.s9ReferenceDb, reference,
                "la taratura si è spostata da sola")
        fuzzyCompare(meter.units, reading, 0.001,
                     "lo stesso segnale legge un rapporto diverso")

        // Solo chi tara la sposta.
        meter.calibrateFromFloor()
        verify(meter.s9ReferenceDb !== reference,
               "la taratura a comando non ha fatto niente")
    }

    // Il fondo scala del quadrante è S9+60: sessanta decibel oltre S9, non uno
    // di più — la lancetta si ferma lì come contro un fermo.
    function test_full_scale_is_s9_plus_sixty() {
        const s9 = -80
        fuzzyCompare(SMeterScale.plusDb(s9 + 60, s9), 60, 0.001)
        fuzzyCompare(SMeterScale.plusDb(s9 + 200, s9), 60, 0.001)
        compare(SMeterScale.readout(s9 + 60, s9), "S9+60")
    }

    function test_readout_data() {
        return [
            { tag: "fondo",      level: -140, expected: "S0" },
            // Sei decibel sotto S1 non è «meno di S1»: è S0. A metà gradino
            // l'arrotondamento risale, ed è giusto così — uno strumento con la
            // lancetta fra due tacche si legge sulla più vicina.
            { tag: "sotto S1",   level: -134, expected: "S0" },
            { tag: "quasi S1",   level: -130, expected: "S1" },
            { tag: "S1",         level: -128, expected: "S1" },
            { tag: "S5",         level: -104, expected: "S5" },
            { tag: "S9",         level: -80,  expected: "S9" },
            { tag: "S9+20",      level: -60,  expected: "S9+20" },
            { tag: "S9+60",      level: -20,  expected: "S9+60" },
            // Oltre il tetto non si va: la lancetta si ferma a fondo scala,
            // come quella di uno strumento vero contro il suo fermo.
            { tag: "oltre",      level: 0,    expected: "S9+60" },
        ]
    }

    function test_readout(data) {
        // Riferimento a −80 dBFS: gli stessi numeri di prima, ma dichiarati
        // invece che ricavati da un tetto che non significava niente.
        compare(SMeterScale.readout(data.level, -80), data.expected)
    }

    // Un livello non finito arriva da un canale che non ha ancora misurato
    // niente. Deve dare zero, non NaN: un NaN si propaga nell'angolo della
    // lancetta, e una rotazione NaN fa sparire l'oggetto senza un errore.
    function test_a_level_that_is_not_a_number_reads_zero() {
        compare(SMeterScale.units(NaN, -80), 0)
        compare(SMeterScale.units(-Infinity, -80), 0)
        compare(SMeterScale.units(-90, NaN), 0)
        compare(SMeterScale.s9From(NaN), -80, "un fondo non misurato deve dare un riferimento d'uso")
    }

    // ── Il quadrante ─────────────────────────────────────────────────────

    function test_geometry_stays_valid_data() {
        return [
            { tag: "colonna stretta", w: 300, h: 200 },
            { tag: "colonna larga",   w: 520, h: 340 },
            { tag: "pannello chiuso", w: 0,   h: 0 },
            { tag: "altezza zero",    w: 320, h: 0 },
            { tag: "larghezza zero",  w: 0,   h: 220 },
        ]
    }

    function test_geometry_stays_valid(data) {
        const meter = createTemporaryObject(meterComponent, testCase, {
            width: data.w, height: data.h, levelDb: -95
        })
        verify(meter !== null, "quadrante non istanziato")

        verify(isFinite(meter.targetFraction), "posizione non finita")
        verify(meter.targetFraction >= 0 && meter.targetFraction <= 1,
               "posizione fuori dall'arco: " + meter.targetFraction)
        verify(isFinite(meter.floorFraction), "fondo di rumore non finito")
    }

    // La lancetta deve stare dentro l'apertura del quadrante a ogni livello,
    // compresi quelli fuori scala: fuori da lì punterebbe verso il nulla.
    function test_needle_stays_inside_the_dial_data() {
        return [
            { tag: "sotto il fondo", level: -200 },
            { tag: "fondo",          level: -140 },
            { tag: "S9",             level: -80 },
            { tag: "fondo scala",    level: -20 },
            { tag: "oltre",          level: 20 },
            { tag: "non numerico",   level: NaN },
        ]
    }

    function test_needle_stays_inside_the_dial(data) {
        const meter = createTemporaryObject(meterComponent, testCase, {
            width: 320, height: 220, levelDb: data.level
        })

        const f = meter.arcFraction(meter.units)
        verify(isFinite(f), "posizione non finita: " + f)
        verify(f >= -0.001 && f <= 1.001, "lancetta fuori dal quadrante: " + f)
    }

    // L'arco cresce da sinistra a destra. Sembra ovvio, e non lo è: la parte
    // sotto S9 e quella sopra hanno due scale diverse, e basta invertire un
    // segno nel raccordo perché la lancetta torni indietro proprio quando il
    // segnale si fa forte.
    function test_the_needle_only_moves_forward() {
        const meter = createTemporaryObject(meterComponent, testCase, {
            width: 320, height: 220, levelDb: -140
        })

        let previous = -Infinity
        for (let level = -140; level <= -20; level += 2) {
            meter.levelDb = level
            const f = meter.arcFraction(meter.units)
            verify(f >= previous - 0.001,
                   "la lancetta torna indietro a " + level + " dBFS: "
                   + f + " dopo " + previous)
            previous = f
        }
    }

    // In trasmissione lo strumento non misura il segnale ricevuto. Deve
    // andare a riposo invece di restare sull'ultimo valore letto, che
    // resterebbe lì plausibile e falso per tutta la chiamata.
    function test_transmitting_parks_the_needle() {
        // La taratura si dichiara: da quando è una misura presa una volta e
        // non un inseguimento, quanto valga un livello dipende da lei.
        const meter = createTemporaryObject(meterComponent, testCase, {
            width: 320, height: 220, levelDb: -60, s9ReferenceDb: -80
        })
        verify(meter.units > 9, "il segnale di prova non è oltre S9")

        meter.transmitting = true
        compare(meter.units, 0, "la lancetta è rimasta sul segnale ricevuto")
    }
}
