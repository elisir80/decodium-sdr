// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Dialog {
    id: root
    required property var controller
    title: qsTr("Ricezione NAVTEX sperimentale")
    anchors.centerIn: parent
    width: 600; height: 470
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 10
        CheckBox { objectName: "navtexEnabled"; text: qsTr("Abilita decoder NAVTEX"); checked: root.controller.enabled; onToggled: root.controller.enabled=checked }
        Label { text: qsTr("Canale USB o LSB") }
        ComboBox {
            objectName: "navtexChannel"; Layout.fillWidth: true
            model: root.controller.channels; textRole: "label"; valueRole: "id"
            currentIndex: {
                for(let i=0;i<root.controller.channels.length;++i)
                    if(root.controller.channels[i].id===root.controller.selectedChannel) return i
                return -1
            }
            displayText: currentIndex<0 ? qsTr("Seleziona un canale") : currentText
            onActivated: root.controller.selectChannel(currentValue)
        }
        Text { objectName: "navtexStatus"; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.WordWrap; text: root.controller.status }
        Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; text: qsTr("Sintonizza il segnale audio a 1000 Hz. Ricezione sperimentale: non usare per la sicurezza della navigazione.") }
        Text { objectName: "navtexHeader"; color: Theme.textPrimary; textFormat: Text.PlainText; text: qsTr("Messaggio: %1").arg(root.controller.header || "—") }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            TextArea { objectName: "navtexText"; readOnly: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; text: root.controller.text; color: Theme.textPrimary }
        }
        Text { objectName: "navtexCounts"; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap
            text: qsTr("Messaggi: %1 · caratteri non riconosciuti: %2 · persi: %3 · blocchi persi: %4%5")
                .arg(root.controller.messageCount).arg(root.controller.errors).arg(root.controller.droppedMessages)
                .arg(root.controller.droppedBlocks).arg(root.controller.truncated ? qsTr(" · testo troncato") : "") }
        Text { objectName: "navtexQualityNotice"; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap
            text: qsTr("Anche con zero caratteri non riconosciuti, il testo può contenere errori.") }
    }
}
