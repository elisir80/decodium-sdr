// SPDX-License-Identifier: GPL-3.0-or-later
// Regressione dell'asse dei tempi del waterfall.
//
// Terzo componente che ricava un numero di tacche da una divisione fatta in
// JavaScript, e terzo presidio contro lo stesso difetto: un passo degenerato a
// zero rende `Infinity` il model del Repeater, e la UI si ferma senza dire
// niente. Qui il rischio è più concreto che altrove, perché la durata della
// storia è una *misura* — all'avvio vale zero, e mentre il ritmo si assesta
// può passare per valori qualsiasi.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "TimeScale"
    width: 400
    height: 600
    visible: true
    when: windowShown

    Component {
        id: scaleComponent
        TimeScale {}
    }

    function test_tickCount_stays_bounded_data() {
        return [
            { tag: "storia tipica",     seconds: 10,        h: 600 },
            { tag: "storia lunga",      seconds: 600,       h: 600 },
            { tag: "storia brevissima", seconds: 0.4,       h: 600 },
            { tag: "non misurata",      seconds: 0,         h: 600 },
            { tag: "negativa",          seconds: -5,        h: 600 },
            { tag: "non finita",        seconds: Infinity,  h: 600 },
            { tag: "NaN",               seconds: NaN,       h: 600 },
            { tag: "altezza zero",      seconds: 10,        h: 0 },
            { tag: "altezza NaN",       seconds: 10,        h: NaN },
        ]
    }

    function test_tickCount_stays_bounded(data) {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            historySeconds: data.seconds, width: 300, height: data.h
        })
        verify(scale !== null, "componente non istanziato")

        verify(isFinite(scale.stepSeconds), "passo non finito: " + scale.stepSeconds)
        verify(scale.stepSeconds > 0, "passo non positivo: " + scale.stepSeconds)

        verify(scale.tickCount >= 0, "numero di tacche negativo: " + scale.tickCount)
        verify(scale.tickCount <= 24,
               "numero di tacche fuori controllo (" + scale.tickCount + "): "
               + "è il difetto che bloccava la UI")
    }

    // Senza misura non si disegna: meglio nessun asse di un asse inventato.
    function test_nothing_is_drawn_before_the_measurement() {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            historySeconds: 0, width: 300, height: 600
        })
        compare(scale.ready, false)
        compare(scale.tickCount, 0)
    }

    function test_seconds_map_to_position() {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            historySeconds: 10, width: 300, height: 600, spectrumRatio: 0.5
        })
        // Lo spettro prende metà dei 600 punti: il waterfall parte da 300 e
        // ne occupa 300. L'istante più recente è il suo bordo alto.
        fuzzyCompare(scale.yForSeconds(0), 300, 0.5)
        fuzzyCompare(scale.yForSeconds(10), 600, 0.5)
        fuzzyCompare(scale.yForSeconds(5), 450, 0.5)
    }

    function test_step_is_a_readable_number() {
        const scale = createTemporaryObject(scaleComponent, testCase, {
            historySeconds: 12, width: 300, height: 600
        })
        // I passi ammessi sono quelli che si leggono su un orologio, non un
        // valore qualsiasi uscito da una divisione.
        const allowed = [1, 2, 5, 10, 15, 30, 60, 120, 300]
        verify(allowed.indexOf(scale.stepSeconds) >= 0,
               "passo non leggibile: " + scale.stepSeconds)
    }
}
