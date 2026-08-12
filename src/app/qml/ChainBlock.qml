// SPDX-License-Identifier: GPL-3.0-or-later
// Un blocco della catena, con il suo interruttore addosso.
//
// L'interruttore sta sul blocco e non in un menù: spegnere e riaccendere è il
// gesto con cui si giudica se quello stadio serve, e farlo altrove significa
// smettere di guardare il segnale proprio mentre lo si sta valutando.
//
// Il blocco dice quattro cose senza chiedere di leggerne più di due: come si
// chiama, se è acceso (il punto), che cosa fa alla forma d'onda (il glifo), e
// come sta regolato (il numero). Il glifo è la parte che si guarda per prima:
// «Gate» e «Limiter» sono due parole che a chi non le ha già imparate non
// dicono niente, mentre una soglia con un gradino e una cima tosata si
// capiscono prima di essere lette.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import DecodiumSdr

Item {
    id: root

    property string title: ""

    /// Come sta regolato adesso, in due parole. Vuoto quando non c'è niente da
    /// dire: una riga vuota è meglio di una riga che ripete il titolo.
    property string readout: ""

    /// Il segno di che cosa fa, in coordinate 72×26. Vuoto: niente disegno.
    property string glyph: ""

    /// Se lo stadio sta lavorando. Un blocco spento resta al suo posto — è
    /// parte della catena anche in bypass — ma si spegne anche a vedersi, e
    /// il segnale gli passa sopra con un salto.
    property bool on: true

    /// Se lo stadio si può accendere e spegnere. Il filtro e il modulatore no:
    /// esistono sempre, e un interruttore che non si può premere è rumore.
    property bool switchable: true

    /// Se è questo il blocco di cui si stanno vedendo i comandi.
    property bool selected: false

    /// Lo stadio sta lavorando al limite: la tinta passa all'ambra. Non è un
    /// guasto ed è per questo che non è rossa — è un «guarda qui».
    property bool warning: false

    /// Spento perché manca qualcosa — il motore neurale che non c'è, il
    /// microfono che non è stato scelto. Diverso da «in bypass»: quello è una
    /// scelta, questo è un fatto.
    property bool unavailable: false

    /// Lo stadio neurale porta la sua targhetta: costa un thread e qualche
    /// millisecondo, e chi lo tiene acceso deve saperlo senza cercarlo.
    property bool neural: false
    property string neuralTag: ""

    signal toggled()
    signal picked()

    implicitWidth: 112
    // L'altezza la decide il contenuto: il glifo e la lettura non stanno in
    // una misura fissa, e fissarla li faceva sbordare fuori dalla scheda —
    // una lettura che galleggia sotto il blocco sembra appartenere a quello
    // dopo.
    implicitHeight: card.anchors.topMargin + column.implicitHeight + 2 * 7

    readonly property color tint: unavailable ? Theme.textDisabled
                                : !on ? Theme.border
                                : warning ? Theme.spectrumPeak
                                : neural ? Theme.spectrumPeak
                                : Theme.accent

    // ── Il salto del bypass ──────────────────────────────────────────────
    //
    // Un arco che scavalca il blocco escluso. È il disegno che dice «il
    // segnale passa di qua» senza una parola, e senza di lui un blocco spento
    // e uno acceso ma silenzioso hanno lo stesso aspetto.
    Shape {
        id: bypassArc

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: root.width + 36
        height: 14
        visible: root.switchable && !root.on && !root.unavailable

        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 1.6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            startX: 2
            startY: 13
            // Le coordinate sono quelle di questa forma, non del blocco: legarle
            // al genitore del genitore dava un arco largo quanto il pannello,
            // che attraversava il disegno da parte a parte.
            PathCubic {
                x: bypassArc.width - 2
                y: 13
                control1X: bypassArc.width * 0.3; control1Y: -12
                control2X: bypassArc.width * 0.7; control2Y: -12
            }
        }
    }

    Rectangle {
        id: card

        anchors.fill: parent
        anchors.topMargin: 12
        radius: 8
        color: root.selected ? Theme.surfaceRaised : Theme.surface
        border.width: root.selected ? 2 : 1
        border.color: root.unavailable ? Theme.border
                    : root.selected ? Theme.accent
                    : root.warning ? Theme.spectrumPeak
                    : !root.on ? Theme.border
                    : Theme.borderStrong
        opacity: root.unavailable ? 0.5 : (root.on ? 1 : 0.6)

        Behavior on border.color {
            ColorAnimation { duration: Theme.animationFast }
        }

        Column {
            id: column

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 7
            spacing: 4

            Item {
                width: parent.width
                height: name.implicitHeight

                Text {
                    id: name

                    anchors.left: parent.left
                    anchors.right: led.left
                    anchors.rightMargin: 5
                    text: root.title
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    color: root.unavailable ? Theme.textDisabled : Theme.textPrimary
                    elide: Text.ElideRight
                }

                // ── L'interruttore ───────────────────────────────────────
                //
                // Un punto, non un pulsante: lo spazio è quello di un blocco in
                // una fila di blocchi, e un comando grande quanto il blocco
                // sposterebbe l'attenzione dalla catena al comando.
                Rectangle {
                    id: led

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: root.unavailable ? Theme.border
                         : root.on ? Theme.success : "transparent"
                    border.width: 1
                    border.color: root.unavailable ? Theme.border
                                : root.on ? Theme.success
                                : ledHover.hovered ? Theme.textPrimary : Theme.textDisabled

                    HoverHandler {
                        id: ledHover
                        enabled: root.switchable && !root.unavailable
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: root.switchable && !root.unavailable
                        onTapped: root.toggled()
                    }

                    ToolTip.visible: ledHover.hovered
                    ToolTip.text: root.on ? qsTr("Escludi %1").arg(root.title)
                                          : qsTr("Inserisci %1").arg(root.title)
                }
            }

            // La targhetta del motore neurale.
            Row {
                visible: root.neural
                spacing: 4

                Text {
                    text: qsTr("●AI")
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    color: Theme.spectrumPeak
                }

                Rectangle {
                    visible: root.neuralTag.length > 0
                    width: tag.implicitWidth + 8
                    height: tag.implicitHeight + 2
                    radius: 3
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.spectrumPeak

                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: root.neuralTag
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                        color: Theme.spectrumPeak
                    }
                }
            }

            ChainGlyph {
                width: parent.width
                height: 20
                visible: root.glyph.length > 0 && !root.neural
                path: root.glyph
                stroke: root.tint
            }

            Text {
                width: parent.width
                visible: root.readout.length > 0
                text: root.readout
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: root.on ? root.tint : Theme.textDisabled
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        // Il resto del blocco apre i suoi comandi. Sta dopo il punto nello
        // stacco dei gesti, così premere l'interruttore non apre anche il
        // pannello.
        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: root.picked()
        }
    }
}
