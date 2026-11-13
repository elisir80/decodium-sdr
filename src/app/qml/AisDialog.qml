// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione AIS — sperimentale")
    anchors.centerIn: parent
    modal: false
    width: 540
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox {
            objectName: "aisEnabled"
            text: qsTr("Abilita decoder AIS")
            checked: root.controller.enabled
            onToggled: root.controller.enabled = checked
        }
        ComboBox {
            objectName: "aisChannel"
            Layout.fillWidth: true
            model: [qsTr("AIS 1 — 161.975 MHz"), qsTr("AIS 2 — 162.025 MHz")]
            currentIndex: root.controller.channel
            onActivated: root.controller.channel = currentIndex
        }
        Text { objectName: "aisStatus"; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { objectName: "aisCapabilities"; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Riceve un canale AIS alla volta, senza sintonia automatica. Può mostrare la posizione per i tipi 1–4, 9, 11, 17, 18, 19, 21 e 27; per gli altri mostra tipo e MMSI.") }
        Text { objectName: "aisType"; color: Theme.textPrimary; text: qsTr("Tipo messaggio: %1").arg(root.controller.messageType >= 0 ? root.controller.messageType : "—") }
        Text { objectName: "aisMmsi"; color: Theme.textPrimary; text: qsTr("MMSI: %1").arg(root.controller.mmsi || "—") }
        Text { objectName: "aisPosition"; color: Theme.textPrimary; text: (root.controller.messageType === 17 ? qsTr("Stazione di riferimento DGNSS nell'ultima trama: %1") : qsTr("Posizione nell'ultima trama: %1")).arg(root.controller.hasPosition ? qsTr("%1°, %2°").arg(root.controller.latitude.toFixed(5)).arg(root.controller.longitude.toFixed(5)) : qsTr("non disponibile")) }
        Text { objectName: "aisProcessedUtc"; color: Theme.textSecondary; text: qsTr("Ultima trama elaborata: %1 UTC").arg(root.controller.processedUtc || "—") }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Trame: %1 · messaggi persi: %2 · blocchi persi: %3 · errori: %4").arg(root.controller.frameCount).arg(root.controller.droppedMessages).arg(root.controller.droppedBlocks).arg(root.controller.workerErrors) }
    }
}
