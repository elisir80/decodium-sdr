// SPDX-License-Identifier: GPL-3.0-or-later
// Comandi della resa del waterfall.
//
// Stavano sovrapposti al panadattatore, con l'idea che si regolano guardando
// l'effetto. L'idea era giusta, il posto no: un riquadro semitrasparente steso
// sopra il segnale copriva proprio la porzione di spettro che si stava
// cercando di rendere leggibile, e il rumore che gli passava dietro rendeva
// illeggibili le etichette. Ora è un pannello come gli altri, nella colonna
// destra: l'effetto resta sotto gli occhi, perché lo spettro occupa tutta la
// larghezza rimanente.
//
// Questo file è il *contenuto* del pannello: la cornice, il titolo e il
// collasso li mette PanelFrame.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import DecodiumSdr

ColumnLayout {
    id: root

    /// Il panadattatore da comandare.
    required property PanadapterView panadapter

    readonly property bool showRelief: panadapter.waterfallMode === PanadapterView.Relief

    spacing: Theme.spacingTight

    // Le preferenze non possono essere alias verso `panadapter`: un alias QML
    // arriva solo a `id.proprietà`, non a una proprietà di una proprietà. Si
    // ripristina all'avvio e si salva a ogni modifica, con `restored` a fare
    // da guardia perché i binding non scrivano i valori di fabbrica sopra
    // quelli dell'utente prima di averli letti.
    property bool restored: false

    Settings {
        id: prefs
        category: "waterfall"
        property int paletteIndex: 0
        property real gamma: 0.85
        property real blackThreshold: 0.06
        property bool autoRange: true
        property int waterfallMode: PanadapterView.Flat
        property real tilt: 58
        property real rotation3d: 0
        property real reliefScale: 0.45
        property real reliefGrid: 0.35
        property real floorFlattening: 0
        property real timeSpan: 1
        property int averaging: 3
        property bool peakHold: true
        property real peakDecayDb: 12
    }

    Component.onCompleted: {
        panadapter.paletteIndex = prefs.paletteIndex
        panadapter.gamma = prefs.gamma
        panadapter.blackThreshold = prefs.blackThreshold
        panadapter.autoRange = prefs.autoRange
        panadapter.waterfallMode = prefs.waterfallMode
        panadapter.tilt = prefs.tilt
        panadapter.rotation3d = prefs.rotation3d
        panadapter.reliefScale = prefs.reliefScale
        panadapter.reliefGrid = prefs.reliefGrid
        panadapter.floorFlattening = prefs.floorFlattening
        panadapter.timeSpan = prefs.timeSpan
        panadapter.peakHold = prefs.peakHold
        panadapter.peakDecayDb = prefs.peakDecayDb
        Session.spectrumAveraging = prefs.averaging
        restored = true
    }

    // Le preferenze seguono i comandi. Prima erano alias, che con un pannello
    // ricostruito dal Loader riscriverebbero i valori salvati con quelli di
    // fabbrica; `restored` fa da guardia.
    Connections {
        target: root.panadapter
        enabled: root.restored

        function onPaletteChanged() { prefs.paletteIndex = root.panadapter.paletteIndex }
        function onToneChanged() {
            prefs.gamma = root.panadapter.gamma
            prefs.blackThreshold = root.panadapter.blackThreshold
            prefs.floorFlattening = root.panadapter.floorFlattening
        }
        // Il fermo immagine non si salva: riaprire il programma con lo spettro
        // già fermo è il modo più rapido di credere che la radio sia guasta.
        function onTimeViewChanged() { prefs.timeSpan = root.panadapter.timeSpan }
        function onAutoRangeChanged() { prefs.autoRange = root.panadapter.autoRange }
        function onWaterfallModeChanged() { prefs.waterfallMode = root.panadapter.waterfallMode }
        function onSceneChanged() {
            prefs.tilt = root.panadapter.tilt
            prefs.rotation3d = root.panadapter.rotation3d
            prefs.reliefScale = root.panadapter.reliefScale
            prefs.reliefGrid = root.panadapter.reliefGrid
        }
        function onPeakHoldChanged() {
            prefs.peakHold = root.panadapter.peakHold
            prefs.peakDecayDb = root.panadapter.peakDecayDb
        }
    }

    // La media non è una regolazione del panadattatore: è il DSP a decidere
    // quante trasformate fanno una riga, e il rendering si limita a consumare
    // quello che gli arriva. Passa da `Session` perché il feed vive sul thread
    // del DSP, dove QML non può seguirlo.
    Connections {
        target: Session
        enabled: root.restored

        function onSpectrumAveragingChanged() { prefs.averaging = Session.spectrumAveraging }
    }

    /// Etichetta, lettura e cursore come un blocco solo.
    ///
    /// Sette ripetizioni dello stesso schema erano sette occasioni di
    /// disallinearsi — ed è quello che era successo: le etichette partivano dal
    /// bordo del pannello mentre i cursori rientravano del proprio padding.
    component LevelRow: Column {
        id: levelRow

        property string label
        property string readout
        property alias from: levelSlider.from
        property alias to: levelSlider.to
        property alias sliderItem: levelSlider
        /// Per i comandi che contano invece di misurare: un passo intero e lo
        /// scatto, così il cursore non può fermarsi fra due valori validi.
        property alias stepSize: levelSlider.stepSize
        property alias snapMode: levelSlider.snapMode

        /// Il valore mostrato. Chi lo usa lo lega alla proprietà del
        /// panadattatore; `moved` riporta indietro il trascinamento.
        property alias value: levelSlider.value

        signal moved(real value)

        spacing: 0

        Item {
            width: parent.width
            height: caption.implicitHeight

            Text {
                id: caption
                x: levelSlider.leftPadding
                text: levelRow.label
                font.pixelSize: Theme.fontSmall
                color: levelRow.enabled ? Theme.textSecondary : Theme.textDisabled
            }

            // La lettura va a destra, incolonnata: così cambia sotto gli occhi
            // senza spostare l'etichetta a ogni cifra in più o in meno.
            Text {
                anchors.right: parent.right
                anchors.rightMargin: levelSlider.rightPadding
                text: levelRow.readout
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: levelRow.enabled ? Theme.textPrimary : Theme.textDisabled
            }
        }

        DsdrSlider {
            id: levelSlider
            width: parent.width
            onMoved: levelRow.moved(value)
        }
    }

    // ── Vista ────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            Layout.fillWidth: true
            implicitWidth: 0
            text: qsTr("Piatto")
            checkable: true
            checked: !root.showRelief
            onClicked: root.panadapter.waterfallMode = PanadapterView.Flat
        }

        DsdrButton {
            Layout.fillWidth: true
            implicitWidth: 0
            text: qsTr("Rilievo")
            checkable: true
            checked: root.showRelief
            onClicked: root.panadapter.waterfallMode = PanadapterView.Relief
        }
    }

    // ── Palette ──────────────────────────────────────────────────────────
    DsdrComboBox {
        Layout.fillWidth: true
        model: root.panadapter.paletteNames
        currentIndex: root.panadapter.paletteIndex
        onActivated: (index) => root.panadapter.paletteIndex = index
    }

    // ── Scala automatica ─────────────────────────────────────────────────
    DsdrButton {
        Layout.fillWidth: true
        implicitWidth: 0
        text: qsTr("Scala automatica")
        checkable: true
        checked: root.panadapter.autoRange
        onClicked: root.panadapter.autoRange = checked
    }

    // Quello che l'antenna misura, non quello che comandiamo: sta su una riga
    // sua, in accento, perché con la scala automatica i cursori sotto sono un
    // riflesso di questa misura e non un'impostazione.
    Text {
        Layout.fillWidth: true
        visible: root.panadapter.autoRange
        text: qsTr("misurato  %1 … %2 dB")
              .arg(Math.round(root.panadapter.noiseFloorDb))
              .arg(Math.round(root.panadapter.peakLevelDb))
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFamily
        color: Theme.accent
        elide: Text.ElideRight
    }

    LevelRow {
        id: floorRow
        Layout.fillWidth: true
        label: qsTr("Fondo")
        readout: qsTr("%1 dB").arg(Math.round(root.panadapter.floorDb))
        from: -160; to: -60
        enabled: !root.panadapter.autoRange
        value: root.panadapter.floorDb
        onMoved: (v) => root.panadapter.floorDb = v
    }

    LevelRow {
        id: ceilingRow
        Layout.fillWidth: true
        label: qsTr("Vetta")
        readout: qsTr("%1 dB").arg(Math.round(root.panadapter.ceilingDb))
        from: -80; to: 0
        enabled: !root.panadapter.autoRange
        value: root.panadapter.ceilingDb
        onMoved: (v) => root.panadapter.ceilingDb = v
    }

    // ── Tono ─────────────────────────────────────────────────────────────
    LevelRow {
        Layout.fillWidth: true
        label: qsTr("Contrasto")
        readout: root.panadapter.gamma.toFixed(2)
        from: 0.2; to: 2.5
        value: root.panadapter.gamma
        onMoved: (v) => root.panadapter.gamma = v
    }

    LevelRow {
        Layout.fillWidth: true
        label: qsTr("Soglia")
        readout: qsTr("%1%").arg(Math.round(root.panadapter.blackThreshold * 100))
        from: 0; to: 0.6
        value: root.panadapter.blackThreshold
        onMoved: (v) => root.panadapter.blackThreshold = v
    }

    // ── Appiattimento del fondo ──────────────────────────────────────────
    //
    // Il rumore non è piatto lungo la banda: un disturbo locale, la risposta
    // del preselettore, una emittente vicina lo alzano su una porzione di
    // spettro e non sulle altre. La scala però è una sola, e dove il fondo è
    // più alto il colore si accende ovunque — i segnali deboli ci scompaiono
    // dentro.
    LevelRow {
        Layout.fillWidth: true
        label: qsTr("Fondo piatto")
        readout: root.panadapter.floorFlattening < 0.01
                 ? qsTr("no")
                 : qsTr("%1%").arg(Math.round(root.panadapter.floorFlattening * 100))
        from: 0; to: 1
        value: root.panadapter.floorFlattening
        onMoved: (v) => root.panadapter.floorFlattening = v
    }

    // Va detto dove si paga, e si paga in onestà: la traccia resta la misura,
    // il waterfall diventa una mappa corretta. Chi legge un livello lo legge
    // sulla traccia, che non è stata toccata.
    Text {
        Layout.fillWidth: true
        visible: root.panadapter.floorFlattening >= 0.01
        text: qsTr("solo il waterfall: la traccia resta la misura")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }

    // ── Media fra le righe ───────────────────────────────────────────────
    //
    // Il fondo di una FFT non mediata respira di parecchi decibel da una riga
    // all'altra, e quel respiro è tutto quello che si vede su una banda quieta.
    // Mediando N trasformate il fondo si ferma e i segnali deboli restano.
    LevelRow {
        Layout.fillWidth: true
        label: qsTr("Media")
        readout: Session.spectrumAveraging <= 1
                 ? qsTr("spenta")
                 : qsTr("%1 FFT").arg(Session.spectrumAveraging)
        from: 1; to: 8
        stepSize: 1
        snapMode: Slider.SnapAlways
        value: Session.spectrumAveraging
        onMoved: (v) => Session.spectrumAveraging = Math.round(v)
    }

    // Il costo della media è dichiarato dove si paga: meno righe al secondo
    // vuol dire più storia sullo schermo, e vale la pena saperlo prima di
    // chiedersi perché il waterfall scorre più piano.
    Text {
        Layout.fillWidth: true
        visible: Session.spectrumAveraging > 1 && root.panadapter.historySeconds > 0
        text: qsTr("storia  %1 s").arg(Math.round(root.panadapter.historySeconds))
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFamily
        color: Theme.textSecondary
        elide: Text.ElideRight
    }

    // ── Scorrimento ──────────────────────────────────────────────────────
    //
    // Quanta storia entra nell'altezza disponibile. Non cambia il ritmo con cui
    // arrivano le righe — quello lo decide il DSP — cambia quanto spazio si dà
    // a ciascuna: è lo zoom dell'asse dei tempi, e serve a leggere la struttura
    // di una trasmissione breve, dove a piena storia una sillaba è alta due
    // pixel.
    LevelRow {
        Layout.fillWidth: true
        label: qsTr("Zoom del tempo")
        readout: root.panadapter.historySeconds > 0
                 ? qsTr("%1 s").arg((root.panadapter.historySeconds
                                     * root.panadapter.timeSpan).toFixed(1))
                 : qsTr("×%1").arg((1 / root.panadapter.timeSpan).toFixed(1))
        from: 0.1; to: 1
        value: root.panadapter.timeSpan
        onMoved: (v) => root.panadapter.timeSpan = v
    }

    // Il fermo immagine ferma lo schermo, non la radio: le righe continuano ad
    // arrivare e a essere consumate — se non lo fossero il ring si riempirebbe
    // e il DSP comincerebbe a scartare. Serve tutte le volte che qualcosa passa
    // e non si fa in tempo a leggerlo.
    DsdrButton {
        Layout.fillWidth: true
        implicitWidth: 0
        text: root.panadapter.frozen ? qsTr("Riprendi") : qsTr("Ferma l'immagine")
        checkable: true
        checked: root.panadapter.frozen
        onClicked: root.panadapter.frozen = checked
    }

    // ── Tenuta dei picchi ────────────────────────────────────────────────
    DsdrButton {
        Layout.fillWidth: true
        implicitWidth: 0
        text: qsTr("Tenuta dei picchi")
        checkable: true
        checked: root.panadapter.peakHold
        onClicked: root.panadapter.peakHold = checked
    }

    LevelRow {
        Layout.fillWidth: true
        visible: root.panadapter.peakHold
        label: qsTr("Discesa")
        readout: qsTr("%1 dB/s").arg(Math.round(root.panadapter.peakDecayDb))
        from: 1; to: 60
        value: root.panadapter.peakDecayDb
        onMoved: (v) => root.panadapter.peakDecayDb = v
    }

    // ── Scena in rilievo ─────────────────────────────────────────────────
    //
    // Tre inquadrature pronte, prima dei tre cursori che le compongono.
    // Inclinazione, rotazione e rilievo sono tre numeri che si azzeccano solo
    // insieme, e chi apre la vista in rilievo per la prima volta non ha modo di
    // sapere quali: ne prova uno per volta, trova una scena illeggibile e torna
    // al piatto. I preset sono le combinazioni che funzionano; i cursori
    // restano, per chi vuole andare oltre.
    RowLayout {
        Layout.fillWidth: true
        visible: root.showRelief
        spacing: Theme.spacingTight

        DsdrButton {
            Layout.fillWidth: true
            implicitWidth: 0
            // Quasi allo zenit: è il waterfall piatto con l'ombreggiatura, e
            // legge le frequenze come quello — ma una cresta si vede alzarsi.
            text: qsTr("Dall'alto")
            onClicked: {
                root.panadapter.tilt = 85
                root.panadapter.rotation3d = 0
                root.panadapter.reliefScale = 0.30
            }
        }

        DsdrButton {
            Layout.fillWidth: true
            implicitWidth: 0
            text: qsTr("Prospettiva")
            onClicked: {
                root.panadapter.tilt = 58
                root.panadapter.rotation3d = 0
                root.panadapter.reliefScale = 0.45
            }
        }

        DsdrButton {
            Layout.fillWidth: true
            implicitWidth: 0
            // Radente e girata: le creste si sovrappongono, e si legge
            // l'andamento nel tempo di un singolo segnale.
            text: qsTr("Di taglio")
            onClicked: {
                root.panadapter.tilt = 26
                root.panadapter.rotation3d = -18
                root.panadapter.reliefScale = 0.75
            }
        }
    }

    LevelRow {
        Layout.fillWidth: true
        visible: root.showRelief
        label: qsTr("Inclinazione")
        readout: qsTr("%1°").arg(Math.round(root.panadapter.tilt))
        from: 15; to: 85
        value: root.panadapter.tilt
        onMoved: (v) => root.panadapter.tilt = v
    }

    LevelRow {
        Layout.fillWidth: true
        visible: root.showRelief
        label: qsTr("Rotazione")
        readout: qsTr("%1°").arg(Math.round(root.panadapter.rotation3d))
        from: -45; to: 45
        value: root.panadapter.rotation3d
        onMoved: (v) => root.panadapter.rotation3d = v
    }

    LevelRow {
        Layout.fillWidth: true
        visible: root.showRelief
        label: qsTr("Rilievo")
        readout: root.panadapter.reliefScale.toFixed(2)
        from: 0.05; to: 1.2
        value: root.panadapter.reliefScale
        onMoved: (v) => root.panadapter.reliefScale = v
    }

    // In prospettiva la stessa distanza sullo schermo vale frequenze diverse a
    // seconda di quanto è lontana la riga: senza un reticolo su cui
    // appoggiarsi si vede *che* c'è un segnale, non *dove*.
    LevelRow {
        Layout.fillWidth: true
        visible: root.showRelief
        label: qsTr("Reticolo")
        readout: root.panadapter.reliefGrid < 0.01
                 ? qsTr("no")
                 : qsTr("%1%").arg(Math.round(root.panadapter.reliefGrid * 100))
        from: 0; to: 1
        value: root.panadapter.reliefGrid
        onMoved: (v) => root.panadapter.reliefGrid = v
    }

    // I cursori di livello seguono la misura finché comanda l'auto-range:
    // senza questo, dopo il primo trascinamento manuale il binding si romperebbe
    // e resterebbero fermi su un valore che non è più quello in uso.
    Binding {
        target: floorRow.sliderItem
        property: "value"
        value: root.panadapter.floorDb
        when: root.panadapter.autoRange
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: ceilingRow.sliderItem
        property: "value"
        value: root.panadapter.ceilingDb
        when: root.panadapter.autoRange
        restoreMode: Binding.RestoreNone
    }
}
