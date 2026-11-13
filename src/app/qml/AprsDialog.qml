// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione APRS AFSK 1200 — sperimentale")
    anchors.centerIn: parent
    modal: false
    width: 540
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox {
            objectName: "aprsEnabled"
            text: qsTr("Abilita decoder APRS")
            checked: root.controller.enabled
            onToggled: root.controller.enabled = checked
        }
        Label { text: qsTr("Canale NFM") }
        ComboBox {
            id: channelPicker
            objectName: "aprsChannel"
            Layout.fillWidth: true
            model: root.controller.channels
            textRole: "label"
            valueRole: "id"
            currentIndex: {
                for (let i = 0; i < root.controller.channels.length; ++i)
                    if (root.controller.channels[i].id === root.controller.selectedChannel) return i
                return -1
            }
            displayText: currentIndex < 0 ? qsTr("Seleziona un canale") : currentText
            onActivated: root.controller.selectChannel(currentValue)
        }
        Text { Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Seleziona un canale NFM già sintonizzato su una frequenza APRS. Il decoder non modifica la sintonia. Le coordinate sono riconosciute per posizioni compresse e non compresse senza ambiguità.") }
        Text { color: Theme.textPrimary; text: qsTr("Trame valide: %1 · perse: %2 · blocchi audio persi: %3").arg(root.controller.frameCount).arg(root.controller.droppedFrames).arg(root.controller.droppedBlocks) }
        Text { objectName: "aprsSource"; color: Theme.textPrimary; textFormat: Text.PlainText; text: qsTr("Da: %1").arg(root.controller.source || "—") }
        Text { color: Theme.textPrimary; textFormat: Text.PlainText; text: qsTr("A: %1").arg(root.controller.destination || "—") }
        Text { objectName: "aprsInformation"; Layout.fillWidth: true; color: Theme.textPrimary; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; text: qsTr("Informazioni: %1").arg(root.controller.information || "—") }
        Text { objectName: "aprsPosition"; Layout.fillWidth: true; color: Theme.textPrimary; textFormat: Text.PlainText; text: qsTr("Posizione nell'ultima trama: %1").arg(root.controller.hasPosition ? qsTr("%1°, %2°").arg(root.controller.latitude.toFixed(5)).arg(root.controller.longitude.toFixed(5)) : qsTr("non disponibile")) }
        Text { objectName: "aprsPositionObject"; visible: root.controller.positionObject.length > 0; Layout.fillWidth: true; color: Theme.textSecondary; textFormat: Text.PlainText; text: qsTr("Oggetto APRS: %1").arg(root.controller.positionObject) }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; text: qsTr("Trama AX.25: %1").arg(root.controller.lastFrame || "—") }
    }
}
