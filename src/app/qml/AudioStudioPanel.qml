// SPDX-License-Identifier: GPL-3.0-or-later
// Lo studio dell'audio: cosa esce davvero dagli altoparlanti.
//
// Il panadattatore mostra la banda, cioè quello che arriva dall'antenna. Fra
// quello e quello che si sente ci sono i filtri del canale, l'AGC, il notch,
// la riduzione di rumore e lo stadio neurale — cinque cose che si regolano al
// buio, perché il loro effetto si giudica a orecchio e l'orecchio si abitua in
// pochi secondi.
//
// Qui si vede. Il filtro taglia dove si crede? Il notch ha preso il fischio o
// gli è passato accanto? La riduzione di rumore sta mangiando le consonanti
// insieme al fruscio? Sono domande a cui prima si rispondeva riascoltando lo
// stesso pezzo con e senza, cioè con la memoria — che è lo strumento di misura
// peggiore che ci sia.
//
// Il tap sta sul mix finale, non prima: quello che si guarda è esattamente
// quello che si sente.
import QtCore
import QtQml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("STUDIO AUDIO")
    draggable: true
    collapsed: true
    // In colonna è una striscia larga trecento punti: uno spettro alto un
    // dito, una banda di tre kilohertz in venti pixel, una forma d'onda che è
    // una linea. Staccato si prende lo spazio che gli serve, e su un secondo
    // schermo diventa lo strumento che si guarda mentre si fa altro.
    detachable: true

    Settings {
        id: prefs
        category: "audio-studio"
        property real viewSpan: 0.1
        property int paletteIndex: 5
        property bool peakHold: true
        property real spanMs: 20
        property bool triggered: true
    }

    // ── Lo spettro dell'audio ────────────────────────────────────────────
    //
    // Lo stesso componente del panadattatore, su un altro feed. Non è
    // un'economia: è la stessa cosa: uno spettro con la sua storia sotto, e
    // averne due che si comportano in modo diverso vorrebbe dire imparare due
    // strumenti per leggere la stessa grandezza.
    // In colonna un'altezza fissa, che è tutto quello che la striscia
    // concede; staccato prende quello che avanza, che è il motivo per cui lo
    // si stacca. Due terzi allo spettro e un terzo all'oscilloscopio: la
    // forma d'onda si legge anche bassa, lo spettro no.
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: root.detached
        Layout.preferredHeight: root.detached ? -1 : 150
        Layout.minimumHeight: 120

        PanadapterView {
            id: audioView

            anchors.fill: parent
            feed: Session.audioSpectrum

            // Metà spettro alla traccia e metà alla storia: su una passata di
            // tre kilohertz la forma conta quanto il suo andamento nel tempo —
            // un fischio che va e viene si riconosce solo dalla riga che
            // lascia dietro di sé.
            spectrumRatio: 0.5

            // La scala automatica sull'audio è quasi obbligatoria: il livello
            // dipende dal volume e dall'AGC, che si muovono di continuo, e una
            // scala fissa lascerebbe l'immagine nera o bruciata quasi sempre.
            autoRange: true
            peakHold: prefs.peakHold
            paletteIndex: prefs.paletteIndex

            traceColor: Theme.spectrumTrace
            fillColor: Theme.spectrumFill
            peakColor: Theme.spectrumPeak
            backgroundColor: Theme.spectrumBackground

            // Si parte sui primi due chilohertz e mezzo, dove sta la voce.
            // Tutta la banda fino a ventiquattro chilohertz è quasi sempre
            // vuota, e mostrarla vuol dire schiacciare in un ventesimo dello
            // spazio l'unica parte che si guarda.
            viewStart: 0
            viewSpan: prefs.viewSpan

            Component.onCompleted: {
                floorDb = -110
                ceilingDb = -20
            }
        }

        // La griglia in kilohertz, disegnata sopra: senza, si vede che c'è un
        // tono ma non a che nota — ed è la sola cosa che questo pannello serve
        // a dire con un numero invece che con un aggettivo.
        Repeater {
            model: root.gridTicks

            delegate: Item {
                required property var modelData

                readonly property real fraction:
                    (modelData - root.spanStartHz) / Math.max(1, root.spanWidthHz)

                anchors.fill: parent
                visible: fraction >= 0 && fraction <= 1

                Rectangle {
                    x: Math.round(parent.fraction * parent.width)
                    width: 1
                    height: parent.height
                    color: Theme.border
                    opacity: 0.5
                }

                Text {
                    x: Math.round(parent.fraction * parent.width) + 3
                    y: 2
                    text: (modelData / 1000).toFixed(1)
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textDisabled
                }
            }
        }

        // I bordi del filtro: dove *dovrebbe* tagliare.
        Repeater {
            model: root.filterEdges

            delegate: Item {
                required property var modelData

                readonly property real fraction:
                    (modelData - root.spanStartHz) / Math.max(1, root.spanWidthHz)

                anchors.fill: parent
                visible: fraction >= 0 && fraction <= 1

                Rectangle {
                    x: Math.round(parent.fraction * parent.width)
                    width: 1
                    height: parent.height
                    color: Theme.spectrumPeak
                    opacity: 0.8
                }

                Text {
                    x: Math.min(parent.width - width - 2,
                                Math.round(parent.fraction * parent.width) + 3)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    text: qsTr("%1 Hz").arg(Math.round(modelData))
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.spectrumPeak
                }
            }
        }
    }

    // ── Dov'è il filtro del canale ───────────────────────────────────────
    //
    // Il pannello esiste per rispondere a «il filtro taglia dove credo?», e
    // finora mostrava solo dove taglia *davvero*: mancava il termine di
    // paragone. Questi sono i due bordi che il filtro dichiara, disegnati
    // sopra lo spettro — se la traccia scende prima o dopo, si vede a colpo
    // d'occhio e non c'è niente da calcolare.
    //
    // I due bordi si leggono dal canale scelto invece di tenerne una copia:
    // è l'unico modo di restare allineati a un valore che l'operatore cambia
    // da tre posti diversi.
    property int filterLowHz: 0
    property int filterHighHz: 0

    // Un Instantiator e non un Repeater: il delegate di un Repeater è un Item,
    // e un Item dentro un ColumnLayout è un figlio del layout — invisibile o
    // no, il layout gli faceva posto, e in cima al pannello restava una fascia
    // vuota alta quanto i canali aperti. Qui il delegate è un QtObject: non è
    // un oggetto visuale, non entra nel layout, e serve solo a srotolare la
    // riga del canale in proprietà leggibili.
    Instantiator {
        model: Session.channels

        delegate: QtObject {
            id: channelRow

            required property int index
            required property int filterLowHz
            required property int filterHighHz

            // Per id e non per `parent`: un Binding non ha un genitore
            // visuale, e `parent` dentro di lui è nullo — i due bordi
            // restavano a zero senza che niente lo dicesse.
            //
            // E in una lista, perché un QtObject non ha una proprietà
            // predefinita in cui infilare figli.
            property list<QtObject> wiring: [
                Binding {
                    target: root
                    property: "filterLowHz"
                    value: channelRow.filterLowHz
                    when: Session.channels.currentIndex === channelRow.index
                    restoreMode: Binding.RestoreNone
                },
    
                Binding {
                    target: root
                    property: "filterHighHz"
                    value: channelRow.filterHighHz
                    when: Session.channels.currentIndex === channelRow.index
                    restoreMode: Binding.RestoreNone
                }
            ]
        }
    }

    /// I due bordi in frequenza audio.
    ///
    /// In banda laterale lo scostamento dalla portante *è* la frequenza audio,
    /// quindi i due numeri passano tali e quali. In banda laterale inferiore
    /// sono negativi e l'audio resta positivo: si prende il modulo. Nei modi
    /// simmetrici — AM, FM — il bordo inferiore è sotto lo zero e l'audio
    /// comincia da zero, e allora il solo bordo che significhi qualcosa è
    /// quello superiore.
    readonly property var filterEdges: {
        const low = Math.abs(root.filterLowHz)
        const high = Math.abs(root.filterHighHz)
        const from = Math.min(low, high)
        const to = Math.max(low, high)
        if (!(to > 0))
            return []
        // Bordo inferiore sotto lo zero: nei modi simmetrici non c'è.
        return root.filterLowHz < 0 && root.filterHighHz > 0 ? [to] : [from, to]
    }

    /// La porzione di audio in vista, in hertz. La banda analizzata va da zero
    /// alla Nyquist dell'audio interno.
    readonly property real audioBandHz: 24000
    readonly property real spanStartHz: audioView.viewStart * audioBandHz
    readonly property real spanWidthHz: audioView.viewSpan * audioBandHz

    /// Le tacche della griglia: un chilohertz quando la vista è stretta, di
    /// più quando si allarga — venti righe in tre centimetri non sono una
    /// griglia, sono un retino.
    readonly property var gridTicks: {
        const step = spanWidthHz > 12000 ? 5000 : (spanWidthHz > 4000 ? 1000 : 500)
        const ticks = []
        for (let hz = Math.ceil(spanStartHz / step) * step;
             hz <= spanStartHz + spanWidthHz; hz += step) {
            ticks.push(hz)
        }
        return ticks
    }

    // ── L'oscilloscopio ──────────────────────────────────────────────────
    //
    // Quello che lo spettro non dice: la tosatura, l'inviluppo di una voce, il
    // battimento fra due portanti vicine. Tre cose che nello spettro sono un
    // tappeto di armoniche indistinguibile dal rumore, e qui saltano all'occhio.
    AudioScope {
        id: scope

        Layout.fillWidth: true
        Layout.fillHeight: root.detached
        Layout.preferredHeight: root.detached ? -1 : 70
        Layout.minimumHeight: 56
        gain: scopeGain.value
        spanMs: prefs.spanMs
        triggered: prefs.triggered
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("GUADAGNO")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            id: scopeGain

            Layout.fillWidth: true
            from: 1; to: 40
            value: 1
        }

        Text {
            text: qsTr("×%1").arg(scopeGain.value.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textSecondary
        }
    }

    // ── La base dei tempi ────────────────────────────────────────────────
    //
    // Due millisecondi per contare i cicli di un tono, cinquanta per vedere le
    // sillabe di una voce, duecento per l'inviluppo di una chiamata. Sono tre
    // strumenti diversi con lo stesso disegno, e senza questo comando se ne ha
    // uno solo — quello sbagliato per due casi su tre.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Repeater {
            model: [2, 5, 20, 50, 200]

            delegate: DsdrButton {
                required property int modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: qsTr("%1 ms").arg(modelData)
                checked: Math.abs(prefs.spanMs - modelData) < 0.5
                onClicked: prefs.spanMs = modelData
            }
        }

        // L'aggancio si spegne per guardare il rumore: lì una salita per lo
        // zero non significa niente, e cercarla fa saltellare la traccia
        // invece di fermarla.
        DsdrButton {
            implicitWidth: 62
            implicitHeight: 24
            text: qsTr("SYNC")
            checkable: true
            checked: prefs.triggered
            onClicked: prefs.triggered = checked
        }
    }

    // ── I livelli ────────────────────────────────────────────────────────
    //
    // Picco e valore efficace sulla stessa barra: la loro distanza è il
    // fattore di cresta, e su una barra sola si legge senza sottrazioni.
    AudioLevelBar {
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("PICCO %1 dB").arg(Session.audioPeakDb.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.audioPeakDb > -0.5 ? Theme.danger : Theme.textPrimary
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("RMS %1 dB").arg(Session.audioRmsDb.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textSecondary
        }

        // Il fattore di cresta. Su una voce sta attorno ai dodici decibel; se
        // scende sotto i sei il compressore sta schiacciando tutto, e questo
        // nello spettro non si vede affatto.
        Text {
            text: qsTr("CRESTA %1 dB")
                  .arg((Session.audioPeakDb - Session.audioRmsDb).toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.accent
        }
    }

    // ── Il tono ──────────────────────────────────────────────────────────
    //
    // Il numero che questo pannello esiste per dare. Serve a due cose che si
    // fanno di continuo e che finora si facevano a orecchio: portare una CW
    // alla nota giusta — quella che si sceglie una volta e si insegue per anni
    // — e verificare che una portante stia dove si crede.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("TONO")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Text {
            Layout.fillWidth: true
            text: Session.audioToneHz > 0
                  ? qsTr("%1 Hz").arg(Math.round(Session.audioToneHz))
                  : qsTr("—")
            font.pixelSize: Theme.fontNormal
            font.family: Theme.monoFamily
            font.bold: Session.audioToneHz > 0
            // Spento quando non c'è niente che emerga: un numero grigio si
            // legge come «non lo so», che è la verità.
            color: Session.audioToneHz > 0 ? Theme.accent : Theme.textDisabled
        }

        Text {
            visible: Session.audioToneHz > 0
            text: qsTr("%1 dB").arg(Session.audioToneDb.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textSecondary
        }

        // La distorsione armonica. Un tono puro ha una riga sola; quando la
        // catena audio è sovrapilotata la sinusoide si tosa, e tosare una
        // sinusoide vuol dire fabbricare armoniche. Sopra il cinque per cento
        // si sente, sopra il dieci si sente da chi ascolta dall'altra parte.
        Text {
            visible: Session.audioThdPercent >= 0
            text: qsTr("THD %1%").arg(Session.audioThdPercent.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            font.bold: Session.audioThdPercent > 5
            color: Session.audioThdPercent > 10 ? Theme.danger
                 : Session.audioThdPercent > 5 ? Theme.spectrumPeak
                 : Theme.success
        }
    }

    // ── Zoom della banda audio ───────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Repeater {
            model: [
                { label: qsTr("2.4k"), span: 2400 },
                { label: qsTr("6k"), span: 6000 },
                { label: qsTr("12k"), span: 12000 },
                { label: qsTr("24k"), span: 24000 },
            ]

            delegate: DsdrButton {
                required property var modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: modelData.label
                checked: Math.abs(audioView.viewSpan * root.audioBandHz - modelData.span) < 50
                onClicked: {
                    audioView.viewSpan = modelData.span / root.audioBandHz
                    prefs.viewSpan = audioView.viewSpan
                }
            }
        }
    }

    // ── Cosa mostra la palette ───────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrComboBox {
            Layout.fillWidth: true
            model: audioView.paletteNames
            currentIndex: audioView.paletteIndex
            onActivated: (index) => {
                audioView.paletteIndex = index
                prefs.paletteIndex = index
            }
        }

        DsdrButton {
            implicitWidth: 84
            implicitHeight: 24
            text: qsTr("Picchi")
            checkable: true
            checked: audioView.peakHold
            onClicked: {
                audioView.peakHold = checked
                prefs.peakHold = checked
            }
        }
    }

    // ── Quello che l'audio misura ────────────────────────────────────────
    //
    // Le stesse cifre della catena RX, qui perché è qui che si guarda mentre
    // si regola: cambiare la riduzione di rumore e leggere il rapporto
    // segnale-rumore due pannelli più in là vuol dire non collegare le due
    // cose.
    Text {
        Layout.fillWidth: true
        visible: Session.connected
        text: qsTr("fondo %1 dB · vetta %2 dB")
              .arg(Math.round(audioView.noiseFloorDb))
              .arg(Math.round(audioView.peakLevelDb))
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFamily
        color: Theme.textSecondary
        elide: Text.ElideRight
    }
}
