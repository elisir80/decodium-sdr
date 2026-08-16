// SPDX-License-Identifier: GPL-3.0-or-later
// Il pannello operativo non può restare un QML che compila ma non nasce:
// contiene due Repeaters su cataloghi vuoti e deve reggere anche prima che ci
// sia un ricevitore collegato.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "OperationsDialog"
    width: 640
    height: 480
    visible: true
    when: windowShown

    Component {
        id: dialogComponent
        OperationsDialog { }
    }

    function test_dialog_constructs_with_empty_catalogues() {
        const dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null, "OperationsDialog non istanziato")
        verify(dialog.width > 0, "larghezza nulla")
        verify(dialog.height > 0, "altezza nulla")
        compare(dialog.actionIds.length, 6)
        compare(dialog.actionIds.indexOf("ptt"), -1,
                "lo scheduler non deve esporre azioni PTT")
    }
}
