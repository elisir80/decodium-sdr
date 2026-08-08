// SPDX-License-Identifier: GPL-3.0-or-later
// Trasporto di una registrazione IQ: dove siamo, pausa, riavvolgimento.
//
// È l'unico backend in cui il tempo è una grandezza governabile, e per questo
// i comandi passano dalla valvola nativa invece che dal seam: nessuna radio
// vera sa cosa voglia dire "torna indietro di dieci secondi".
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("REGISTRAZIONE")

    property var status: ({})

    readonly property real durationMs: status.durationMs !== undefined ? status.durationMs : 0
    readonly property real positionMs: status.positionMs !== undefined ? status.positionMs : 0
    readonly property bool paused: status.paused === true
    readonly property bool scrubbing: progress.pressed

    function refresh() {
        root.status = Session.nativeCommand("iqfile.status", {}) || ({})
    }

    /// mm:ss — su una registrazione le ore non servono e ruberebbero spazio.
    function formatMs(ms) {
        const total = Math.max(0, Math.floor(ms / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    Component.onCompleted: refresh()

    // Si interroga il backend invece di farsi notificare: la posizione cambia
    // di continuo e non vale un segnale per ogni blocco di campioni. Dieci
    // volte al secondo è più di quanto l'occhio distingua su una barra.
    Timer {
        running: root.visible
        interval: 100
        repeat: true
        onTriggered: if (!root.scrubbing) root.refresh()
    }

    // ── Posizione ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: root.formatMs(root.positionMs)
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 42
        }

        DsdrSlider {
            id: progress
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, root.durationMs)
            value: root.positionMs
            enabled: root.durationMs > 0
            onMoved: Session.nativeCommand("iqfile.seek", { "ms": Math.round(value) })
        }

        Text {
            text: root.formatMs(root.durationMs)
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textSecondary
            Layout.preferredWidth: 42
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Trasporto ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            Layout.preferredWidth: 34
            implicitWidth: 0
            implicitHeight: 26
            text: "|◀"
            onClicked: {
                Session.nativeCommand("iqfile.seek", { "ms": 0 })
                root.refresh()
            }
        }

        DsdrButton {
            Layout.preferredWidth: 64
            implicitWidth: 0
            implicitHeight: 26
            text: "« 10s"
            // Dieci secondi indietro: il salto giusto per riascoltare un
            // nominativo perso, senza dover cercare il punto sulla barra.
            onClicked: {
                Session.nativeCommand("iqfile.seek",
                                      { "ms": Math.max(0, root.positionMs - 10000) })
                root.refresh()
            }
        }

        DsdrButton {
            Layout.fillWidth: true
            implicitWidth: 0
            implicitHeight: 26
            text: root.paused ? qsTr("Riprendi") : qsTr("Pausa")
            onClicked: {
                Session.nativeCommand("iqfile.setPaused", { "paused": !root.paused })
                root.refresh()
            }
        }

        DsdrButton {
            Layout.preferredWidth: 34
            implicitWidth: 0
            implicitHeight: 26
            text: "↻"
            checkable: true
            checked: root.status.loop === true
            onToggled: {
                Session.nativeCommand("iqfile.setLoop", { "loop": checked })
                root.refresh()
            }
        }
    }

    // ── Velocità ─────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Text {
            text: qsTr("Velocità")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Repeater {
            model: [0.5, 1, 2, 4]

            delegate: DsdrButton {
                required property real modelData
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: modelData + "×"
                checkable: true
                checked: Math.abs((root.status.speed !== undefined ? root.status.speed : 1)
                                  - modelData) < 0.01
                onClicked: {
                    Session.nativeCommand("iqfile.setSpeed", { "speed": modelData })
                    root.refresh()
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        // Rallentare aiuta sul CW veloce; accelerare serve a scorrere una
        // registrazione lunga in cerca del punto giusto. Il DSP demodula
        // comunque, ma a velocità diversa da uno l'audio non è più fedele.
        text: qsTr("Fuori da 1× l'audio cambia tono: serve a cercare, non ad ascoltare.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // ── Provenienza ──────────────────────────────────────────────────────
    Text {
        Layout.fillWidth: true
        visible: root.status.recordedWith !== undefined && root.status.recordedWith !== ""
        text: qsTr("Registrata con %1").arg(root.status.recordedWith || "")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
        elide: Text.ElideRight
    }

    // Senza sidecar la frequenza mostrata è dedotta dal nome del file: va
    // detto, perché tutto il resto della UI la tratterebbe come certa.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: root.status.hasSidecar === false

        Rectangle {
            width: 8; height: 8; radius: 4
            color: Theme.warning
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Metadati assenti: la frequenza è dedotta dal nome del file.")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }
    }
}
