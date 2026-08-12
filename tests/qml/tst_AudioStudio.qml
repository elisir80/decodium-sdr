// SPDX-License-Identifier: GPL-3.0-or-later
// Lo studio dell'audio: che si istanzi, e che i conti che fa siano quelli.
//
// La parte grafica non si verifica qui — non si verifica in nessun test — ma
// istanziare i componenti sì, ed è quello che coglie l'errore più probabile:
// un import mancante. `QtQuick.Shapes` non è fra i moduli che un'applicazione
// Qt si porta dietro d'ufficio, e senza di lui l'intero pannello non si carica:
// non si vede un oscilloscopio vuoto, si vede una colonna senza studio audio.
//
// Il resto sono i due conti che il pannello fa in proprio: la griglia in
// kilohertz e la scala dei livelli. Sbagliarli non fa fallire niente — disegna
// una riga nel posto sbagliato, e chi legge crede al disegno.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "AudioStudio"
    width: 400
    height: 400
    visible: true
    when: windowShown

    Component {
        id: scopeComponent
        AudioScope {}
    }

    Component {
        id: barComponent
        AudioLevelBar {}
    }

    Component {
        id: studioComponent
        AudioStudioPanel {}
    }

    // ── Che esistano ─────────────────────────────────────────────────────

    function test_the_scope_is_built() {
        const scope = createTemporaryObject(scopeComponent, testCase)
        verify(scope !== null, "oscilloscopio non istanziato: manca QtQuick.Shapes?")

        // Spento quando non si vede: un oscilloscopio dentro un pannello
        // chiuso continuerebbe a svuotare il ring venticinque volte al secondo
        // per disegnare niente.
        scope.visible = false
        verify(!scope.running, "continua a girare da invisibile")
    }

    function test_the_studio_is_built() {
        const studio = createTemporaryObject(studioComponent, testCase)
        verify(studio !== null, "pannello non istanziato")
        verify(studio.detachable, "lo studio deve potersi staccare: in colonna non ci sta")
    }

    // ── La scala dei livelli ─────────────────────────────────────────────

    function test_the_level_scale_maps_decibels() {
        const bar = createTemporaryObject(barComponent, testCase)

        // Il fondo scala a destra, il fondo della scala a sinistra, e in mezzo
        // proporzionale: è la sola cosa che questa barra promette.
        fuzzyCompare(bar.fraction(bar.ceilingDb), 1.0, 0.001)
        fuzzyCompare(bar.fraction(bar.floorDb), 0.0, 0.001)
        fuzzyCompare(bar.fraction((bar.floorDb + bar.ceilingDb) / 2), 0.5, 0.001)

        // Fuori scala si ferma ai bordi: un livello a −200 dB non deve
        // disegnare una barra di larghezza negativa.
        fuzzyCompare(bar.fraction(-200), 0.0, 0.001)
        fuzzyCompare(bar.fraction(+40), 1.0, 0.001)
    }

    // ── La griglia in kilohertz ──────────────────────────────────────────

    function test_the_grid_thins_out_when_the_view_widens() {
        const studio = createTemporaryObject(studioComponent, testCase)

        // Il conto delle tacche dipende dalla porzione in vista, e la vista la
        // decide il panadattatore dentro il pannello. Quello che si presidia è
        // la regola: più larga è la vista, più rade sono le righe — venti
        // righe in tre centimetri non sono una griglia, sono un retino.
        verify(studio.gridTicks.length > 0, "nessuna tacca")
        verify(studio.gridTicks.length < 40,
               "troppe tacche: " + studio.gridTicks.length)

        // E stanno dentro la porzione mostrata, sempre.
        for (const hz of studio.gridTicks) {
            verify(hz >= studio.spanStartHz - 1, "tacca prima dell'inizio: " + hz)
            verify(hz <= studio.spanStartHz + studio.spanWidthHz + 1,
                   "tacca oltre la fine: " + hz)
        }
    }
}
