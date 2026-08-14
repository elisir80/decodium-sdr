// SPDX-License-Identifier: GPL-3.0-or-later
// La finestra visibile dello spettro: quando si sposta, e quando no.
//
// «Porta in vista» è un comando che si nota solo quando sbaglia, e sbaglia in
// due modi opposti. Se non si sposta abbastanza, si sceglie un ricevitore
// dalla colonna e il waterfall non cambia: la scheda si illumina e sembra che
// il clic non abbia fatto niente. Se si sposta troppo — cioè anche quando il
// canale è già lì — la vista salta mentre si sta guardando un segnale, e si
// perde il segno proprio nel momento in cui serve tenerlo.
//
// Si verifica il conto e non il pannello: banda campionata e centro vengono
// dalla sessione e sono in sola lettura, quindi una prova che volesse metterli
// non potrebbe. I numeri sono quelli di una connessione vera — un megahertz
// attorno ai sette — perché con numeri tondi un errore di segno passerebbe
// inosservato.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "PanadapterView"
    width: 600
    height: 400
    visible: true
    when: windowShown

    readonly property real bandStart: 7000000
    readonly property real bandSpan: 1000000

    Component {
        id: paneComponent
        PanadapterPane { width: 600; height: 400 }
    }

    /// Il pannello si crea dentro la prova che lo usa: `createTemporaryObject`
    /// lo distrugge alla fine di ciascuna, e uno creato in `initTestCase`
    /// sarebbe già sparito alla prima chiamata.
    function shownStart(hz, currentStart, currentSpan) {
        const pane = createTemporaryObject(paneComponent, testCase)
        verify(pane !== null, "panadattatore non istanziato")
        return pane.viewStartToShow(hz, testCase.bandStart, testCase.bandSpan,
                                    currentStart, currentSpan)
    }

    function deliveredView(bandSpanHz, clientDemod) {
        const pane = createTemporaryObject(paneComponent, testCase)
        verify(pane !== null, "panadattatore non istanziato")
        return pane.deliveredBandView(bandSpanHz, clientDemod)
    }

    // Audio CAT: 48 kHz arrivano dal codec, ma la vista operativa è larga
    // 7 kHz e deve avere il VFO esattamente al centro dopo ogni aggiornamento
    // dalla radio. Un viewStart conservato dal VFO precedente sposta invece
    // cursore, griglia e waterfall assieme verso un bordo.
    function test_audio_radio_view_is_centred_on_the_cat_vfo() {
        const view = deliveredView(48000, false)
        compare(view.span, 7000 / 48000)
        compare(view.start + view.span / 2, 0.5)
    }

    // Per un ricevitore IQ il centro può muoversi dentro tutta la banda;
    // ricentrare qui cancellerebbe pan e zoom scelti dall'operatore.
    function test_iq_radio_keeps_the_full_delivered_band() {
        const view = deliveredView(2048000, true)
        compare(view.start, 0)
        compare(view.span, 1)
    }

    // Un canale fuori dalla finestra la fa spostare, e ci finisce dentro.
    function test_a_channel_outside_the_window_brings_it_in() {
        const span = 0.1                      // si guarda un decimo di banda
        const start = shownStart(7500000, 0.0, span)

        const visibleStart = testCase.bandStart + testCase.bandSpan * start
        const visibleEnd = visibleStart + testCase.bandSpan * span
        verify(visibleStart <= 7500000 && 7500000 <= visibleEnd,
               "il canale è rimasto fuori: finestra " + visibleStart + "–" + visibleEnd)
    }

    // Un canale già comodamente dentro non muove niente.
    function test_a_channel_already_visible_does_not_move_the_view() {
        // Finestra 7.400–7.600, canale proprio in mezzo.
        compare(shownStart(7500000, 0.4, 0.2), 0.4,
                "la vista si è spostata senza motivo")
    }

    // Appiccicato al bordo conta come fuori: un flag mezzo tagliato dal
    // margine è peggio di uno assente, perché lo si legge male invece di
    // andarlo a cercare.
    function test_a_channel_glued_to_the_edge_counts_as_outside() {
        verify(shownStart(7401000, 0.4, 0.2) !== 0.4,
               "il canale sul bordo non è stato centrato")
    }

    // Fuori dalla banda campionata non c'è vista che lo mostri: meglio non
    // toccare niente che spostarsi verso un punto che non esiste.
    function test_a_frequency_outside_the_band_is_ignored_data() {
        return [
            { tag: "sotto la banda", hz: 6000000 },
            { tag: "sopra la banda", hz: 9000000 },
            { tag: "non numerica",   hz: NaN },
        ]
    }

    function test_a_frequency_outside_the_band_is_ignored(data) {
        compare(shownStart(data.hz, 0.3, 0.2), 0.3,
                "la vista è andata a cercare il nulla")
    }

    // La finestra resta dentro la banda: portare in vista un canale al bordo
    // non deve far scorrere lo spettro oltre ciò che il device consegna.
    function test_the_window_stays_inside_the_band_data() {
        return [
            { tag: "primo hertz",  hz: 7000000 },
            { tag: "ultimo hertz", hz: 8000000 },
        ]
    }

    function test_the_window_stays_inside_the_band(data) {
        const span = 0.2
        const start = shownStart(data.hz, 0.5, span)

        verify(start >= 0, "finestra oltre l'inizio: " + start)
        verify(start + span <= 1.0001, "finestra oltre la fine: " + (start + span))
    }

    // A piena banda non c'è niente da portare in vista: tutto è già visibile,
    // e spostare una finestra larga quanto la banda vorrebbe dire uscirne.
    function test_at_full_span_nothing_moves() {
        compare(shownStart(7900000, 0, 1), 0, "la vista si è mossa a piena banda")
    }
}
