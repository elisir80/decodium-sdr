// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello dello spettro: rendering GPU sotto, sovrapposizioni QML sopra.
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Item {
    id: root

    readonly property real centerHz: Session.centerFrequency
    readonly property real spanHz: Session.sampleRate > 0 ? Session.sampleRate : 1
    readonly property real startHz: centerHz - spanHz / 2

    function frequencyAt(x) {
        return startHz + (x / Math.max(width, 1)) * spanHz
    }

    function xForFrequency(hz) {
        return (hz - startHz) / spanHz * width
    }

    /// Quota di altezza occupata dallo spettro; il resto è waterfall.
    readonly property real spectrumRatio: 0.45

    PanadapterView {
        id: panadapter
        anchors.fill: parent

        // `--no-panadapter` stacca la sorgente dal rendering GPU: serve a
        // isolare i problemi di prestazioni fra DSP e scene graph.
        feed: Qt.application.arguments.indexOf("--no-panadapter") >= 0 ? null : Session.spectrum
        floorDb: levelControls.floorValue
        ceilingDb: levelControls.ceilingValue
        spectrumRatio: root.spectrumRatio
        traceColor: Theme.spectrumTrace
        fillColor: Theme.spectrumFill
        backgroundColor: Theme.spectrumBackground
    }

    // ── Griglia di frequenza ─────────────────────────────────────────────
    FrequencyGrid {
        anchors.fill: parent
        spanHz: root.spanHz
        centerHz: root.centerHz
        spectrumRatio: root.spectrumRatio
    }

    // ── Flag VFO, uno per canale ─────────────────────────────────────────
    Repeater {
        model: Session.channels

        delegate: VfoFlag {
            required property int index
            required property color channelColor
            required property string label
            required property real frequencyHz
            required property int filterLowHz
            required property int filterHighHz
            required property real signalDb

            vfoRow: index
            vfoColor: channelColor
            vfoLabel: label
            vfoFrequency: frequencyHz
            bandLowHz: filterLowHz
            bandHighHz: filterHighHz
            levelDb: signalDb
            vfoSelected: Session.channels.currentIndex === index

            xForFrequency: root.xForFrequency
            frequencyAt: root.frequencyAt
        }
    }

    // ── Interazione: click-to-tune, pan, zoom ────────────────────────────
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        cursorShape: Qt.CrossCursor
        z: -1

        onClicked: (mouse) => {
            if (!Session.connected)
                return
            const target = Math.round(root.frequencyAt(mouse.x))
            if (Session.channels.currentIndex >= 0)
                Session.setChannelFrequency(Session.channels.currentIndex, target)
            else
                Session.addChannel(target)
        }

        onDoubleClicked: (mouse) => {
            if (Session.connected)
                Session.addChannel(Math.round(root.frequencyAt(mouse.x)))
        }

        onWheel: (wheel) => {
            if (!Session.connected)
                return
            const step = wheel.modifiers & Qt.ControlModifier ? 1000
                       : wheel.modifiers & Qt.ShiftModifier ? 10 : 100
            const direction = wheel.angleDelta.y > 0 ? 1 : -1
            if (Session.channels.currentIndex >= 0)
                Session.nudgeChannel(Session.channels.currentIndex, direction * step)
        }
    }

    // ── Controlli di livello del waterfall ───────────────────────────────
    Rectangle {
        id: levelControls

        property real floorValue: -125
        property real ceilingValue: -25

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacing
        width: 132
        height: contentColumn.implicitHeight + 2 * Theme.spacing
        radius: Theme.radiusSmall
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.82)
        border.width: 1
        border.color: Theme.border

        Column {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spacingTight

            Text {
                text: qsTr("Fondo %1 dB").arg(Math.round(levelControls.floorValue))
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                width: parent.width
                from: -160; to: -60
                value: levelControls.floorValue
                onMoved: levelControls.floorValue = value
            }

            Text {
                text: qsTr("Vetta %1 dB").arg(Math.round(levelControls.ceilingValue))
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                width: parent.width
                from: -80; to: 0
                value: levelControls.ceilingValue
                onMoved: levelControls.ceilingValue = value
            }
        }
    }

    // ── Stato "nessun segnale" ───────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: !Session.connected
        text: qsTr("Nessun device connesso")
        font.pixelSize: Theme.fontLarge
        color: Theme.textDisabled
    }
}
