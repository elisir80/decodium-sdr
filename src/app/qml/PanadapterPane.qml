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

    // ── Zoom orizzontale ─────────────────────────────────────────────────
    // `viewStart` e `viewSpan` sono frazioni della banda campionata. Tutto
    // quello che si disegna sopra il panadattatore — griglia, flag VFO — usa
    // le conversioni qui sotto, così zoom e sovrapposizioni non possono
    // scivolare gli uni rispetto agli altri.
    property real viewStart: 0
    property real viewSpan: 1

    readonly property real visibleSpanHz: spanHz * viewSpan
    readonly property real visibleStartHz: startHz + spanHz * viewStart
    readonly property real visibleCenterHz: visibleStartHz + visibleSpanHz / 2

    function frequencyAt(x) {
        return visibleStartHz + (x / Math.max(width, 1)) * visibleSpanHz
    }

    function xForFrequency(hz) {
        return (hz - visibleStartHz) / visibleSpanHz * width
    }

    /// Zoom attorno a un punto: la frequenza sotto il puntatore resta ferma,
    /// che è l'unico comportamento che non fa perdere il segno a chi guarda.
    function zoomAt(x, factor) {
        const anchor = viewStart + (x / Math.max(width, 1)) * viewSpan
        const span = Math.max(0.005, Math.min(1, viewSpan * factor))
        viewSpan = span
        viewStart = Math.max(0, Math.min(1 - span, anchor - (x / Math.max(width, 1)) * span))
    }

    function resetZoom() {
        viewStart = 0
        viewSpan = 1
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
        viewStart: root.viewStart
        viewSpan: root.viewSpan
        traceColor: Theme.spectrumTrace
        fillColor: Theme.spectrumFill
        backgroundColor: Theme.spectrumBackground
    }

    // ── Griglia di frequenza ─────────────────────────────────────────────
    FrequencyGrid {
        anchors.fill: parent
        spanHz: root.visibleSpanHz
        centerHz: root.visibleCenterHz
        spectrumRatio: root.spectrumRatio
        // Senza sorgente non c'è banda da graduare: una scala di zeri
        // sarebbe solo rumore visivo.
        visible: Session.connected
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
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: root.viewSpan < 1 ? Qt.OpenHandCursor : Qt.CrossCursor
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

        // Trascinamento = scorrimento della banda visibile. Ha senso solo
        // sotto zoom: a piena banda non c'è nulla oltre i bordi.
        property real dragAnchorX: 0
        property real dragAnchorStart: 0

        onPressed: (mouse) => {
            dragAnchorX = mouse.x
            dragAnchorStart = root.viewStart
        }

        onPositionChanged: (mouse) => {
            if (!pressed || root.viewSpan >= 1)
                return
            const delta = (dragAnchorX - mouse.x) / Math.max(root.width, 1) * root.viewSpan
            root.viewStart = Math.max(0, Math.min(1 - root.viewSpan, dragAnchorStart + delta))
        }

        onWheel: (wheel) => {
            if (!Session.connected)
                return

            // Ctrl+rotellina = zoom, come in ogni programma che mostri una
            // scala continua. Senza Ctrl la rotellina sintonizza, che è il
            // gesto che si usa più spesso.
            if (wheel.modifiers & Qt.ControlModifier) {
                root.zoomAt(wheel.x, wheel.angleDelta.y > 0 ? 0.8 : 1.25)
                return
            }

            const step = wheel.modifiers & Qt.ShiftModifier ? 10 : 100
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

    // ── Indicatore di zoom ───────────────────────────────────────────────
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.spacing
        width: zoomLabel.implicitWidth + 2 * Theme.spacing
        height: 22
        radius: Theme.radiusSmall
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.82)
        border.width: 1
        border.color: Theme.border
        visible: root.viewSpan < 0.999

        Text {
            id: zoomLabel
            anchors.centerIn: parent
            text: (1 / root.viewSpan).toFixed(1) + "\u00d7  \u00b7  "
                  + Math.round(root.visibleSpanHz / 1000) + " kHz"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.accent
        }

        MouseArea {
            anchors.fill: parent
            // Un clic sull'indicatore torna a piena banda: è dove si guarda
            // quando ci si accorge di essere troppo stretti.
            onClicked: root.resetZoom()
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
