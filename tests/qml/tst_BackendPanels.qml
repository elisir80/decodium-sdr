// SPDX-License-Identifier: GPL-3.0-or-later
// I pannelli backend-specifici devono istanziarsi anche senza un device
// aperto: il Loader li crea appena il backend li dichiara, e a quel punto i
// comandi nativi restituiscono valori vuoti.
//
// È il caso che rompe più facilmente un pannello: se un binding assume che
// `nativeCommand()` restituisca sempre qualcosa, il pannello non compare e
// l'utente non ha modo di capire perché.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "BackendPanels"
    width: 320
    height: 480
    visible: true
    when: windowShown

    Component {
        id: soapyPanel
        SoapyDevicePanel {}
    }

    Component {
        id: netTcpPanel
        NetTcpDevicePanel {}
    }

    Component {
        id: panelHost
        BackendPanelHost {}
    }

    function test_soapy_panel_survives_empty_commands() {
        const panel = createTemporaryObject(soapyPanel, testCase, { width: 300 })
        verify(panel !== null, "SoapyDevicePanel non istanziato")
        verify(panel.height > 0, "pannello di altezza nulla")
        verify(panel.title.length > 0, "titolo mancante")
        // Senza device i comandi nativi non rispondono: i default devono
        // reggere invece di lasciare proprietà indefinite.
        verify(panel.antennas !== undefined)
        verify(panel.gainRange !== undefined)
        compare(panel.automaticGain, true)
    }

    function test_nettcp_panel_survives_empty_commands() {
        const panel = createTemporaryObject(netTcpPanel, testCase, { width: 300 })
        verify(panel !== null, "NetTcpDevicePanel non istanziato")
        verify(panel.height > 0, "pannello di altezza nulla")
        compare(panel.automaticGain, true)
        compare(panel.ppm, 0)
        compare(panel.biasTee, false)
    }

    function test_host_is_hidden_without_panels() {
        const host = createTemporaryObject(panelHost, testCase, { width: 300 })
        verify(host !== null)
        // Nessun backend connesso ⇒ nessun pannello ⇒ niente da mostrare.
        compare(host.visible, false)
    }
}
