// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "AprsDialog"
    visible: true
    when: windowShown
    width: 800; height: 600
    QtObject {
        id: mock
        property bool enabled: false
        property int selectedChannel: -1
        property var channels: [{id: 4, label: "NFM A"}, {id: 9, label: "NFM B"}]
        property string status: enabled ? "Ricezione APRS attiva" : "Decoder disattivato"
        property string source: ""
        property string destination: ""
        property string information: ""
        property bool hasPosition: false
        property double latitude: 0
        property double longitude: 0
        property string positionObject: ""
        property string lastFrame: ""
        property int frameCount: 0
        property int droppedFrames: 0
        property int droppedBlocks: 0
        function selectChannel(id) { selectedChannel = id }
    }
    Component { id: dialogComponent; AprsDialog { controller: mock } }
    function init() { mock.enabled = false; mock.selectedChannel = -1; mock.hasPosition = false; mock.positionObject = "" }
    function test_session_integration_without_selected_channel() {
        verify(Session.aprs !== null)
        Session.aprs.enabled = true
        compare(Session.aprs.selectedChannel, -1)
        compare(Session.aprs.status, "Seleziona un canale NFM")
        Session.aprs.enabled = false
        compare(Session.aprs.status, "Decoder disattivato")
    }
    function test_opt_in_and_explicit_selection() {
        let dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const selector = findChild(dialog, "aprsChannel")
        const enable = findChild(dialog, "aprsEnabled")
        verify(selector !== null && enable !== null)
        compare(selector.currentIndex, -1)
        compare(mock.enabled, false)
        mock.selectChannel(9)
        tryCompare(selector, "currentIndex", 1)
        compare(mock.selectedChannel, 9)
        mock.selectChannel(4)
        tryCompare(selector, "currentIndex", 0)
        mouseClick(enable)
        compare(mock.enabled, true)
        mouseClick(enable)
        compare(mock.enabled, false)
        dialog.close()
    }
    function test_received_information_is_plain_text() {
        mock.information = "<b>TEST</b>"
        let dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const information = findChild(dialog, "aprsInformation")
        verify(information !== null)
        compare(information.textFormat, Text.PlainText)
        verify(information.text.indexOf("<b>TEST</b>") >= 0)
        dialog.close()
        mock.information = ""
    }
    function test_object_is_separate_plain_text() {
        mock.source = "IU8LMC-7"
        mock.hasPosition = true; mock.latitude = 35.9; mock.longitude = 14.5
        mock.positionObject = "<b>TEST</b>"
        let dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null); dialog.open(); tryVerify(function() { return dialog.opened })
        const source = findChild(dialog, "aprsSource")
        const position = findChild(dialog, "aprsPosition")
        const object = findChild(dialog, "aprsPositionObject")
        verify(source !== null && position !== null && object !== null)
        verify(source.text.indexOf("IU8LMC-7") >= 0)
        verify(position.text.indexOf("35.90000") >= 0)
        compare(object.textFormat, Text.PlainText)
        verify(object.text.indexOf("<b>TEST</b>") >= 0)
        verify(object.visible)
        mock.positionObject = ""; mock.hasPosition = false
        tryVerify(function() { return !object.visible })
        verify(position.text.indexOf("non disponibile") >= 0)
        dialog.close(); mock.source = ""
    }
}
