// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione ACARS — sperimentale")
    anchors.centerIn: parent
    modal: false
    width: 620
    standardButtons: Dialog.Close
    onOpened: frequencyInput.text = root.controller.frequencyHz > 0 ? String(root.controller.frequencyHz) : ""
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox { objectName: "acarsEnabled"; text: qsTr("Abilita ricevitore ACARS"); checked: root.controller.enabled; onToggled: root.controller.enabled = checked }
        RowLayout {
            Label { text: qsTr("Frequenza (Hz)") }
            TextField { id: frequencyInput; objectName: "acarsFrequency"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: DoubleValidator { bottom: 1; decimals: 0; notation: DoubleValidator.StandardNotation } }
            Button { objectName: "acarsApply"; text: qsTr("Applica"); enabled: frequencyInput.acceptableInput; onClicked: root.controller.setFrequencyHz(Number(frequencyInput.text)) }
        }
        Text { objectName: "acarsStatus"; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; textFormat: Text.PlainText; text: root.controller.status }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Un canale VHF IQ; sintonia e sorgente sono gestite dall'applicazione. I blocchi con errore vengono scartati.") }
        Text { objectName: "acarsCounts"; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Messaggi: %1 · messaggi persi: %2 · blocchi IQ persi: %3 · errori: %4").arg(root.controller.messageCount).arg(root.controller.droppedMessages).arg(root.controller.droppedBlocks).arg(root.controller.workerErrors) }
        RowLayout {
            Text { Layout.fillWidth: true; color: Theme.textSecondary; text: qsTr("Ultimi 200 blocchi") }
            Button { objectName: "acarsClear"; text: qsTr("Cancella"); onClicked: root.controller.clearMessages() }
        }
        ListView {
            id: list
            objectName: "acarsMessages"
            Layout.fillWidth: true
            Layout.preferredHeight: 250
            clip: true
            model: root.controller.messages
            ScrollBar.vertical: ScrollBar { }
            delegate: Column {
                required property string mode
                required property string address
                required property string ack
                required property string label
                required property string blockId
                required property string messageText
                required property string ending
                width: list.width
                spacing: 3
                Text { objectName: "acarsMessageHeader"; width: parent.width; color: Theme.textPrimary; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; text: qsTr("%1 · %2 · %3 · %4 · ACK %5 · %6").arg(address).arg(label).arg(blockId).arg(mode).arg(ack).arg(ending) }
                Text { objectName: "acarsMessageText"; width: parent.width; color: Theme.textPrimary; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; text: messageText }
                Item { width: 1; height: 8 }
            }
        }
    }
}
