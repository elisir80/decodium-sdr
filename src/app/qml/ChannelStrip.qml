// SPDX-License-Identifier: GPL-3.0-or-later
// Channel strip: un blocco di controlli per ogni canale RX.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    /// Il panadattatore da comandare dal pannello «Waterfall».
    ///
    /// Arriva da fuori invece di essere cercato: la colonna non sa dove viva
    /// lo spettro, e non deve saperlo.
    property PanadapterView panadapter: null

    color: Theme.surface
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Text {
                text: qsTr("Canali")
                font.pixelSize: Theme.fontLarge
                font.bold: true
                color: Theme.textPrimary
                Layout.fillWidth: true
                // Su una strip stretta il titolo si riduceva a una lettera
                // sola: meglio troncarlo con i puntini che lasciare una "C".
                elide: Text.ElideRight
                Layout.minimumWidth: 0
            }

            Text {
                text: Session.channels.count + " / " + Session.capabilities.maxRxChannels
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary
            }

            DsdrButton {
                text: "+"
                implicitWidth: 32
                enabled: Session.connected
                         && Session.channels.count < Session.capabilities.maxRxChannels
                onClicked: Session.addChannel(Session.centerFrequency)
            }
        }

        // Scegliere dove andare viene prima di regolare il canale.
        FrequencyPanel {
            Layout.fillWidth: true
            visible: Session.connected
        }

        // Controlli del backend attivo, se ne dichiara.
        BackendPanelHost {
            Layout.fillWidth: true
            visible: Session.connected && Session.capabilities.nativePanels.length > 0
        }

        // Resa dello spettro. Chiuso di fabbrica: si tocca quando l'immagine
        // non convince, non a ogni sessione.
        PanelFrame {
            Layout.fillWidth: true
            title: qsTr("Waterfall")
            collapsed: true
            visible: root.panadapter !== null

            WaterfallControls {
                Layout.fillWidth: true
                panadapter: root.panadapter
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacing
            model: Session.channels

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: entry

                required property int index
                required property color channelColor
                required property string label
                required property real frequencyHz
                required property int mode
                required property string modeName
                required property int filterLowHz
                required property int filterHighHz
                required property int agcMode
                required property real agcThresholdDb
                required property real volume
                required property bool muted
                required property real signalDb
                required property real agcGainDb

                readonly property bool current: Session.channels.currentIndex === index

                width: ListView.view.width
                height: layout.implicitHeight + 2 * Theme.spacing
                radius: Theme.radius
                color: current ? Theme.surfaceRaised : Theme.surface
                border.width: 1
                border.color: current ? entry.channelColor : Theme.border

                MouseArea {
                    anchors.fill: parent
                    onClicked: Session.channels.currentIndex = entry.index
                    z: -1
                }

                ColumnLayout {
                    id: layout
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacingTight

                    // ── Intestazione ─────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: entry.channelColor
                        }

                        Text {
                            text: entry.label
                            font.pixelSize: Theme.fontNormal
                            font.bold: true
                            color: Theme.textPrimary
                        }

                        Item { Layout.fillWidth: true }

                        DsdrButton {
                            text: entry.muted ? qsTr("Muto") : qsTr("Attivo")
                            implicitWidth: 66
                            checkable: true
                            checked: entry.muted
                            danger: entry.muted
                            onToggled: Session.setChannelMuted(entry.index, checked)
                        }

                        DsdrButton {
                            text: "×"
                            implicitWidth: 28
                            enabled: Session.channels.count > 1
                            onClicked: Session.removeChannel(entry.index)
                        }
                    }

                    // ── Frequenza ────────────────────────────────────────
                    Text {
                        text: (entry.frequencyHz / 1e6).toFixed(6) + " MHz"
                        font.pixelSize: Theme.fontLarge
                        font.family: Theme.monoFamily
                        color: entry.current ? Theme.accent : Theme.textPrimary
                        Layout.fillWidth: true

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onWheel: (wheel) => {
                                // Stesso passo della rotellina sullo spettro:
                                // due manopole della stessa radio non possono
                                // muovere quantità diverse.
                                const step = wheel.modifiers & Qt.ShiftModifier
                                           ? Math.max(1, Tuning.stepHz / 10)
                                           : Tuning.stepHz
                                Session.nudgeChannel(entry.index,
                                                     wheel.angleDelta.y > 0 ? step : -step)
                            }
                        }
                    }

                    // Lo strumento a lancetta solo per il canale in ascolto:
                    // quattro quadranti animati in colonna sarebbero una
                    // giostra, e tre di quei quattro non li sta guardando
                    // nessuno.
                    AnalogMeter {
                        Layout.fillWidth: true
                        visible: entry.current
                        levelDb: entry.signalDb
                    }

                    SignalMeter {
                        Layout.fillWidth: true
                        visible: !entry.current
                        levelDb: entry.signalDb
                    }

                    // ── Modo e filtro ────────────────────────────────────
                    ModeSelector {
                        Layout.fillWidth: true
                        channelIndex: entry.index
                        mode: entry.mode
                        filterLowHz: entry.filterLowHz
                        filterHighHz: entry.filterHighHz
                    }

                    // ── AGC ──────────────────────────────────────────────
                    Text {
                        text: qsTr("AGC")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textSecondary
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingTight

                        Repeater {
                            model: Session.agcModeNames()

                            delegate: DsdrButton {
                                required property int index
                                required property string modelData

                                Layout.fillWidth: true
                                implicitWidth: 0
                                implicitHeight: 24
                                text: modelData
                                checkable: true
                                checked: entry.agcMode === index
                                enabled: Session.capabilities.clientAgc
                                onClicked: Session.setChannelAgcMode(entry.index, index)
                            }
                        }
                    }

                    // ── AGC-T ────────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Session.capabilities.clientAgc && entry.agcMode !== 0

                        Text {
                            text: qsTr("AGC-T")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        DsdrSlider {
                            Layout.fillWidth: true
                            from: -130; to: -30
                            value: entry.agcThresholdDb
                            onMoved: Session.setChannelAgcThreshold(entry.index, value)
                        }

                        Text {
                            text: Math.round(entry.agcThresholdDb) + " dB"
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.monoFamily
                            color: Theme.textSecondary
                            Layout.preferredWidth: 52
                        }
                    }

                    // I due cursori del passabanda se ne sono andati: i
                    // preimpostati di ModeSelector coprono i valori d'uso, e
                    // per il taglio fine ci sono i bordi della fascia sullo
                    // spettro, dove si vede cosa si sta tagliando.
                    Text {
                        text: qsTr("guadagno AGC %1 dB").arg(Math.round(entry.agcGainDb))
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                        color: Theme.textDisabled
                    }

                    // ── Volume ───────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Vol")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }

                        DsdrSlider {
                            Layout.fillWidth: true
                            from: 0; to: 1
                            value: entry.volume
                            accentColor: entry.channelColor
                            onMoved: Session.setChannelVolume(entry.index, value)
                        }
                    }
                }
            }
        }
    }
}
