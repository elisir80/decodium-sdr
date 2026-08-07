// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — finestra principale.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

ApplicationWindow {
    id: window

    // Su schermi con fattore di scala elevato una finestra da 1440×860 punti
    // esce dal desktop: partiamo dalla dimensione desiderata ma restando
    // dentro l'area disponibile.
    width: Math.min(1440, Screen.desktopAvailableWidth - 100)
    height: Math.min(860, Screen.desktopAvailableHeight - 100)
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    color: Theme.background
    title: Session.connected
           ? qsTr("DECODIUM SDR — %1").arg(Session.deviceName)
           : qsTr("DECODIUM SDR")

    // ── Barra superiore ──────────────────────────────────────────────────
    header: Rectangle {
        implicitHeight: Theme.headerHeight
        color: Theme.surface

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLoose
            anchors.rightMargin: Theme.spacingLoose
            spacing: Theme.spacingLoose

            Text {
                text: "DECODIUM SDR"
                font.pixelSize: Theme.fontLarge
                font.bold: true
                font.letterSpacing: 1.5
                color: Theme.accent
            }

            Rectangle {
                width: 1
                Layout.fillHeight: true
                Layout.topMargin: Theme.spacing
                Layout.bottomMargin: Theme.spacing
                color: Theme.border
            }

            // Frequenza centrale del device.
            ColumnLayout {
                spacing: 0

                Text {
                    text: qsTr("Centro")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                Text {
                    text: (Session.centerFrequency / 1e6).toFixed(4) + " MHz"
                    font.pixelSize: Theme.fontLarge
                    font.family: Theme.monoFamily
                    color: Theme.textPrimary

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.SizeVerCursor
                        enabled: Session.connected
                        onWheel: (wheel) => {
                            const step = wheel.modifiers & Qt.ControlModifier ? 1000000 : 100000
                            Session.centerFrequency += wheel.angleDelta.y > 0 ? step : -step
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 0
                visible: Session.connected

                Text {
                    text: qsTr("Campionamento")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                DsdrComboBox {
                    implicitWidth: 132
                    implicitHeight: 24
                    model: Session.capabilities.sampleRates.map(r => (r / 1e6).toFixed(3) + " MS/s")
                    currentIndex: Session.capabilities.sampleRates.indexOf(Session.sampleRate)
                    onActivated: Session.sampleRate = Session.capabilities.sampleRates[currentIndex]
                }
            }

            Item { Layout.fillWidth: true }

            // Volume generale.
            RowLayout {
                spacing: Theme.spacing
                visible: Session.connected

                Text {
                    text: qsTr("Volume")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                DsdrSlider {
                    implicitWidth: 130
                    from: 0; to: 1
                    value: Session.audio.volume
                    onMoved: Session.audio.volume = value
                }
            }

            // Registrazione IQ: compare solo se la sorgente la consente.
            DsdrButton {
                visible: Session.capabilities.supportsRecording
                text: Session.recorder.recording
                      ? qsTr("■ %1").arg(Qt.formatTime(new Date(Session.recorder.durationMs), "mm:ss"))
                      : qsTr("● REC")
                checkable: true
                checked: Session.recorder.recording
                danger: Session.recorder.recording
                implicitWidth: 96
                enabled: Session.connected
                onToggled: Session.toggleRecording()
            }

            // §4.2: se il backend non trasmette, il PTT non esiste — non è
            // disabilitato, proprio non viene creato.
            DsdrButton {
                visible: Session.capabilities.canTransmit
                text: Session.transmitting ? qsTr("TX ON") : qsTr("PTT")
                checkable: true
                checked: Session.transmitting
                danger: true
                implicitWidth: 88
                enabled: Session.connected
                onToggled: Session.setPtt(checked)
            }

            DsdrButton {
                text: Session.connected ? qsTr("Disconnetti") : qsTr("Sorgente")
                onClicked: {
                    if (Session.connected)
                        Session.disconnectDevice()
                    else
                        discoveryPopup.open()
                }
            }
        }
    }

    // ── Corpo ────────────────────────────────────────────────────────────
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            implicitWidth: 4
            color: SplitHandle.pressed ? Theme.accent
                 : SplitHandle.hovered ? Theme.borderStrong : Theme.border
        }

        PanadapterPane {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 420
        }

        ChannelStrip {
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 300
            SplitView.maximumWidth: 520
        }
    }

    // ── Barra di stato ───────────────────────────────────────────────────
    footer: Rectangle {
        implicitHeight: 26
        color: Theme.surface

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLoose
            anchors.rightMargin: Theme.spacingLoose
            spacing: Theme.spacingLoose

            Rectangle {
                width: 8; height: 8; radius: 4
                color: Session.connected ? Theme.success : Theme.textDisabled
            }

            Text {
                text: Session.statusMessage
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Text {
                visible: Session.connected
                text: qsTr("audio %1 · %2 ms").arg(Session.audio.deviceName).arg(Session.audio.latencyMs)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
            }

            Text {
                text: Session.backendName
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
            }

            // Selettore lingua: elenca solo le lingue che hanno davvero una
            // traduzione compilata, così sceglierne una cambia sempre qualcosa.
            DsdrComboBox {
                implicitWidth: 132
                implicitHeight: 20

                model: Session.language.availableLanguages.filter(l => l.available)
                textRole: "name"
                valueRole: "code"

                Component.onCompleted: currentIndex = indexOfValue(Session.language.currentLanguage)

                onActivated: Session.language.setLanguage(currentValue)

                Connections {
                    target: Session.language
                    function onLanguageChanged() {
                        // La lingua può cambiare anche da fuori (ripristino
                        // all'avvio): il selettore deve restare allineato.
                        parent.currentIndex = parent.indexOfValue(Session.language.currentLanguage)
                    }
                }
            }
        }
    }

    // ── Selezione sorgente ───────────────────────────────────────────────
    Popup {
        id: discoveryPopup

        anchors.centerIn: Overlay.overlay
        width: 560
        height: 520
        modal: true
        focus: true
        padding: 0

        background: Rectangle {
            color: "transparent"
        }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }

        contentItem: DiscoveryPane {}

        Connections {
            target: Session
            function onConnectionChanged() {
                if (Session.connected)
                    discoveryPopup.close()
            }
        }
    }

    // All'avvio si parte dalla scelta della sorgente: senza device non c'è
    // nulla di sensato da mostrare.
    Component.onCompleted: {
        if (Session.connected || Session.discovering)
            return
        Session.startDiscovery()
        discoveryPopup.open()
    }
}
