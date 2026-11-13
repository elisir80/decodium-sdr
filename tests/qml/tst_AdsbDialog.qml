// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "AdsbDialog"
    visible: true; when: windowShown
    width: 800; height: 600
    Component { id: dialogComponent; AdsbDialog { controller: Session.adsb } }
    function test_optional_and_disabled_initially() {
        verify(Session.adsb !== null)
        compare(Session.adsb.enabled, false)
        let dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        compare(Session.adsb.enabled, false)
        dialog.close()
    }
}
