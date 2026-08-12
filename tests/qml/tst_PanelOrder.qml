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

    /// I pannelli che la colonna deve offrire, in un posto solo: il numero
    /// stava scritto a mano in tre prove diverse, e aggiungerne uno le faceva
    /// fallire tutte e tre parlando di un «7» che non spiegava niente.
    readonly property var expectedKeys: [
        "sintonia", "strumento", "audio", "flusso", "tempo", "catena",
        "trasmissione", "device", "waterfall", "canali"
    ]

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
        compare(keys.length, testCase.expectedKeys.length,
                "pannelli attesi: " + testCase.expectedKeys.join(", "))
        for (const key of testCase.expectedKeys)
            verify(keys.indexOf(key) >= 0, "pannello mancante: " + key)
    }

    // ── Pannelli spenti ──────────────────────────────────────────────────
    //
    // Chiuso e spento sono due cose diverse: chiuso è ridotto a una riga di
    // titolo, spento è via. Uno spento non deve essere nemmeno creato — un
    // pannello invisibile ma vivo tiene i suoi binding, e lo studio audio
    // terrebbe un secondo rendering su GPU per qualcosa che nessuno guarda.
    function test_a_hidden_panel_is_not_built() {
        const strip = createTemporaryObject(stripComponent, testCase)

        verify(strip.slotActive("canali"), "i canali dovrebbero esserci sempre")
        strip.togglePanel("canali")
        verify(!strip.slotActive("canali"), "spento e ancora costruito")

        // Lo stesso gesto nei due versi: chi ne spegne uno per sbaglio lo
        // riaccende ripetendo quello che ha appena fatto.
        strip.togglePanel("canali")
        verify(strip.slotActive("canali"), "riacceso e ancora spento")
    }

    function test_hidden_panels_survive_a_restart() {
        const strip = createTemporaryObject(stripComponent, testCase)

        strip.togglePanel("tempo")
        strip.togglePanel("trasmissione")

        // È la stringa che finisce nelle preferenze: se cambia forma, chi
        // riapre il programma si ritrova pannelli spenti a caso.
        const keys = strip.hiddenPanels.split(",")
        compare(keys.length, 2)
        verify(keys.indexOf("tempo") >= 0)
        verify(keys.indexOf("trasmissione") >= 0)
    }

    // Spegnere non riordina: sono due gesti diversi, e uno che facesse anche
    // l'altro rimescolerebbe la colonna ogni volta che si nasconde qualcosa.
    function test_hiding_does_not_reorder() {
        const strip = createTemporaryObject(stripComponent, testCase)

        const before = keysOf(strip)
        strip.togglePanel("catena")
        compare(keysOf(strip).join(","), before.join(","))
    }

    // Ogni pannello ha la sua icona, e ogni icona il suo pannello: una fila di
    // icone che non copre tutta la colonna lascia pannelli che non si possono
    // più riaccendere.
    function test_every_panel_has_an_icon() {
        const strip = createTemporaryObject(stripComponent, testCase)

        compare(strip.panelInfo.length, testCase.expectedKeys.length)
        for (const key of testCase.expectedKeys) {
            let found = false
            for (const entry of strip.panelInfo) {
                if (entry.key !== key)
                    continue
                found = true
                verify(entry.glyph.length > 0, "icona vuota per " + key)
                verify(entry.label.length > 0, "etichetta vuota per " + key)
            }
            verify(found, "nessuna icona per il pannello " + key)
        }
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
        compare(keys.length, testCase.expectedKeys.length,
                "un pannello è sparito nel ripristino")
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
        compare(keys.length, testCase.expectedKeys.length)
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
