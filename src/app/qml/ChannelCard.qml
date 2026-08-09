// SPDX-License-Identifier: GPL-3.0-or-later
// La scheda di un canale RX: frequenza, livello, modo, filtro, AGC, disturbi.
//
// Sta in un file suo perché è il blocco più lungo dell'interfaccia, e perché
// la colonna che lo ospita deve restare leggibile: chi cerca l'ordine dei
// pannelli non vuole scorrere trecento righe di manopole per trovarlo.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
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
    required property bool squelchEnabled
    required property real squelchThresholdDb
    required property bool nrEnabled
    required property bool anfEnabled
    required property bool notchEnabled
    required property real notchFrequencyHz
    required property real notchWidthHz
    required property real signalDb
    required property real agcGainDb

    readonly property bool current: Session.channels.currentIndex === index

    Layout.fillWidth: true
    Layout.preferredHeight: layout.implicitHeight + 2 * Theme.spacing
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

        // ── Squelch ──────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            DsdrButton {
                text: qsTr("SQL")
                implicitWidth: 52
                implicitHeight: 24
                checkable: true
                checked: entry.squelchEnabled
                onToggled: Session.setChannelSquelch(
                               entry.index, checked, entry.squelchThresholdDb)
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: -140; to: -20
                enabled: entry.squelchEnabled
                value: entry.squelchThresholdDb
                onMoved: Session.setChannelSquelch(entry.index, true, value)
            }

            Text {
                text: Math.round(entry.squelchThresholdDb) + " dB"
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: entry.squelchEnabled ? Theme.textPrimary : Theme.textDisabled
                Layout.preferredWidth: 52
            }
        }

        // Uno squelch chiuso e una radio guasta suonano identici:
        // senza una spia si finisce a cercare il problema nel cavo
        // dell'antenna.
        Text {
            visible: entry.squelchEnabled
                     && entry.signalDb < entry.squelchThresholdDb
            text: qsTr("squelch chiuso")
            font.pixelSize: Theme.fontSmall
            color: Theme.warning
        }

        // ── Disturbi ─────────────────────────────────────────
        //
        // Tre interruttori, tutti spenti di fabbrica. Nessuno di
        // questi filtri è gratis, e la sigla è quella che
        // l'operatore conosce da trent'anni: NR, ANF, NOTCH.
        //
        // Il NB non è qui: toglie impulsi che arrivano su tutta la
        // banda, quindi vale per l'intera catena e sta nel suo
        // pannello (SPEC-003 §4).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: qsTr("NR")
                checkable: true
                checked: entry.nrEnabled
                onToggled: Session.setChannelNoiseReduction(entry.index, checked, 0.05)
            }

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: qsTr("ANF")
                checkable: true
                checked: entry.anfEnabled
                onToggled: Session.setChannelAutoNotch(entry.index, checked)
            }

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: qsTr("NOTCH")
                checkable: true
                checked: entry.notchEnabled
                onToggled: Session.setChannelNotch(entry.index, checked,
                                                   entry.notchFrequencyHz,
                                                   entry.notchWidthHz)
            }
        }

        // Il notch automatico toglie *tutte* le righe ferme, e in
        // CW la nota che si ascolta è una riga ferma: va detto qui,
        // non scoperto perdendo un QSO.
        Text {
            Layout.fillWidth: true
            // Il modo si riconosce dal nome e non dal numero: la
            // tabella dei modi vive in C++, e un indice cablato qui
            // resterebbe indietro senza dirlo.
            visible: entry.anfEnabled && entry.modeName.indexOf("CW") === 0
            text: qsTr("In CW l'ANF attenua anche la nota di ascolto.")
            font.pixelSize: Theme.fontSmall
            color: Theme.warning
            wrapMode: Text.WordWrap
        }

        // Frequenza del notch manuale: compare solo quando serve,
        // perché una manopola che non fa niente è peggio di una
        // manopola assente.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: entry.notchEnabled

            Text {
                text: qsTr("Notch")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: 200; to: 3000
                value: entry.notchFrequencyHz
                onMoved: Session.setChannelNotch(entry.index, true, value,
                                                 entry.notchWidthHz)
            }

            Text {
                text: Math.round(entry.notchFrequencyHz) + " Hz"
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textPrimary
                Layout.preferredWidth: 62
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
