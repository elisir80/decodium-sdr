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

    /// Porta la vista a una larghezza data, in hertz, tenendo fermo il centro.
    ///
    /// Il centro resta quello che si sta guardando, non quello della banda:
    /// chi stringe lo zoom sta seguendo un segnale, e ritrovarselo fuori
    /// schermo dopo aver premuto un pulsante di vista è il modo più rapido di
    /// perderlo.
    function zoomToSpan(hz) {
        if (!(spanHz > 0) || !(hz > 0))
            return

        const centre = viewStart + viewSpan / 2
        const span = Math.max(0.005, Math.min(1, hz / spanHz))
        viewSpan = span
        viewStart = Math.max(0, Math.min(1 - span, centre - span / 2))
    }

    /// Quota di altezza occupata dallo spettro; il resto è waterfall.
    readonly property real spectrumRatio: 0.45

    /// Il panadattatore, per chi deve comandarne la resa da fuori.
    ///
    /// I comandi del waterfall vivono nella colonna dei pannelli, che è un
    /// altro ramo dell'albero: senza questo appiglio dovrebbero cercarsi
    /// l'oggetto per conto proprio, e un giorno non lo troverebbero più.
    readonly property alias panadapterView: panadapter

    PanadapterView {
        id: panadapter
        anchors.fill: parent

        // `--no-panadapter` stacca la sorgente dal rendering GPU: serve a
        // isolare i problemi di prestazioni fra DSP e scene graph.
        feed: Qt.application.arguments.indexOf("--no-panadapter") >= 0 ? null : Session.spectrum
        // Niente binding su `floorDb`/`ceilingDb`: con la scala automatica è
        // il C++ a scriverli, e un binding qui li riporterebbe indietro.
        Component.onCompleted: {
            floorDb = -125
            ceilingDb = -25
        }
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

    // ── Scala di ampiezza ────────────────────────────────────────────────
    //
    // Legge gli estremi dal panadattatore invece di tenerne una copia: con la
    // scala automatica quei due valori si muovono da soli, e una scala
    // graduata su numeri diversi da quelli del rendering mentirebbe.
    LevelScale {
        anchors.fill: parent
        floorDb: panadapter.floorDb
        ceilingDb: panadapter.ceilingDb
        spectrumRatio: root.spectrumRatio
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

    // ── Piano bande e asse dei tempi ─────────────────────────────────────
    BandSegments {
        anchors.fill: parent
        startHz: root.visibleStartHz
        endHz: root.visibleStartHz + root.visibleSpanHz
        spectrumRatio: root.spectrumRatio
        visible: Session.connected
    }

    TimeScale {
        anchors.fill: parent
        historySeconds: panadapter.historySeconds
        spectrumRatio: root.spectrumRatio
        visible: Session.connected
    }

    // ── Targa del canale attivo ──────────────────────────────────────────
    //
    // Un Repeater su tutto il modello, con visibile solo il canale corrente:
    // è il modo di leggere le proprietà di una riga restando reattivi ai suoi
    // cambiamenti, senza aggiungere al modello una copia delle stesse
    // proprietà da tenere allineata a mano.
    Repeater {
        model: Session.channels

        delegate: VfoPlate {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.spacing
            z: 5
            visible: Session.channels.currentIndex === index
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

            // Il passo lo dice la targa del VFO, non questo file: era cablato
            // qui e in ChannelStrip, e i due potevano divergere senza che
            // nessuno se ne accorgesse. Shift resta la scorciatoia per il
            // passo fine, dieci volte più piccolo.
            const step = wheel.modifiers & Qt.ShiftModifier
                       ? Math.max(1, Tuning.stepHz / 10)
                       : Tuning.stepHz
            const direction = wheel.angleDelta.y > 0 ? 1 : -1
            if (Session.channels.currentIndex >= 0)
                Session.nudgeChannel(Session.channels.currentIndex, direction * step)
        }
    }

    // ── Indicatore di zoom ───────────────────────────────────────────────
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing
        // Rientrato oltre la colonna delle etichette in dB: la prima tacca
        // della scala cade proprio qui, e le due scritte si coprirebbero.
        anchors.leftMargin: 42
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
