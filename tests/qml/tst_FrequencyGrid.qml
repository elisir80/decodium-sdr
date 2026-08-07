// SPDX-License-Identifier: GPL-3.0-or-later
// Regressione della griglia di frequenza.
//
// CONSTITUTION §8: ogni bug fix nasce con il test che lo riproduce. Qui il bug
// è quello che ha bloccato la UI alla prima connessione — un passo di griglia
// degenerato a zero rendeva `Infinity` il numero di tacche, e il Repeater
// istanziava delegate finché il thread della UI non smetteva di rispondere.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "FrequencyGrid"
    width: 800
    height: 400
    visible: true
    when: windowShown

    Component {
        id: gridComponent
        FrequencyGrid {}
    }

    function test_tickCount_stays_bounded_data() {
        return [
            { tag: "banda normale 192 kHz", span: 192000, w: 800 },
            { tag: "banda larga 10 MHz",    span: 10000000, w: 1200 },
            { tag: "banda strettissima",    span: 1, w: 800 },
            { tag: "larghezza zero",        span: 192000, w: 0 },
            { tag: "span zero",             span: 0, w: 800 },
            { tag: "span negativo",         span: -5000, w: 800 },
            { tag: "span non finito",       span: Infinity, w: 800 },
            { tag: "span NaN",              span: NaN, w: 800 },
            { tag: "larghezza NaN",         span: 192000, w: NaN },
        ]
    }

    function test_tickCount_stays_bounded(data) {
        const grid = createTemporaryObject(gridComponent, testCase,
                                           { spanHz: data.span, centerHz: 7100000, width: data.w, height: 300 })
        verify(grid !== null, "componente non istanziato")

        verify(isFinite(grid.stepHz), "passo non finito: " + grid.stepHz)
        verify(grid.stepHz > 0, "passo non positivo: " + grid.stepHz)

        verify(grid.tickCount >= 2, "troppe poche tacche: " + grid.tickCount)
        verify(grid.tickCount <= 64,
               "numero di tacche fuori controllo (" + grid.tickCount + "): "
               + "è il difetto che bloccava la UI")
    }

    function test_step_is_a_round_number() {
        const grid = createTemporaryObject(gridComponent, testCase,
                                           { spanHz: 192000, centerHz: 7100000, width: 800, height: 300 })
        // Il passo deve essere della forma 1/2/5 × 10ⁿ, altrimenti le
        // etichette mostrerebbero frequenze arbitrarie.
        const magnitude = Math.pow(10, Math.floor(Math.log(grid.stepHz) / Math.LN10))
        const mantissa = Math.round(grid.stepHz / magnitude)
        verify([1, 2, 5, 10].indexOf(mantissa) >= 0,
               "passo non tondo: " + grid.stepHz + " (mantissa " + mantissa + ")")
    }

    // Le tacche devono davvero comparire nella scena, con la loro etichetta:
    // una griglia che calcola i valori giusti ma non li disegna è inutile.
    function test_ticks_are_instantiated_with_labels() {
        const grid = createTemporaryObject(gridComponent, testCase,
                                           { spanHz: 192000, centerHz: 7100000, width: 800, height: 400 })
        wait(50)

        let visibleTicks = 0
        let labelsWithText = 0
        let sampleLabel = ""

        for (let i = 0; i < grid.children.length; ++i) {
            const tick = grid.children[i]
            if (tick.width !== 1)
                continue // non è un delegate della griglia
            if (tick.visible)
                ++visibleTicks

            for (let j = 0; j < tick.children.length; ++j) {
                const child = tick.children[j]
                if (child.text !== undefined && String(child.text).length > 0) {
                    ++labelsWithText
                    sampleLabel = String(child.text)
                    verify(child.width > 0, "etichetta di larghezza nulla: " + child.text)
                    // Devono restare nella fascia alta, dove sono sempre
                    // leggibili: più in basso finirebbero sotto il waterfall,
                    // la cui posizione è decisa dal rendering GPU.
                    verify(child.y >= 0 && child.y + child.height < grid.height * 0.25,
                           "etichetta fuori dalla fascia alta: y=" + child.y
                           + " su height=" + grid.height)
                }
            }
        }

        verify(visibleTicks >= 3, "tacche visibili insufficienti: " + visibleTicks)
        verify(labelsWithText >= 3,
               "etichette con testo insufficienti: " + labelsWithText
               + " (tacche visibili: " + visibleTicks + ")")
        verify(sampleLabel.indexOf("7.") === 0,
               "etichetta non plausibile per 7,1 MHz: '" + sampleLabel + "'")
    }

    function test_frequency_maps_to_position() {
        const grid = createTemporaryObject(gridComponent, testCase,
                                           { spanHz: 100000, centerHz: 7100000, width: 1000, height: 300 })
        // Il centro cade a metà, i bordi agli estremi.
        fuzzyCompare(grid.xForFrequency(7100000), 500, 0.5)
        fuzzyCompare(grid.xForFrequency(7050000), 0, 0.5)
        fuzzyCompare(grid.xForFrequency(7150000), 1000, 0.5)
    }

    function test_label_precision_follows_step() {
        const wide = createTemporaryObject(gridComponent, testCase,
                                           { spanHz: 10000000, centerHz: 14000000, width: 800, height: 300 })
        const narrow = createTemporaryObject(gridComponent, testCase,
                                             { spanHz: 4000, centerHz: 7100000, width: 800, height: 300 })
        // Su una banda stretta servono più decimali, altrimenti tutte le
        // etichette mostrerebbero lo stesso numero.
        verify(narrow.labelDecimals > wide.labelDecimals,
               "precisione delle etichette non adattata al passo")
    }
}
