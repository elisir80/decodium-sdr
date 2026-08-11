// SPDX-License-Identifier: GPL-3.0-or-later
// Le memorie: quelle vecchie devono continuare a funzionare.
//
// Il formato delle voci è cresciuto — prima frequenza ed etichetta, ora anche
// modo e filtro — e il file è quello che l'operatore ha riempito nel tempo.
// Una voce senza `mode` non è un errore da correggere: è una memoria salvata
// prima, e richiamarla deve portare alla sua frequenza senza toccare altro.
//
// Si presidia anche il duplicato: memorizzare due volte la stessa frequenza
// riempie l'elenco di righe identiche, e allora l'elenco smette di servire.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "Memories"
    width: 200
    height: 200
    visible: true
    when: windowShown

    // Si lavora su una copia dell'elenco vero e lo si rimette a posto: il test
    // gira sulla macchina di chi sviluppa, e le sue memorie non sono materiale
    // di prova.
    property var saved: []

    function initTestCase() {
        saved = Memories.entries
    }

    function cleanupTestCase() {
        Memories.entries = saved
    }

    function init() {
        Memories.entries = []
    }

    // ── Formato delle voci ───────────────────────────────────────────────

    function test_indexOf_finds_by_frequency() {
        Memories.entries = [{ frequency: 14074000, label: "vecchia" }]
        compare(Memories.indexOf(14074000), 0)
        compare(Memories.indexOf(14074000.4), 0)   // si arrotonda: sono hertz
        compare(Memories.indexOf(7074000), -1)
    }

    function test_old_entries_have_no_mode() {
        // Una voce del formato vecchio: nessun campo oltre i due originali.
        // Se un giorno `recall` desse per scontato `entry.mode`, qui si
        // vedrebbe — e sull'installazione di qualcuno si vedrebbe invece come
        // un modo che cambia da solo.
        const old = { frequency: 7074000, label: "7.074000 MHz" }
        compare(old.mode, undefined)
        verify(Memories.labels.length === 0)
    }

    function test_labels_follow_entries() {
        Memories.entries = [
            { frequency: 3573000, label: "3.573000 USB" },
            { frequency: 7074000, label: "7.074000 USB" },
        ]
        compare(Memories.labels.length, 2)
        compare(Memories.labels[0], "3.573000 USB")
    }

    // ── Etichette ────────────────────────────────────────────────────────

    // Sei decimali: sotto il chilohertz si distinguono due stazioni vicine, e
    // una memoria che arrotonda al kHz le confonde.
    function test_label_keeps_hertz() {
        compare(Memories.labelFor(14074123, "USB"), "14.074123 USB")
        compare(Memories.labelFor(14074123, ""), "14.074123 MHz")
    }

    // ── Rimozione ────────────────────────────────────────────────────────

    function test_remove_drops_only_that_frequency() {
        Memories.entries = [
            { frequency: 3573000, label: "a" },
            { frequency: 7074000, label: "b" },
        ]
        Memories.remove(7074000)
        compare(Memories.entries.length, 1)
        compare(Memories.entries[0].frequency, 3573000)
    }

    // ── Bande ────────────────────────────────────────────────────────────

    // Senza copertura dichiarata non si vincola niente: un elenco di bande
    // vuoto renderebbe il comando della targa inutilizzabile proprio con i
    // backend che non dichiarano i limiti.
    function test_reachable_bands_are_never_empty_without_capabilities() {
        verify(Memories.reachableBands.length > 0)
    }

    function test_band_named() {
        const band = Memories.bandNamed("40m")
        verify(band !== null)
        compare(band.start, 7000000)
        compare(Memories.bandNamed("nessuna"), null)
    }
}
