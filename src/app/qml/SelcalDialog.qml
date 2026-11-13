// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione SELCAL sperimentale")
    anchors.centerIn: parent
    width: 520
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox { objectName: "selcalEnabled"; text: qsTr("Abilita decoder SELCAL"); checked: root.controller.enabled; onToggled: root.controller.enabled=checked }
        Label { text: qsTr("Canale AM o SSB") }
        ComboBox {
            objectName: "selcalChannel"; Layout.fillWidth: true
            model: root.controller.channels; textRole: "label"; valueRole: "id"
            currentIndex: {
                for(let i=0;i<root.controller.channels.length;++i)
                    if(root.controller.channels[i].id===root.controller.selectedChannel) return i
                return -1
            }
            displayText: currentIndex<0 ? qsTr("Seleziona un canale") : currentText
            onActivated: root.controller.selectChannel(currentValue)
        }
        Text { objectName: "selcalStatus"; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { objectName: "selcalLimitations"; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Sintonizza prima un canale AM o SSB. Il decoder non modifica la sintonia, può produrre falsi allarmi e non certifica l'identità dell'aeromobile.") }
        Text { objectName: "selcalCode"; color: Theme.textPrimary; text: qsTr("Ultimo codice: %1").arg(root.controller.lastCode || "—") }
        Text { color: Theme.textSecondary; text: qsTr("Chiamate: %1 · perse: %2 · blocchi persi: %3").arg(root.controller.messageCount).arg(root.controller.droppedMessages).arg(root.controller.droppedBlocks) }
    }
}
