// SPDX-License-Identifier: GPL-3.0-or-later
// L'ordine dei pannelli: si sposta, si ricorda, e sopravvive alle versioni.
//
// La parte che si rompe in silenzio è il ripristino: un ordine salvato ieri
// non contiene i pannelli aggiunti oggi, e la tentazione di ricostruire la
// colonna «come sta scritto» fa sparire i pannelli nuovi — che è il modo più
// efficace di far credere che una funzione non sia stata rilasciata.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "PanelOrder"
    width: 360
    height: 600
    visible: true
    when: windowShown

    Component {
        id: stripComponent
        ChannelStrip {}
    }

    function keysOf(strip) {
        const out = []
        for (let i = 0; i < strip.panels.count; ++i)
            out.push(strip.panels.get(i).key)
        return out
    }

    function test_factory_order_is_complete() {
        const strip = createTemporaryObject(stripComponent, testCase)
        verify(strip !== null, "colonna non istanziata")

        const keys = keysOf(strip)
        compare(keys.length, 7,
                "pannelli attesi: sintonia, tempo, catena, trasmissione, device, waterfall, canali")
        verify(keys.indexOf("sintonia") >= 0)
        verify(keys.indexOf("catena") >= 0)
        verify(keys.indexOf("trasmissione") >= 0)
        verify(keys.indexOf("tempo") >= 0)
        verify(keys.indexOf("device") >= 0)
        verify(keys.indexOf("waterfall") >= 0)
        verify(keys.indexOf("canali") >= 0)
    }

    function test_moving_a_panel_is_remembered() {
        const strip = createTemporaryObject(stripComponent, testCase)

        // Il waterfall in cima: è quello che si vuole sott'occhio mentre si
        // regola l'immagine dello spettro.
        const from = keysOf(strip).indexOf("waterfall")
        strip.panels.move(from, 0, 1)
        strip.storeOrder()

        compare(keysOf(strip)[0], "waterfall")
        compare(strip.savedOrder.split(",")[0], "waterfall",
                "l'ordine non è stato scritto come si vede")
    }

    function test_restoring_puts_known_panels_first() {
        const strip = createTemporaryObject(stripComponent, testCase)

        strip.savedOrder = "canali,waterfall"
        strip.restoreOrder()

        const keys = keysOf(strip)
        compare(keys[0], "canali")
        compare(keys[1], "waterfall")
        // E nessuno si perde per strada: i pannelli che l'ordine salvato non
        // nominava restano, in coda.
        compare(keys.length, 7, "un pannello è sparito nel ripristino")
        verify(keys.indexOf("sintonia") >= 2)
        verify(keys.indexOf("tempo") >= 2)
        verify(keys.indexOf("device") >= 2)
    }

    function test_restoring_ignores_panels_that_no_longer_exist() {
        const strip = createTemporaryObject(stripComponent, testCase)

        // Un ordine che nomina un pannello tolto da una versione successiva
        // non deve far saltare il ripristino di quelli che restano.
        strip.savedOrder = "fantasma,canali,altrofantasma,tempo"
        strip.restoreOrder()

        const keys = keysOf(strip)
        compare(keys.length, 7)
        compare(keys[0], "canali")
        compare(keys[1], "tempo")
    }

    function test_an_empty_order_leaves_the_factory_arrangement() {
        const strip = createTemporaryObject(stripComponent, testCase)
        const before = keysOf(strip)

        strip.savedOrder = ""
        strip.restoreOrder()

        compare(keysOf(strip), before, "un ordine vuoto ha rimescolato la colonna")
    }
}
