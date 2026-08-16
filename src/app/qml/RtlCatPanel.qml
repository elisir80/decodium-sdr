// SPDX-License-Identifier: GPL-3.0-or-later
// Stato del ponte CAT per un panadapter RTL-SDR sull'uscita IF della radio.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("CAT · PANADAPTER IF")
    property int refresh: 0
    readonly property var catStatus: {
        root.refresh
        return Session.nativeCommand("rtlcat.status", {}) || ({})
    }
    readonly property bool inputBlocked: catStatus.inputBlocked === true
    readonly property bool transmitting: catStatus.transmitting === true
    readonly property bool pttKnown: catStatus.pttKnown === true

    Timer {
        interval: 100
        repeat: true
        running: Session.connected
        onTriggered: root.refresh++
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: stateText.implicitHeight + 12
        radius: Theme.radiusSmall
        color: root.inputBlocked ? Qt.rgba(0.84, 0.25, 0.23, 0.18)
                                 : Qt.rgba(0.18, 0.72, 0.46, 0.14)
        border.width: 1
        border.color: root.inputBlocked ? Theme.danger : Theme.success

        Text {
            id: stateText
            anchors.fill: parent
            anchors.margins: 6
            text: root.transmitting
                  ? qsTr("TX RILEVATA · IQ RTL-SDR BLOCCATO")
                  : root.inputBlocked
                    ? root.pttKnown
                      ? qsTr("CAT NON CONFERMATO · IQ RTL-SDR BLOCCATO")
                      : qsTr("PTT CAT NON DISPONIBILE · IQ RTL-SDR BLOCCATO")
                    : qsTr("RX CONFERMATA · IQ RTL-SDR ATTIVO")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            color: root.inputBlocked ? Theme.danger : Theme.success
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.catStatus.radio || root.catStatus.port
        text: (root.catStatus.radio || qsTr("Radio Hamlib"))
              + (root.catStatus.port ? " · " + root.catStatus.port : "")
              + (root.catStatus.hamlibModel ? " · ID " + root.catStatus.hamlibModel : "")
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFamily
        color: Theme.textSecondary
        elide: Text.ElideRight
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Il VFO della radio e' l'autorita': i cambi di frequenza e modo sono bidirezionali via Hamlib. Il blocco software ferma e svuota il flusso IQ; configura comunque il mute IF in TX della radio o un relè RF esterno per proteggere fisicamente l'ingresso RTL-SDR.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
