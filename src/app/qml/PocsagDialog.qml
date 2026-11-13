// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione POCSAG — sperimentale")
    anchors.centerIn: parent
    modal: false
    width: 500
    standardButtons: Dialog.Close
    onOpened: {
        baudPicker.currentIndex = Math.max(0, baudPicker.model.indexOf(root.controller.baud))
        frequencyInput.text = root.controller.frequencyHz > 0 ? String(root.controller.frequencyHz) : ""
    }
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox {
            objectName: "pocsagEnabled"
            text: qsTr("Abilita ricevitore POCSAG")
            checked: root.controller.enabled
            onToggled: root.controller.enabled = checked
        }
        RowLayout {
            Label { text: qsTr("Baud") }
            ComboBox { id: baudPicker; objectName: "pocsagBaud"; model: [512, 1200, 2400] }
            Label { text: qsTr("Frequenza (Hz)") }
            TextField { id: frequencyInput; objectName: "pocsagFrequency"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: DoubleValidator { bottom: 1; decimals: 0; notation: DoubleValidator.StandardNotation } }
            Button { objectName: "pocsagApply"; text: qsTr("Applica"); enabled: frequencyInput.acceptableInput; onClicked: root.controller.configureRequested(baudPicker.currentValue, Number(frequencyInput.text)) }
        }
        Text { objectName: "pocsagStatus"; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { objectName: "pocsagTuningHelp"; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("La sorgente IQ deve essere attiva e comprendere la frequenza scelta nella sua banda. Applica configura il decoder: non cambia la sintonia della radio.") }
        Text { objectName: "pocsagCounts"; color: Theme.textPrimary; text: qsTr("Messaggi: %1 · messaggi persi: %2 · blocchi IQ persi: %3").arg(root.controller.messageCount).arg(root.controller.droppedMessages).arg(root.controller.droppedBlocks) }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Cronologia: ultimi 500 messaggi; i più vecchi vengono rimossi.") }
        ListView {
            id: messages
            objectName: "pocsagMessages"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            clip: true
            model: root.controller.messages
            ScrollBar.vertical: ScrollBar { }
            delegate: Column {
                required property int address
                required property int functionCode
                required property string messageText
                required property int correctedBits
                width: messages.width
                spacing: 3
                Text {
                    objectName: "pocsagMessageHeader"
                    width: parent.width
                    color: Theme.textPrimary
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    text: qsTr("Indirizzo: %1 · Funzione: %2 · Bit corretti: %3").arg(address).arg(functionCode).arg(correctedBits)
                }
                Text {
                    objectName: "pocsagMessageText"
                    width: parent.width
                    color: Theme.textPrimary
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    text: messageText
                }
                Item { width: 1; height: 8 }
            }
        }
    }
}
