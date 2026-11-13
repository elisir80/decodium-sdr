// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone component: SessionManager integration is intentionally pending.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "backendShutdownDialog"
    title: qsTr("Chiusura radio")
    width: 440
    modal: true
    closePolicy: Popup.NoAutoClose
    property var controller: null
    property string statusText: controller ? controller.statusText : ""
    property bool retryAllowed: controller ? controller.retryAllowed : false
    // Only enable when the backend has been safely returned and app state restored.
    property bool cancelAllowed: false
    signal retryRequested()
    signal cancelRequested()
    onRetryRequested: if (controller) controller.retry()

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            objectName: "backendShutdownStatus"
            Layout.fillWidth: true
            text: root.statusText
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("L'applicazione resta aperta finché la radio non è stata fermata in sicurezza.")
            wrapMode: Text.WordWrap
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "backendShutdownRetry"
            text: qsTr("Riprova")
            enabled: root.retryAllowed
            Accessible.name: text
            onClicked: root.retryRequested()
        }
        Button {
            objectName: "backendShutdownCancel"
            text: qsTr("Annulla chiusura")
            enabled: root.cancelAllowed
            Accessible.name: text
            onClicked: root.cancelRequested()
        }
    }
}
