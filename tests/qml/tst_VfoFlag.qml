// SPDX-License-Identifier: GPL-3.0-or-later
// Regressione del trascinamento della bandierina VFO.
//
// CONSTITUTION §8: il test nasce col difetto che riproduce. Qui il difetto è
// quello che ha portato RX 1 a 12.076.200 Hz con il centro a 14.100.000 — una
// frequenza che non era né quella vecchia né quella nuova.
//
// La bandierina si ferma al bordo quando il canale esce dalla banda
// campionata: da lì in poi non è più sopra il suo canale, e il gesto che la
// trascina calcolava lo spostamento come se lo fosse. Il canale avanzava di
// uno schermo per trascinata senza mai raggiungere il puntatore, e dopo
// qualche tentativo si fermava su un numero arbitrario.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "VfoFlag"
    width: 1000
    height: 400
    visible: true
    when: windowShown

    // Banda campionata da 1,536 MS/s centrata sui venti metri: gli stessi
    // numeri con cui il difetto è stato visto.
    readonly property real spanHz: 1536000
    readonly property real startHz: 14100000 - spanHz / 2

    function freqAt(x) { return startHz + (x / testCase.width) * spanHz }
    function xForFreq(hz) { return (hz - startHz) / spanHz * testCase.width }

    Component {
        id: flagComponent

        Item {
            id: holder

            property alias flag: vfo
            property real requestedHz: -1
            property int tuneCount: 0

            width: 1000
            height: 400

            VfoFlag {
                id: vfo
                vfoRow: 0
                vfoColor: "#4fc3f7"
                vfoLabel: "RX 1"
                vfoFrequency: 14100000
                bandLowHz: 300
                bandHighHz: 2700
                levelDb: -80
                vfoSelected: true

                xForFrequency: testCase.xForFreq
                frequencyAt: testCase.freqAt

                onTuneRequested: (hz) => {
                    holder.requestedHz = hz
                    ++holder.tuneCount
                }
            }
        }
    }

    function drag(holder, toX) {
        const flag = holder.flag.flagItem
        const fromX = flag.x + flag.width / 2
        const y = flag.y + flag.height / 2

        mousePress(holder, fromX, y)
        // Un passo intermedio: il trascinamento reale non teletrasporta il
        // puntatore, e un difetto che si accumula lungo il percorso non si
        // vedrebbe con un solo salto.
        mouseMove(holder, (fromX + toX) / 2, y)
        mouseMove(holder, toX, y)
        mouseRelease(holder, toX, y)
    }

    // Caso sano: il canale è sotto la bandierina, e trascinarla lo porta dove
    // si è lasciato il puntatore.
    function test_drag_follows_the_pointer() {
        const holder = createTemporaryObject(flagComponent, testCase)
        verify(holder !== null, "componente non istanziato")
        wait(50)

        drag(holder, 700)

        verify(holder.tuneCount > 0, "il trascinamento non ha chiesto nulla")
        fuzzyCompare(holder.requestedHz, testCase.freqAt(700), 5000)
    }

    // Il difetto: canale fuori dalla banda campionata, bandierina schiacciata
    // al bordo. Trascinarla deve riportare il canale dove punta il mouse — è
    // l'unico modo che ha l'operatore di recuperarlo.
    function test_drag_recovers_a_channel_left_outside() {
        const holder = createTemporaryObject(flagComponent, testCase)
        verify(holder !== null, "componente non istanziato")
        holder.flag.vfoFrequency = 7100000       // rimasto sui quaranta metri
        wait(50)

        const flag = holder.flag.flagItem
        fuzzyCompare(flag.x, 0, 0.5,
                     "la bandierina dovrebbe fermarsi al bordo sinistro")

        drag(holder, 700)

        verify(holder.tuneCount > 0, "il trascinamento non ha chiesto nulla")
        fuzzyCompare(holder.requestedHz, testCase.freqAt(700), 5000,
                     "il canale non è arrivato sotto il puntatore: è il difetto "
                     + "che ha portato RX 1 a 12.076.200 Hz")
    }

    // Una bandierina ferma al bordo non deve sembrare un canale in ascolto lì:
    // è come il difetto è passato inosservato tanto a lungo.
    function test_a_channel_outside_the_band_says_so() {
        const holder = createTemporaryObject(flagComponent, testCase)
        verify(!holder.flag.adrift, "canale dentro la banda dato per disperso")

        holder.flag.vfoFrequency = 7100000
        wait(50)
        verify(holder.flag.adrift,
               "canale a 7,1 MHz con il centro a 14,1 non è dato per fuori banda")

        holder.flag.vfoFrequency = 14100000
        wait(50)
        verify(!holder.flag.adrift, "canale rientrato ancora dato per disperso")
    }

    // E il gesto non deve restituire frequenze fuori da ciò che si sta
    // guardando: una richiesta che cade oltre i bordi dello schermo è già la
    // prova che il conto è sbagliato.
    function test_drag_never_leaves_the_visible_band() {
        const holder = createTemporaryObject(flagComponent, testCase)
        holder.flag.vfoFrequency = 7100000
        wait(50)

        drag(holder, 700)

        verify(holder.requestedHz >= testCase.freqAt(0)
               && holder.requestedHz <= testCase.freqAt(testCase.width),
               "frequenza richiesta fuori dalla banda visibile: "
               + holder.requestedHz)
    }
}
