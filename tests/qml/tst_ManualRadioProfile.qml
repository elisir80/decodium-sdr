// SPDX-License-Identifier: GPL-3.0-or-later
// Il profilo CAT è un contratto operativo: porta e formato seriale devono
// tornare insieme, altrimenti un riavvio può ritrovare la radio ma interrogarla
// con 8N2 o sulla prima porta della lista.
import QtCore
import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "ManualRadioProfile"
    when: windowShown

    Component {
        id: profileComponent

        QtObject {
            property Settings store: Settings {
                category: "radio/manual-cat"
                property string driverId: ""
                property string port: ""
                property int baud: 0
                property int dataBits: 8
                property int parity: 0
                property int stopBits: 1
                property int flowControl: -1
                property bool dtr: false
                property bool rts: false
            }
        }
    }

    function test_the_complete_cat_profile_survives_a_new_instance() {
        const saved = createTemporaryObject(profileComponent, testCase)
        verify(saved !== null)
        saved.store.driverId = "civ"
        saved.store.port = "cu.usbserial-312410"
        saved.store.baud = 115200
        saved.store.dataBits = 8
        saved.store.parity = 0
        saved.store.stopBits = 1
        saved.store.flowControl = 0
        saved.store.dtr = false
        saved.store.rts = false

        // Settings scarica i valori quando l'oggetto viene chiuso: distruggerlo
        // qui verifica davvero il caso dell'app chiusa e poi riaperta, non la
        // lettura della stessa istanza ancora viva.
        saved.destroy()
        wait(20)

        const restored = createTemporaryObject(profileComponent, testCase)
        verify(restored !== null)
        compare(restored.store.driverId, "civ")
        compare(restored.store.port, "cu.usbserial-312410")
        compare(restored.store.baud, 115200)
        compare(restored.store.dataBits, 8)
        compare(restored.store.parity, 0)
        compare(restored.store.stopBits, 1)
        compare(restored.store.flowControl, 0)
        compare(restored.store.dtr, false)
        compare(restored.store.rts, false)
    }
}
