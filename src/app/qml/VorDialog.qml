// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("VOR — sperimentale")
    anchors.centerIn: parent
    width: 510
    standardButtons: Dialog.Close
    onOpened: frequencyInput.text = root.controller.frequencyHz > 0 ? String(root.controller.frequencyHz) : ""
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        Text { objectName: "vorWarning"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textPrimary; text: qsTr("Sperimentale: non usare per navigazione o sicurezza.") }
        CheckBox { objectName: "vorEnabled"; text: qsTr("Abilita VOR"); checked: root.controller.enabled; onToggled: root.controller.enabled = checked }
        RowLayout {
            Label { text: qsTr("Frequenza (Hz)") }
            TextField { id: frequencyInput; objectName: "vorFrequency"; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: DoubleValidator { bottom: 1; decimals: 0; notation: DoubleValidator.StandardNotation } }
            Button { objectName: "vorApply"; text: qsTr("Applica"); enabled: frequencyInput.acceptableInput; onClicked: root.controller.setFrequencyHz(Number(frequencyInput.text)) }
        }
        Text { objectName: "vorStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textPrimary; text: root.controller.status }
        Text { objectName: "vorRadial"; color: Theme.textPrimary; text: root.controller.valid ? qsTr("Radiale stimato: %1°").arg(root.controller.radial.toFixed(1)) : qsTr("Radiale non disponibile") }
        Text { objectName: "vorIdentifier"; color: Theme.textPrimary; text: root.controller.identifier ? qsTr("Identificativo stimato: %1").arg(root.controller.identifier) : qsTr("Identificativo non disponibile") }
        Text { objectName: "vorDrops"; color: Theme.textSecondary; text: qsTr("Blocchi persi: %1 · risultati sovrascritti: %2").arg(root.controller.droppedBlocks).arg(root.controller.droppedResults) }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textSecondary; text: qsTr("IQ: 96 kHz–2,048 MHz. L’identificativo è sperimentale: voce e rumore possono renderlo errato o impedirne la lettura. Non usare per la navigazione.") }
    }
}
