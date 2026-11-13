// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione DSC sperimentale")
    anchors.centerIn: parent
    width: 540
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox { objectName: "dscEnabled"; text: qsTr("Abilita decoder DSC"); checked: root.controller.enabled; onToggled: root.controller.enabled=checked }
        Label { text: qsTr("Canale USB o LSB") }
        ComboBox {
            objectName: "dscChannel"; Layout.fillWidth: true
            model: root.controller.channels; textRole: "label"; valueRole: "id"
            currentIndex: {
                for(let i=0;i<root.controller.channels.length;++i)
                    if(root.controller.channels[i].id===root.controller.selectedChannel) return i
                return -1
            }
            displayText: currentIndex<0 ? qsTr("Seleziona un canale") : currentText
            onActivated: root.controller.selectChannel(currentValue)
        }
        Text { objectName: "dscStatus"; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("MF/HF sperimentale: soccorso, riscontro e rilancio a tutte le navi o indirizzati, tutte le navi, individuale, gruppo e area. Sintonizza il segnale audio a 1700 Hz. Altri formati e VHF non sono supportati. Non usare per la sicurezza della vita umana.") }
        Text { objectName: "dscFormat"; color: Theme.textPrimary; text: qsTr("Tipo: %1").arg(root.controller.lastFormat || "—") }
        Text { objectName: "dscCategory"; color: Theme.textPrimary; text: qsTr("Categoria: %1").arg(root.controller.lastCategory || "—") }
        Text { objectName: "dscSequence"; color: Theme.textPrimary; text: qsTr("Sequenza: %1").arg(root.controller.lastSequence || "—") }
        Text { objectName: "dscMmsi"; color: Theme.textPrimary; text: qsTr("Ultimo MMSI: %1").arg(root.controller.lastMmsi || "—") }
        Text { color: Theme.textSecondary; text: qsTr("Chiamate: %1 · perse: %2 · blocchi persi: %3").arg(root.controller.messageCount).arg(root.controller.droppedMessages).arg(root.controller.droppedBlocks) }
    }
}
