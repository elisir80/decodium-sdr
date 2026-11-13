// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
Dialog {
    id: root
    required property var controller
    objectName: "dscDialog"
    title: qsTr("DSC RX — distress MF/HF parziale")
    width: 620
    height: 430
    modal: false
    standardButtons: Dialog.Close
    contentItem: ColumnLayout {
        CheckBox { objectName: "dscEnabled"; text: qsTr("Abilita ricezione"); checked: root.controller.enabled; onToggled: root.controller.enabled = checked }
        Label { objectName: "dscStatus"; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.controller.status }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Solo messaggi distress MF/HF; sorgente audio esterna a 24 kHz. Posizione e ora sono mostrate come codici DSC, non interpretati.") }
        Label { objectName: "dscDrops"; text: qsTr("Blocchi persi: %1 · risultati persi: %2").arg(root.controller.droppedBlocks).arg(root.controller.droppedResults) }
        ListView {
            objectName: "dscMessages"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: root.controller.messages
            delegate: Label {
                required property var modelData
                objectName: "dscMessage"
                width: ListView.view.width; wrapMode: Text.WrapAnywhere
                text: qsTr("MMSI %1 · natura %2 · posizione DSC %3 · ora DSC %4 · copie riparate %5")
                    .arg(modelData.mmsi).arg(modelData.nature).arg(modelData.positionRaw).arg(modelData.utcRaw).arg(modelData.repaired)
            }
        }
    }
}
