// SPDX-License-Identifier: GPL-3.0-or-later
// Targa del canale attivo, sopra lo spettro.
//
// Frequenza, livello, modo, filtro e AGC stavano in fondo alla colonna
// laterale: lontani dal punto in cui si guarda mentre si sintonizza. Chi opera
// tiene gli occhi sul segnale, non sul bordo destro della finestra.
//
// È il delegate del canale corrente, non una copia dei suoi dati: si aggiorna
// da sé quando il modello cambia, senza che nessuno debba ricordarsi di
// tenerlo allineato.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    required property int index
    required property color channelColor
    required property string label
    required property real frequencyHz
    required property int mode
    required property string modeName
    required property int filterLowHz
    required property int filterHighHz
    required property real signalDb
    required property real noiseFloorDb
    required property int agcMode
    required property bool muted
    required property real volume

    /// I filtri di disturbo del canale: interruttore qui, regolazione nella
    /// colonna. `notches` sono quelli piazzati a mano sullo spettro.
    required property bool nrEnabled
    required property real nrStrength
    required property bool anfEnabled
    required property var notches

    /// Larghezza del filtro: è il numero che si legge, non i due estremi.
    readonly property int filterWidthHz: Math.max(0, filterHighHz - filterLowHz)

    /// Le larghezze proposte per il modo in uso.
    readonly property var filterChoices: FilterPresets.widthsFor(mode)

    /// La banda in cui sta questo ricevitore. Fuori dalle bande amatoriali —
    /// onde medie, aeronautica, utility — non c'è un nome da mostrare, e il
    /// comando resta comunque il modo per andarsene altrove.
    readonly property string bandName: {
        const band = BandPlan.bandAt(frequencyHz)
        return band ? band.name : qsTr("BANDA")
    }

    /// Chi prende una targa sta lavorando su quel ricevitore: prenderla lo
    /// sceglie, come prendere il flag sullo spettro.
    signal selectRequested()

    /// La targa si prende per la maniglia e si mette dove si vuole.
    ///
    /// Con un ricevitore solo, in cima al centro andava bene. Con quattro,
    /// una sopra l'altra, le targhe coprono la parte di spettro che si sta
    /// guardando — e quale sia la parte che dà fastidio lo sa solo chi ascolta.
    property bool movable: false

    /// Riporta la targa dentro il riquadro che la ospita.
    ///
    /// Serve quando la finestra si stringe: una targa lasciata a destra
    /// finirebbe fuori dal bordo e non ci sarebbe più modo di riprenderla, se
    /// non allargando di nuovo la finestra.
    function keepInside() {
        if (!parent)
            return
        x = Math.max(0, Math.min(Math.max(0, parent.width - width), x))
        y = Math.max(0, Math.min(Math.max(0, parent.height - height), y))
    }

    Connections {
        target: root.parent
        enabled: root.movable && root.parent !== null
        ignoreUnknownSignals: true

        function onWidthChanged() { root.keepInside() }
        function onHeightChanged() { root.keepInside() }
    }

    /// Nome dell'AGC. Il modello espone il numero del modo, non la sua
    /// etichetta; la tabella dei nomi la tiene la sessione, tradotta.
    readonly property string agcName: {
        const names = Session.agcModeNames()
        return (agcMode >= 0 && agcMode < names.length) ? names[agcMode] : ""
    }

    implicitWidth: row.implicitWidth + 2 * Theme.spacingLoose
    implicitHeight: row.implicitHeight + 2 * Theme.spacing

    radius: Theme.radius
    // Opaca, non velata: sotto scorre lo spettro. La lezione è già stata
    // imparata una volta con i comandi del waterfall.
    color: Theme.surface
    border.width: 1
    border.color: root.channelColor

    // La targa ferma i gesti che le arrivano sopra.
    //
    // È opaca, e sotto scorre lo spettro: senza questo, un clic su una sua
    // parte vuota attraversa e finisce nel click-to-tune, che sposta il
    // canale. Si preme un pezzo di targa per leggere meglio un numero e la
    // radio cambia frequenza — e non c'è modo di collegare le due cose. Anche
    // la rotellina si ferma qui: sotto c'è lo zoom dello spettro, e girarla
    // sopra una targa non deve muovere il mondo dietro.
    //
    // Sta dichiarata **prima** della riga dei comandi, quindi le resta sotto:
    // la maniglia e i comandi ricevono i gesti per primi. Un TapHandler sul
    // riquadro faceva la stessa cosa in apparenza e prendeva il gesto prima
    // che il trascinamento potesse cominciare — la targa non si spostava più.
    MouseArea {
        id: plateArea

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: overHandle || drag.active
                     ? (drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                     : Qt.ArrowCursor

        /// Se il puntatore sta sulla maniglia, cioè nella fascia a sinistra.
        ///
        /// Il bordo si ricava sommando dove comincia la riga dei comandi e
        /// dove finisce la maniglia dentro di essa: la riga è centrata nella
        /// targa, quindi la maniglia non parte dal bordo del riquadro e
        /// misurare dal bordo darebbe una zona sfalsata di qualche punto —
        /// abbastanza da mancarla proprio dove si prova a prenderla.
        readonly property real handleEdge: row.x + handleZone.x + handleZone.width
        readonly property bool overHandle: root.movable && mouseX <= handleEdge

        drag.axis: Drag.XAndYAxis
        drag.minimumX: 0
        drag.maximumX: Math.max(0, (root.parent ? root.parent.width : 0) - root.width)
        drag.minimumY: 0
        drag.maximumY: Math.max(0, (root.parent ? root.parent.height : 0) - root.height)

        // Il bersaglio del trascinamento si decide al momento della presa: la
        // targa si sposta solo se il gesto nasce sulla maniglia, e altrove il
        // gesto resta un clic.
        //
        // Un solo MouseArea per tutta la targa, e non due — uno per la
        // maniglia e uno per il resto — perché due si contendono la presa: il
        // secondo prendeva il gesto e il primo non trascinava più, e dall'alto
        // sembrava che la maniglia non facesse niente.
        onPressed: (mouse) => {
            drag.target = overHandle ? root : null
            root.selectRequested()
        }

        onReleased: drag.target = null

        // La rotellina si ferma qui: sotto c'è lo zoom dello spettro, e
        // girarla sopra una targa non deve muovere il mondo dietro.
        onWheel: (wheel) => wheel.accepted = true
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingLoose

        // ── Maniglia ─────────────────────────────────────────────────────
        //
        // Il trascinamento sta su una maniglia e non su tutta la targa: qui
        // dentro ci sono un cursore del volume e una frequenza che si edita a
        // cifre, e un gesto che comincia su quelli deve restare loro.
        Item {
            id: handleZone

            visible: root.movable
            // Il bersaglio è più largo dei puntini che si vedono: otto punti
            // di larghezza sono una cosa che si manca, e mancarla qui non
            // significa non fare niente — significa che il gesto arriva allo
            // spettro sotto, che lo interpreta come una sintonia.
            implicitWidth: 22
            implicitHeight: 26

            Column {
                anchors.centerIn: parent
                spacing: 3

                Repeater {
                    model: 4

                    delegate: Row {
                        spacing: 3

                        Repeater {
                            model: 2

                            delegate: Rectangle {
                                width: 2
                                height: 2
                                radius: 1
                                color: plateArea.drag.active ? Theme.accent
                                     : plateArea.overHandle ? Theme.textPrimary
                                     : Theme.textDisabled
                            }
                        }
                    }
                }
            }

        }

        // ── Etichetta del canale ─────────────────────────────────────────
        Rectangle {
            implicitWidth: tag.implicitWidth + 2 * Theme.spacing
            implicitHeight: tag.implicitHeight + 4
            radius: Theme.radiusSmall
            color: root.channelColor

            Text {
                id: tag
                anchors.centerIn: parent
                text: root.label
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.letterSpacing: 1
                // Il testo sta sopra il colore del canale, che è chiaro: qui
                // il nero è il contrasto, non una tinta del tema.
                color: Theme.surfaceSunken
            }
        }

        // ── Frequenza ────────────────────────────────────────────────────
        FrequencyDisplay {
            frequencyHz: root.frequencyHz
            minimumHz: Session.capabilities.minFrequency
            maximumHz: Session.capabilities.maxFrequency > 0
                       ? Session.capabilities.maxFrequency : 30000000
            editable: Session.connected
            digitSize: 26
            onTuneRequested: (hz) => Session.setChannelFrequency(root.index, hz)
        }

        // ── Livello ──────────────────────────────────────────────────────
        ColumnLayout {
            spacing: 1

            SignalMeter {
                Layout.preferredWidth: 132
                Layout.preferredHeight: 18
                levelDb: root.signalDb
                noiseFloorDb: root.noiseFloorDb
            }
        }

        // ── Passo di sintonia ────────────────────────────────────────────
        //
        // Era cablato nel codice della rotellina, e l'unico modo di sapere di
        // quanto ci si stesse muovendo era muoversi e guardare.
        RowLayout {
            spacing: 2

            Text {
                text: qsTr("STEP")
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
            }

            DsdrButton {
                text: "◀"
                implicitWidth: 22
                implicitHeight: 22
                enabled: Tuning.stepHz > Tuning.steps[0]
                onClicked: Tuning.shift(-1)
            }

            Text {
                text: Tuning.label(Tuning.stepHz)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.accent
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 30
            }

            DsdrButton {
                text: "▶"
                implicitWidth: 22
                implicitHeight: 22
                enabled: Tuning.stepHz < Tuning.steps[Tuning.steps.length - 1]
                onClicked: Tuning.shift(1)
            }
        }

        // ── Silenzia ─────────────────────────────────────────────────────
        //
        // Nella targa e non solo nel pannello: si silenzia un canale mentre si
        // ascolta, cioè guardando lo spettro, non la colonna laterale.
        DsdrButton {
            text: root.muted ? qsTr("MUTO") : qsTr("AUDIO")
            implicitWidth: 62
            implicitHeight: 24
            checkable: true
            checked: root.muted
            danger: root.muted
            onToggled: Session.setChannelMuted(root.index, checked)
        }

        // Il volume accanto al silenzia, non in fondo alla colonna: sono la
        // stessa manopola divisa in due, e chi abbassa spesso finisce per
        // volere l'una o l'altra. Da muto il cursore si spegne — muoverlo
        // senza sentire niente è il modo più efficace di credere che il guasto
        // sia altrove.
        DsdrSlider {
            implicitWidth: 84
            enabled: !root.muted
            from: 0; to: 1
            value: root.volume
            accentColor: root.channelColor
            onMoved: Session.setChannelVolume(root.index, value)
        }

        // ── Dove si è: banda e modo ──────────────────────────────────────
        //
        // Vicini perché si scelgono insieme. Si cambia banda e la prima cosa
        // dopo è il modo, perché sotto i dieci megahertz si sta in LSB e sopra
        // in USB, e su una banda nuova quello di prima è quasi sempre quello
        // sbagliato. Tenerli ai due capi della targa voleva dire attraversarla
        // due volte per un gesto solo.
        //
        // Il bandstack è condiviso con il pannello di sintonia: la banda
        // ricorda dove la si era lasciata comunque la si sia scelta.
        PlateChip {
            text: root.bandName
            enabled: Session.connected
            model: Memories.reachableBands.map(band => band.name)
            currentIndex: {
                const bands = Memories.reachableBands
                for (let i = 0; i < bands.length; ++i) {
                    if (bands[i].name === root.bandName)
                        return i
                }
                return -1
            }
            onSelected: (index) => {
                Session.channels.currentIndex = root.index
                Memories.selectBand(Memories.reachableBands[index])
            }
        }

        // A menu e non a pulsantiera come nella colonna: qui lo spazio è una
        // riga sopra il waterfall, e undici modi in fila la renderebbero più
        // larga della finestra. Il menu costa un gesto in più e non copre il
        // segnale quando è chiuso, che è il compromesso giusto per una targa
        // che sta sopra ciò che si guarda.
        PlateChip {
            text: root.modeName
            enabled: Session.connected
            model: Session.modeNames()
            currentIndex: root.mode
            onSelected: (index) => Session.setChannelMode(root.index, index)
        }

        PlateChip {
            text: FilterPresets.label(root.filterWidthHz)
            enabled: Session.connected
            model: root.filterChoices.map(FilterPresets.label)
            // Il filtro in uso può non essere fra quelli proposti — lo si
            // regola anche a mano dal pannello — e allora nessuna voce è
            // segnata: meglio di una spuntata a caso.
            currentIndex: root.filterChoices.indexOf(root.filterWidthHz)
            onSelected: (index) => FilterPresets.applyWidth(
                            root.index, root.mode, root.filterChoices[index],
                            root.filterLowHz, root.filterHighHz)
        }

        PlateChip {
            text: qsTr("AGC %1").arg(root.agcName)
            enabled: Session.connected
            model: Session.agcModeNames()
            currentIndex: root.agcMode
            onSelected: (index) => Session.setChannelAgcMode(root.index, index)
        }

        // ── Filtri di disturbo ───────────────────────────────────────────
        //
        // In un blocco solo, con la sua cornice. Sono cinque interruttori che
        // in fila con gli altri comandi si leggevano come cinque comandi
        // scollegati: quando il rumore peggiora si cerca *questa* zona della
        // targa, non un interruttore per volta.
        //
        // Interruttori e non un menu: si accendono guardando lo spettro — si
        // alza il rumore, si preme, si sente se è servito — e un gesto in più
        // per aprire un elenco è un gesto durante il quale non si ascolta.
        // Nella colonna ci sono già con i loro cursori: la regolazione fine
        // resta di là, dove c'è spazio per spiegarla.
        //
        // Nessuno dei cinque è gratis, ed è per questo che nascono spenti.
        Rectangle {
            Layout.alignment: Qt.AlignVCenter

            implicitWidth: disturbances.implicitWidth + 2 * Theme.spacing
            implicitHeight: disturbances.implicitHeight + 2 * Theme.spacingTight
            radius: Theme.radiusSmall
            // Incassato e con il bordo marcato: un riquadro appena accennato,
            // su una riga già piena di riquadri, non raggruppa niente — si
            // legge come un sesto comando invece che come la cornice degli
            // altri cinque.
            color: Theme.background
            border.width: 1
            border.color: Theme.borderStrong

            RowLayout {
                id: disturbances

                anchors.centerIn: parent
                spacing: Theme.spacingTight

                // Riduzione di rumore del canale.
                PlateToggle {
                    text: qsTr("NR")
                    enabled: Session.connected
                    checked: root.nrEnabled
                    onToggled: Session.setChannelNoiseReduction(root.index, !root.nrEnabled,
                                                                root.nrStrength)
                }

                // Riduzione neurale: è di tutta la catena audio, non di questo
                // canale, e compare solo dove il motore c'è davvero — un
                // interruttore che non può fare niente è peggio di un
                // interruttore assente (CONSTITUTION §7).
                PlateToggle {
                    visible: Session.neuralAvailable
                    text: qsTr("DNR")
                    enabled: Session.connected
                    checked: Session.neuralEnabled
                    // Tinta diversa dagli altri: costa un thread e qualche
                    // millisecondo di ritardo, e chi lo tiene acceso deve
                    // ricordarselo.
                    activeColor: Theme.spectrumPeak
                    onToggled: Session.setNeuralNr(!Session.neuralEnabled)
                }

                // Soppressore di impulsi: di catena, perché un impulso arriva
                // su tutta la banda campionata e va tolto prima che i canali
                // decimino.
                PlateToggle {
                    text: qsTr("NB")
                    enabled: Session.connected
                    checked: Session.noiseBlanker
                    onToggled: Session.setNoiseBlanker(!Session.noiseBlanker,
                                                       Session.noiseBlankerThreshold)
                }

                // Notch automatico: toglie i fischi, e su una voce si sente.
                PlateToggle {
                    text: qsTr("ANF")
                    enabled: Session.connected
                    checked: root.anfEnabled
                    onToggled: Session.setChannelAutoNotch(root.index, !root.anfEnabled)
                }

                // I notch manuali si piazzano con il tasto destro sullo
                // spettro. Qui si contano e si tolgono tutti insieme:
                // dimenticarne uno sopra un segnale che si voleva ascoltare è
                // il modo più efficace di credere che la radio sia sorda
                // proprio lì.
                PlateToggle {
                    visible: root.notches.length > 0
                    text: qsTr("NOTCH %1").arg(root.notches.length)
                    enabled: Session.connected
                    checked: true
                    activeColor: Theme.danger
                    onToggled: Session.clearChannelNotches(root.index)
                }
            }
        }
    }
}
