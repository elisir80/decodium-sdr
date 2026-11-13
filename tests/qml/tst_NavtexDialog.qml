// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "NavtexDialog"
    visible: true
    when: windowShown
    width: 800; height: 600
    QtObject {
        id: mock
        property bool enabled: false
        property var channels: []
        property int selectedChannel: -1
        property string status: "In attesa"
        property string header: "EA01"
        property string text: "<b>TEST</b>"
        property int messageCount: 1
        property int errors: 0
        property int droppedMessages: 0
        property int droppedBlocks: 0
        property bool truncated: false
        function selectChannel(id) { selectedChannel = id }
    }
    Component { id: dialogComponent; NavtexDialog { controller: mock } }
    function test_received_text_is_literal() {
        let dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const body = findChild(dialog, "navtexText")
        const header = findChild(dialog, "navtexHeader")
        const counts = findChild(dialog, "navtexCounts")
        const notice = findChild(dialog, "navtexQualityNotice")
        verify(body !== null && header !== null)
        verify(counts !== null && notice !== null)
        verify(counts.text.indexOf("caratteri non riconosciuti: 0") >= 0)
        verify(notice.text.indexOf("può contenere errori") >= 0)
        compare(body.textFormat, TextEdit.PlainText)
        compare(body.text, "<b>TEST</b>")
        compare(header.textFormat, Text.PlainText)
        dialog.close()
    }
}
