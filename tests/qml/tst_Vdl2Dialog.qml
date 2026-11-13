// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "Vdl2Dialog"
    visible: true
    when: windowShown
    width: 900; height: 700
    ListModel { id: mockMessages }
    QtObject {
        id: mock
        property var messages: mockMessages
        property int droppedResults: 0
        property int droppedBlocks: 0
        property int workerErrors: 0
        property string sourceStatus: "In attesa della sorgente IQ"
    }
    Component { id: dialogComponent; Vdl2Dialog { controller: mock } }
    function test_empty_and_populated() {
        mockMessages.clear()
        let dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        let list = findChild(dialog, "vdl2Messages")
        let status = findChild(dialog, "vdl2SourceStatus")
        verify(status !== null)
        compare(status.text, "In attesa della sorgente IQ")
        mock.sourceStatus = "Sorgente IQ attiva"
        tryCompare(status, "text", "Sorgente IQ attiva")
        verify(list !== null)
        compare(list.count, 0)
        mockMessages.append({source:"4CA2B1", destination:"10A000", kind:"RR", protocol:"", summary:"<b>synthetic</b>"})
        tryCompare(list, "count", 1)
        let heading = null
        let summary = null
        tryVerify(function() {
            heading = findChild(list, "vdl2FrameHeading")
            summary = findChild(list, "vdl2FrameSummary")
            return heading !== null && summary !== null
        })
        compare(heading.textFormat, Text.PlainText)
        compare(summary.textFormat, Text.PlainText)
        compare(summary.text, "<b>synthetic</b>")
        mockMessages.clear()
        tryCompare(list, "count", 0)
        dialog.close()
    }
}
