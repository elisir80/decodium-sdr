// SPDX-License-Identifier: GPL-3.0-or-later
// La scala S e il quadrante che la disegna.
//
// Uno strumento sbagliato non fallisce: mostra un numero plausibile. Un S7 che
// è un S4 non ha alcun sintomo — si scopre solo passando un rapporto a
// qualcuno che ne passa uno diverso indietro. Per questo qui si verificano i
// numeri, non il fatto che il componente si istanzi.
//
// La geometria si presidia per la stessa ragione di tst_LevelScale: il raggio
// nasce da una divisione fatta in JavaScript, e in un pannello chiuso —
// larghezza e altezza a zero — diventerebbe negativo. Un arco di raggio
// negativo non disegna niente e non dà alcun errore.
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
        LcdSMeter { levelDb: -140 }
    }

    // ── La scala ─────────────────────────────────────────────────────────

    // Sei decibel per punto S: è la convenzione IARU, ed è ciò che rende
    // confrontabile un rapporto passato da due stazioni diverse.
    function test_one_s_unit_is_six_decibels() {
        const ceiling = -20
        const s9 = SMeterScale.s9Level(ceiling)
        compare(s9, -80, "S9 non cade dove dovrebbe")

        fuzzyCompare(SMeterScale.units(s9, ceiling), 9, 0.001)
        fuzzyCompare(SMeterScale.units(s9 - 6, ceiling), 8, 0.001)
        fuzzyCompare(SMeterScale.units(s9 - 48, ceiling), 1, 0.001)
    }

    // Il fondo scala del quadrante è S9+60, e cade sul tetto della dinamica:
    // se non ci cadesse, l'ultimo tratto dell'arco sarebbe irraggiungibile.
    function test_full_scale_is_s9_plus_sixty() {
        const ceiling = -20
        fuzzyCompare(SMeterScale.plusDb(ceiling, ceiling), 60, 0.001)
        compare(SMeterScale.readout(ceiling, ceiling), "S9+60")
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
        compare(SMeterScale.readout(data.level, -20), data.expected)
    }

    // Un livello non finito arriva da un canale che non ha ancora misurato
    // niente. Deve dare zero, non NaN: un NaN si propaga nell'angolo della
    // lancetta, e una rotazione NaN fa sparire l'oggetto senza un errore.
    function test_a_level_that_is_not_a_number_reads_zero() {
        compare(SMeterScale.units(NaN, -20), 0)
        compare(SMeterScale.units(-Infinity, -20), 0)
        compare(SMeterScale.units(-90, NaN), 0)
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

        verify(isFinite(meter.radius), "raggio non finito: " + meter.radius)
        verify(meter.radius > 0, "raggio non positivo: " + meter.radius)
        verify(isFinite(meter.pivotY), "perno non finito")
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

        const angle = meter.angleFor(meter.units)
        verify(isFinite(angle), "angolo non finito: " + angle)
        verify(angle >= -meter.sweep - 0.001 && angle <= meter.sweep + 0.001,
               "lancetta fuori dal quadrante: " + angle)
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
            const angle = meter.angleFor(meter.units)
            verify(angle >= previous - 0.001,
                   "la lancetta torna indietro a " + level + " dBFS: "
                   + angle + " dopo " + previous)
            previous = angle
        }
    }

    // In trasmissione lo strumento non misura il segnale ricevuto. Deve
    // andare a riposo invece di restare sull'ultimo valore letto, che
    // resterebbe lì plausibile e falso per tutta la chiamata.
    function test_transmitting_parks_the_needle() {
        const meter = createTemporaryObject(meterComponent, testCase, {
            width: 320, height: 220, levelDb: -60
        })
        verify(meter.units > 9, "il segnale di prova non è oltre S9")

        meter.transmitting = true
        compare(meter.units, 0, "la lancetta è rimasta sul segnale ricevuto")
    }
}
