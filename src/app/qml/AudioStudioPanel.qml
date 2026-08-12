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
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("STUDIO AUDIO")
    draggable: true
    collapsed: true

    Settings {
        id: prefs
        category: "audio-studio"
        property real viewSpan: 0.1
        property int paletteIndex: 5
        property bool peakHold: true
    }

    // ── Lo spettro dell'audio ────────────────────────────────────────────
    //
    // Lo stesso componente del panadattatore, su un altro feed. Non è
    // un'economia: è la stessa cosa: uno spettro con la sua storia sotto, e
    // averne due che si comportano in modo diverso vorrebbe dire imparare due
    // strumenti per leggere la stessa grandezza.
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 150

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
