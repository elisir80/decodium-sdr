// SPDX-License-Identifier: GPL-3.0-or-later
// Regressione della scala di ampiezza.
//
// Stesso presidio di tst_FrequencyGrid, e per lo stesso motivo: qui il numero
// di tacche nasce da una divisione fatta in JavaScript, e un passo degenerato
// a zero renderebbe `Infinity` il model del Repeater. La UI non darebbe alcun
// errore — si fermerebbe e basta. È già successo una volta.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "LevelScale"
    width: 800
    height: 400
    visible: true
    when: windowShown

    Component {
        id: scaleComponent
        LevelScale {}
    }

    function test_tickCount_stays_bounded_data() {
        return [
            { tag: "scala normale",        floor: -125, ceiling: -25,  h: 400 },
            { tag: "scala minima",         floor: -100, ceiling: -75,  h: 400 },
            { tag: "scala larghissima",    floor: -180, ceiling: 0,    h: 400 },
            { tag: "altezza zero",         floor: -125, ceiling: -25,  h: 0 },
            { tag: "estremi invertiti",    floor: -25,  ceiling: -125, h: 400 },
            { tag: "estremi coincidenti",  floor: -90,  ceiling: -90,  h: 400 },
            { tag: "fondo non finito",     floor: -Infinity, ceiling: -25, h: 400 },
            { tag: "vetta NaN",            floor: -125, ceiling: NaN,  h: 400 },
            { tag: "altezza NaN",          floor: -125, ceiling: -25,  h: NaN },
        ]
    }

    function test_tickCount_stays_bounded(data) {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            floorDb: data.floor, ceilingDb: data.ceiling, width: 200, height: data.h
        })
        verify(scale !== null, "componente non istanziato")

        verify(isFinite(scale.stepDb), "passo non finito: " + scale.stepDb)
        verify(scale.stepDb > 0, "passo non positivo: " + scale.stepDb)

        verify(scale.tickCount >= 2, "troppe poche tacche: " + scale.tickCount)
        verify(scale.tickCount <= 32,
               "numero di tacche fuori controllo (" + scale.tickCount + "): "
               + "è il difetto che bloccava la UI")
    }

    function test_step_is_a_round_number() {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            floorDb: -125, ceilingDb: -25, width: 200, height: 400
        })
        // Un operatore legge 10 o 20 dB, non 17: un passo arbitrario rende la
        // scala inutile proprio quando la si usa per stimare un livello.
        const magnitude = Math.pow(10, Math.floor(Math.log(scale.stepDb) / Math.LN10))
        const mantissa = Math.round(scale.stepDb / magnitude)
        verify([1, 2, 5, 10].indexOf(mantissa) >= 0,
               "passo non tondo: " + scale.stepDb + " (mantissa " + mantissa + ")")
    }

    // Su una scala compressa il passo scende sotto il decibel: arrotondando
    // all'intero comparivano tacche diverse con la stessa etichetta — −81,
    // −81, −82, −82 — e una scala che ripete i numeri sembra funzionare
    // mentre non dice niente.
    function test_labels_do_not_repeat_on_a_narrow_scale() {
        // L'altezza conta: su un pannello alto la spaziatura desiderata
        // concede molte divisioni, il passo scende a due decimi di decibel ed
        // è lì che gli interi cominciano a ripetersi. Sono i numeri veri di
        // una finestra a schermo intero su un monitor da 1370 punti.
        const scale = createTemporaryObject(scaleComponent, testCase, {
            floorDb: -83, ceilingDb: -80, width: 200, height: 1370
        })
        wait(50)

        const seen = {}
        let labels = 0
        for (let i = 0; i < scale.children.length; ++i) {
            const tick = scale.children[i]
            if (tick.height !== 1 || !tick.visible)
                continue
            for (let j = 0; j < tick.children.length; ++j) {
                const child = tick.children[j]
                if (child.text === undefined || String(child.text).length === 0)
                    continue
                const text = String(child.text)
                verify(seen[text] === undefined,
                       "due tacche con la stessa etichetta: '" + text + "'")
                seen[text] = true
                ++labels
            }
        }
        verify(labels >= 2, "etichette insufficienti per il controllo: " + labels)
    }

    function test_level_maps_to_position() {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            floorDb: -100, ceilingDb: -50, width: 200, height: 400, spectrumRatio: 0.5
        })
        // Lo spettro occupa metà dei 400 punti: 200. La vetta in cima, il
        // fondo sulla linea che lo separa dal waterfall.
        fuzzyCompare(scale.yForLevel(-50), 0, 0.5)
        fuzzyCompare(scale.yForLevel(-100), 200, 0.5)
        fuzzyCompare(scale.yForLevel(-75), 100, 0.5)
    }

    // Le tacche devono comparire davvero, con la loro etichetta, e restare
    // dentro la fascia dello spettro: sotto c'è il waterfall, che non ha un
    // asse delle ampiezze.
    function test_ticks_stay_within_the_spectrum_band() {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            floorDb: -125, ceilingDb: -25, width: 200, height: 400, spectrumRatio: 0.45
        })
        wait(50)

        let visibleTicks = 0
        for (let i = 0; i < scale.children.length; ++i) {
            const tick = scale.children[i]
            if (tick.height !== 1)
                continue // non è un delegate della scala
            if (!tick.visible)
                continue

            ++visibleTicks
            verify(tick.y >= 0 && tick.y <= scale.spectrumHeight + 0.5,
                   "tacca fuori dalla fascia dello spettro: y=" + tick.y
                   + " su spectrumHeight=" + scale.spectrumHeight)
        }

        verify(visibleTicks >= 3, "tacche visibili insufficienti: " + visibleTicks)
    }
}
