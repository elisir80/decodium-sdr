// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    objectName: "vdl2Dialog"
    required property var controller
    title: qsTr("Ricezione VDL Mode 2 — sperimentale")
    anchors.centerIn: parent
    modal: false
    width: 720
    height: 480
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 8
        Text {
            Layout.fillWidth: true
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            text: qsTr("Ricezione sperimentale su un solo canale. Le trame mostrate hanno FCS valido; le informazioni ACARS complete, la posizione e la ricezione radio reale non sono ancora verificate.")
        }
        Text {
            objectName: "vdl2SourceStatus"
            Layout.fillWidth: true
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
            text: root.controller.sourceStatus
        }
        Text {
            objectName: "vdl2Counters"
            Layout.fillWidth: true
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            text: qsTr("Risultati persi: %1 · blocchi persi: %2 · errori: %3")
                .arg(root.controller.droppedResults).arg(root.controller.droppedBlocks).arg(root.controller.workerErrors)
        }
        ListView {
            objectName: "vdl2Messages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 5
            model: root.controller.messages
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                required property string source
                required property string destination
                required property string kind
                required property string protocol
                required property string summary
                width: ListView.view.width
                height: content.implicitHeight + 16
                color: Theme.surface
                border.color: Theme.border
                radius: Theme.radiusSmall
                Column {
                    id: content
                    x: 8; y: 8; width: parent.width - 16
                    Text { objectName: "vdl2FrameHeading"; width: parent.width; color: Theme.textPrimary; textFormat: Text.PlainText; text: source + " → " + destination + " · " + kind + (protocol ? " · " + protocol : ""); elide: Text.ElideRight }
                    Text { objectName: "vdl2FrameSummary"; width: parent.width; color: Theme.textSecondary; textFormat: Text.PlainText; text: summary; wrapMode: Text.Wrap; visible: summary.length > 0 }
                }
            }
        }
    }
}
