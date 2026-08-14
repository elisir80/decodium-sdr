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

    /// Apre la vista su ciò che il device consegna davvero.
    ///
    /// Con un backend che demodula a bordo la banda campionata è quella del
    /// codec — 48 kHz — ma il segnale sta tutto nella passata della radio,
    /// tre kilohertz. A piena banda si vedeva una fettina in mezzo al vuoto,
    /// e il waterfall sembrava non funzionare.
    ///
    /// La finestra è simmetrica attorno al VFO e non spostata sulla banda
    /// laterale in uso: in USB metà resta vuota, ma passando a LSB non si
    /// perde il segnale — che sarebbe il modo peggiore di essere precisi.
    function deliveredBandView(bandSpanHz, clientDemod) {
        if (clientDemod || !(bandSpanHz > 0))
            return { "start": 0, "span": 1 }

        const span = Math.max(0.005, Math.min(1, 7000 / bandSpanHz))
        return {
            "span": span,
            // La passata audio non ha un lato privilegiato: la parte visibile
            // deve restare centrata esattamente sul VFO CAT.
            "start": Math.max(0, Math.min(1 - span, 0.5 - span / 2))
        }
    }

    function fitToDeliveredBand() {
        const view = deliveredBandView(spanHz, Session.capabilities.clientDemod)
        viewSpan = view.span
        viewStart = view.start
    }

    Connections {
        target: Session
        function onConnectionChanged() { root.fitToDeliveredBand() }
        function onSampleRateChanged() { root.fitToDeliveredBand() }
        // In una radio a demodulazione interna (CAT + codec audio) il VFO è
        // anche il centro della banda consegnata. Il model del canale viene
        // aggiornato prima di questo segnale: senza il riallineamento qui,
        // `bringIntoView()` può calcolare la posizione con il vecchio centro
        // e lasciare il cursore spostato lateralmente.
        function onCenterFrequencyChanged() {
            if (!Session.capabilities.clientDemod)
                root.fitToDeliveredBand()
        }
    }

    /// Porta una frequenza dentro la finestra visibile, se non c'è già.
    ///
    /// Serve quando il ricevitore lo si sceglie dalla colonna invece che dallo
    /// spettro: con lo zoom stretto su un'altra porzione di banda, il canale
    /// scelto resta fuori campo e sembra che il clic non abbia fatto niente —
    /// la scheda si illumina e il waterfall non cambia.
    ///
    /// Se il canale è già comodamente dentro non si tocca nulla: spostare la
    /// vista quando non serve fa perdere il segno a chi stava guardando, e il
    /// margine del quindici per cento è quello che distingue «dentro» da
    /// «appiccicato al bordo».
    function bringIntoView(hz) {
        viewStart = viewStartToShow(hz, startHz, spanHz, viewStart, viewSpan)
    }

    /// Il conto che sta dietro, separato perché si possa verificare senza una
    /// radio connessa: banda campionata e centro vengono dalla sessione e sono
    /// in sola lettura — giustamente, li decide il device — quindi una prova
    /// che volesse metterli non potrebbe.
    function viewStartToShow(hz, bandStartHz, bandSpanHz, currentStart, currentSpan) {
        if (!(bandSpanHz > 0) || !isFinite(hz) || !isFinite(bandStartHz))
            return currentStart

        const fraction = (hz - bandStartHz) / bandSpanHz
        if (fraction < 0 || fraction > 1)
            return currentStart // fuori dalla banda: non c'è vista che lo mostri

        const margin = currentSpan * 0.15
        if (fraction >= currentStart + margin
            && fraction <= currentStart + currentSpan - margin)
            return currentStart

        return Math.max(0, Math.min(1 - currentSpan, fraction - currentSpan / 2))
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

    /// Frequenza del ricevitore scelto: la scrive il suo delegate.
    property real currentChannelHz: 0

    // ── Righello ─────────────────────────────────────────────────────────
    //
    // Si prende con Shift e si trascina. Misura quanto è larga una emissione o
    // quanto distano due stazioni — a occhio, contro una griglia da dieci
    // kilohertz, quella misura non si fa.
    //
    // Resta disegnato dopo il rilascio: si misura per leggere il numero, e un
    // numero che sparisce insieme al gesto va riletto mentre si tiene premuto.
    // Lo toglie il primo clic che non è una misura.
    property real rulerFromHz: 0
    property bool rulerActive: false

    /// Cambia a ogni riga di spettro misurata.
    ///
    /// Serve a far ricalcolare la lettura del mirino: `levelAt()` è una
    /// funzione, e un binding che la chiama non ha modo di sapere che i dati
    /// sotto sono cambiati — resterebbe fermo sul livello che c'era quando il
    /// puntatore si è mosso l'ultima volta.
    property int levelTick: 0

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
        //
        // In trasmissione la sorgente cambia. Non è un vezzo: in mezzo duplex
        // la radio si assorda, e la traccia resterebbe piatta proprio nei
        // secondi in cui si vorrebbe vedere se il segnale è largo, se il
        // compressore sta esagerando, se i due toni della prova ne sono
        // diventati cinque.
        feed: {
            if (Qt.application.arguments.indexOf("--no-panadapter") >= 0)
                return null
            if (Session.transmitting && Session.txSpectrum)
                return Session.txSpectrum
            return Session.spectrum
        }
        // Niente binding su `floorDb`/`ceilingDb`: con la scala automatica è
        // il C++ a scriverli, e un binding qui li riporterebbe indietro.
        Component.onCompleted: {
            floorDb = -125
            ceilingDb = -25
        }
        spectrumRatio: root.spectrumRatio
        viewStart: root.viewStart
        viewSpan: root.viewSpan
        // In trasmissione la traccia cambia colore: si guarda il proprio
        // segnale, e confonderlo con quello di qualcun altro sarebbe il modo
        // migliore di credere di aver ricevuto quel che si è appena detto.
        traceColor: Session.transmitting ? Theme.danger : Theme.spectrumTrace
        fillColor: Session.transmitting ? Qt.rgba(Theme.danger.r, Theme.danger.g,
                                                  Theme.danger.b, 0.25)
                                        : Theme.spectrumFill
        peakColor: Theme.spectrumPeak
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

    // ── Notch del canale attivo ──────────────────────────────────────────
    //
    // Una tacca dove il notch morde: senza, l'unico modo di sapere dove sono è
    // leggerne l'elenco, e chi guarda lo spettro vede un buco senza causa.
    //
    // Un delegate per canale che disegna i propri, invece di chiedere al
    // modello la riga corrente: è il modo di restare reattivi ai cambiamenti
    // senza tenerne una copia da riallineare a mano.
    Repeater {
        model: Session.channels

        delegate: Item {
            required property int index
            required property var notches

            anchors.fill: parent
            visible: Session.channels.currentIndex === index
            z: 4

            Repeater {
                model: parent.notches

                delegate: Item {
                    required property var modelData

                    readonly property real centreX: root.xForFrequency(modelData.frequencyHz)
                    readonly property real halfWidth:
                        Math.max(2, modelData.widthHz / 2 / root.visibleSpanHz * root.width)

                    visible: centreX + halfWidth > 0 && centreX - halfWidth < root.width

                    Rectangle {
                        x: parent.centreX - parent.halfWidth
                        width: parent.halfWidth * 2
                        y: 0
                        height: root.height * root.spectrumRatio
                        color: Theme.danger
                        opacity: 0.18
                    }

                    Rectangle {
                        x: parent.centreX - 0.5
                        width: 1
                        y: 0
                        height: root.height * root.spectrumRatio
                        color: Theme.danger
                        opacity: 0.7
                    }
                }
            }
        }
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

            // Il canale appena scelto si fa vedere. L'aggancio sta qui e non
            // su `Session.channels.currentIndex` perché è il delegate a sapere
            // su che frequenza sta il suo canale: chiederla al modello da fuori
            // vorrebbe dire tenerne una copia.
            onVfoSelectedChanged: if (vfoSelected) root.bringIntoView(frequencyHz)

            // E lo fa anche se, mentre è scelto, lo si sposta da altrove — dal
            // riquadro della frequenza, o da un comando CAT della radio.
            onVfoFrequencyChanged: if (vfoSelected) root.bringIntoView(frequencyHz)

            xForFrequency: root.xForFrequency
            frequencyAt: root.frequencyAt

            onSelectRequested: Session.channels.currentIndex = index
            onTuneRequested: (hz) => Session.setChannelFrequency(index, hz)
            onRemoveRequested: {
                if (Session.channels.count > 1)
                    Session.removeChannel(index)
            }
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

        // Una targa per ogni ricevitore aperto, non solo per quello scelto:
        // con più ricevitori si sta seguendo più di una cosa, ed è lì che
        // serve vedere frequenza e livello di tutti insieme.
        //
        // Niente anchors: la targa si sposta, e un ancoraggio la riporterebbe
        // al suo posto al primo rilascio. La posizione di partenza è una
        // scala — la prima in alto, le altre sotto — perché nascere tutte
        // nello stesso punto vorrebbe dire trovarne una sola e doverle
        // separare a mano ogni volta che se ne apre una.
        delegate: VfoPlate {
            id: plate

            movable: true
            visible: Session.connected

            // Quella scelta sta sopra le altre: è quella che si guarda, e se
            // finisce sotto si perde proprio mentre la si cerca.
            z: Session.channels.currentIndex === index ? 6 : 5

            onSelectRequested: Session.channels.currentIndex = index

            // La frequenza del ricevitore scelto, per il mirino: è l'unico
            // punto dell'albero in cui una riga del modello è già stata
            // srotolata in proprietà, e chiederla di nuovo al modello vorrebbe
            // dire tenerne una copia da riallineare a mano.
            Binding {
                target: root
                property: "currentChannelHz"
                value: plate.frequencyHz
                when: Session.channels.currentIndex === plate.index
                restoreMode: Binding.RestoreNone
            }

            Component.onCompleted: {
                x = Math.max(0, (root.width - width) / 2)
                y = Theme.spacing + index * (implicitHeight + Theme.spacingTight)
                keepInside()
            }
        }
    }

    // ── Interazione: click-to-tune, pan, zoom ────────────────────────────
    MouseArea {
        id: spectrumMouse

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: root.viewSpan < 1 ? Qt.OpenHandCursor : Qt.CrossCursor
        // Serve al mirino: è questo l'item che sa dove sta il puntatore sullo
        // spettro, ed è già quello che possiede i gesti.
        hoverEnabled: true
        z: -1

        onClicked: (mouse) => {
            if (!Session.connected || panned)
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

        // Tasto destro: un notch dove si vede il disturbo (SPEC-003 §5). È il
        // gesto naturale — si punta il fischio sullo spettro invece di
        // leggerne la frequenza e riscriverla altrove.
        // Trascinamento = scorrimento della banda visibile. Ha senso solo
        // sotto zoom: a piena banda non c'è nulla oltre i bordi.
        property real dragAnchorX: 0
        property real dragAnchorStart: 0

        /// Se il gesto in corso ha spostato lo spettro.
        ///
        /// `onClicked` di un MouseArea scatta anche dopo un trascinamento, e
        /// senza questa distinzione ogni pan finiva con una sintonia: si
        /// spostava la vista per andare a vedere un segnale e il ricevitore si
        /// piazzava dove capitava di lasciare il dito. Quattro punti sono la
        /// soglia oltre la quale un gesto smette di essere un clic tremolante.
        property bool panned: false

        /// Se il gesto in corso sta misurando invece di spostare la vista.
        property bool measuring: false

        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                if (Session.connected && Session.channels.currentIndex >= 0) {
                    Session.addChannelNotch(Session.channels.currentIndex,
                                            Math.round(root.frequencyAt(mouse.x)), 150)
                }
                return
            }

            // Tasto centrale: un marcatore dove si punta, o via quello che c'è
            // già lì. Lo stesso gesto nei due versi, così chi ne mette uno per
            // sbaglio lo toglie ripetendo quello che ha appena fatto.
            if (mouse.button === Qt.MiddleButton) {
                if (Session.connected)
                    Markers.toggle(root.frequencyAt(mouse.x))
                return
            }

            // Shift: si sta misurando, non si sta spostando la vista.
            measuring = (mouse.modifiers & Qt.ShiftModifier) !== 0
            if (measuring) {
                root.rulerFromHz = root.frequencyAt(mouse.x)
                root.rulerActive = true
                panned = true      // niente sintonia al rilascio: era una misura
                return
            }

            // Un clic normale chiude la misura precedente: il righello resta
            // disegnato dopo il rilascio — si misura per leggere il numero, e
            // un numero che sparisce col gesto va riletto tenendo premuto — ma
            // non deve restare lì per sempre.
            root.rulerActive = false

            dragAnchorX = mouse.x
            dragAnchorStart = root.viewStart
            panned = false
        }

        onPositionChanged: (mouse) => {
            if (!pressed || measuring)
                return
            if (Math.abs(mouse.x - dragAnchorX) > 4)
                panned = true
            if (root.viewSpan >= 1)
                return
            const delta = (dragAnchorX - mouse.x) / Math.max(root.width, 1) * root.viewSpan
            root.viewStart = Math.max(0, Math.min(1 - root.viewSpan, dragAnchorStart + delta))
        }

        onReleased: measuring = false

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

    // ── Marcatori ────────────────────────────────────────────────────────
    //
    // Non sono canali e non sono memorie: sono punti che si tengono d'occhio.
    // Si mettono e si tolgono con il tasto centrale, che era l'unico dei tre
    // libero — e che nessun altro gesto dello spettro usa, quindi metterne uno
    // per sbaglio non fa altro.
    //
    // Sopra la griglia e sotto le targhe: devono vedersi, ma non coprire i
    // comandi.
    Repeater {
        model: Markers.entries

        delegate: Item {
            required property var modelData

            readonly property real markerX: root.xForFrequency(modelData.frequency)

            anchors.fill: parent
            visible: Session.connected && markerX >= -20 && markerX <= root.width + 20
            z: 3

            Rectangle {
                x: Math.round(parent.markerX)
                width: 1
                y: 0
                height: parent.height
                color: Theme.spectrumPeak
                opacity: 0.5
            }

            // L'etichetta in basso: in alto ci sono le targhe, e un marcatore
            // messo sotto un ricevitore aperto sparirebbe proprio lì.
            Rectangle {
                x: Math.round(parent.markerX) - width / 2
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                width: markerLabel.implicitWidth + 8
                height: markerLabel.implicitHeight + 4
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: 1
                border.color: Theme.spectrumPeak

                Text {
                    id: markerLabel
                    anchors.centerIn: parent
                    text: Markers.label(parent.parent.modelData)
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.spectrumPeak
                }
            }
        }
    }

    // ── Mirino ───────────────────────────────────────────────────────────
    //
    // Legge frequenza e livello sotto il puntatore. Non riceve gesti: quelli
    // restano del MouseArea sotto, e un item che li prendesse romperebbe il
    // click-to-tune — è già successo con i comandi del waterfall.
    Connections {
        target: panadapter
        function onMeasuredLevelsChanged() { root.levelTick++ }
    }

    SpectrumCursor {
        anchors.fill: parent
        z: 8

        // Il puntatore lo dà il MouseArea che possiede già i gesti dello
        // spettro, e non un HoverHandler a parte: sopra le targhe è il loro
        // MouseArea a prendere l'evento, e il mirino si spegne da sé — che è
        // giusto, perché lì sotto non si sta guardando lo spettro.
        readonly property real pointerX: spectrumMouse.containsMouse
                                         ? spectrumMouse.mouseX : -1

        cursorX: pointerX
        cursorY: spectrumMouse.mouseY
        cursorHz: root.frequencyAt(pointerX)
        cursorLevelDb: {
            root.levelTick    // dipendenza voluta: vedi `levelTick`
            return panadapter.levelAt(root.viewStart
                                      + (pointerX / Math.max(root.width, 1)) * root.viewSpan)
        }
        referenceHz: root.currentChannelHz
        floorDb: panadapter.floorDb
        ceilingDb: panadapter.ceilingDb
        spectrumRatio: root.spectrumRatio
        rulerActive: root.rulerActive
        rulerFromHz: root.rulerFromHz
        rulerX: root.xForFrequency(root.rulerFromHz)
    }

    // ── Spia di saturazione ──────────────────────────────────────────────
    //
    // Sul pan e non solo nel pannello: quando l'ingresso satura è lo spettro a
    // mentire per primo — il fondo si alza, i deboli spariscono — e chi guarda
    // qui deve sapere che non sta guardando la banda, ma i prodotti del
    // proprio convertitore.
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing
        anchors.leftMargin: 42 + 90
        width: overloadLabel.implicitWidth + 2 * Theme.spacingLoose
        height: 22
        radius: Theme.radiusSmall
        color: Theme.danger
        visible: Session.overloaded
        z: 7

        Text {
            id: overloadLabel
            anchors.centerIn: parent
            text: qsTr("OVL  %1 dBFS").arg(Session.peakDbfs.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.bold: true
            font.family: Theme.monoFamily
            color: Theme.background
        }
    }

    // ── Avviso di riascolto ──────────────────────────────────────────────
    //
    // In riascolto non è solo l'audio a essere in ritardo: traccia, waterfall
    // e S-meter raccontano tutti lo stesso istante passato. Chi non lo sapesse
    // scambierebbe una banda di trenta secondi fa per la propagazione di
    // adesso — ed è il tipo di errore che si porta dietro un log sbagliato.
    // ── In trasmissione ──────────────────────────────────────────────────
    //
    // La cornice rossa dice che quello che si sta guardando è il **proprio**
    // segnale e non la banda: senza, un panadattatore con una riga in mezzo
    // sembra una banda con una stazione sopra.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 2
        border.color: Theme.danger
        opacity: 0.8
        visible: Session.transmitting
        z: 6
        enabled: false
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing
        width: txLabel.implicitWidth + 2 * Theme.spacingLoose
        height: 24
        radius: Theme.radiusSmall
        color: Theme.danger
        visible: Session.transmitting
        z: 7

        Text {
            id: txLabel
            anchors.centerIn: parent
            text: Session.tuning
                  ? qsTr("TX · PROVA · %1 s").arg(Session.tuneSecondsLeft)
                  : qsTr("TX · %1 %").arg(Math.round(Session.txLevel * 100))
            font.pixelSize: Theme.fontSmall
            font.bold: true
            font.family: Theme.monoFamily
            color: Theme.background
        }

        // Pulsa mentre si trasmette: una cornice ferma si smette di vedere
        // dopo dieci secondi, e restare in trasmissione senza accorgersene è
        // il modo in cui si scalda un finale.
        SequentialAnimation on opacity {
            running: Session.transmitting
            loops: Animation.Infinite
            NumberAnimation { to: 0.45; duration: 700; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 1.0;  duration: 700; easing.type: Easing.InOutQuad }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 2
        border.color: Theme.warning
        opacity: 0.7
        visible: Session.replaying
        z: 6
        // La cornice segnala, non intercetta: sotto ci si deve poter
        // continuare a sintonizzare.
        enabled: false
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacing
        width: replayLabel.implicitWidth + 2 * Theme.spacingLoose
        height: 24
        radius: Theme.radiusSmall
        color: Theme.warning
        visible: Session.replaying
        z: 7

        Text {
            id: replayLabel
            anchors.centerIn: parent
            text: qsTr("◀◀ %1 s fa · torna in diretta")
                  .arg(Math.round(Session.replayDelaySeconds))
            font.pixelSize: Theme.fontSmall
            font.bold: true
            color: Theme.background
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Session.returnToLive()
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
