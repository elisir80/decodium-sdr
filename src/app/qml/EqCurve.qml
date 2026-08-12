// SPDX-License-Identifier: GPL-3.0-or-later
// L'equalizzatore a curva, sopra lo spettro vivo (SPEC-005 §4.2).
//
// Due lezioni messe insieme. La prima: una campana si regola trascinando un
// punto, non compilando tre caselle — frequenza, guadagno e Q sono i tre gradi
// di libertà di un punto su un piano, e trattarli come tre numeri separati
// costringe a immaginare la curva che poi si disegna da sé.
//
// La seconda, che è quella che conta: la curva sta **sopra lo spettro vivo**.
// Si trascina il punto e si vede la voce cambiare forma sotto la curva, nello
// stesso istante e nello stesso riquadro. Il legame fra il gesto e l'effetto
// smette di passare dalla memoria — che è lo strumento di misura peggiore che
// ci sia, e che è quello con cui si regola un equalizzatore in quasi tutti i
// programmi.
//
// Sopra e non accanto: due riquadri affiancati sono due immagini da
// confrontare, e confrontare è già un lavoro.
import QtQuick
import QtQuick.Shapes
import DecodiumSdr

Item {
    id: root

    /// La porzione di banda audio disegnata, in hertz. La stessa dello spettro
    /// che sta sotto: due assi diversi nello stesso riquadro sarebbero una
    /// trappola.
    property real spanStartHz: 0
    property real spanWidthHz: 4000

    /// Il fondo scala verticale, in decibel. Dodici sopra e dodici sotto: è
    /// quanto può fare una campana, e mostrarne di più vorrebbe dire disegnare
    /// spazio in cui il filtro non può andare.
    readonly property real rangeDb: 12

    /// Su quale delle due catene si sta lavorando.
    ///
    /// Il componente è lo stesso perché il gesto è lo stesso: cinque campane,
    /// un punto per campana, la curva sopra lo spettro vivo. Cambia solo da
    /// che parte del ricetrasmettitore si trova quello spettro — e duplicare
    /// il file per cambiare un prefisso avrebbe voluto dire due curve da
    /// tenere allineate a mano per sempre.
    property bool transmit: false

    readonly property bool eqEnabled: transmit ? Session.txEqEnabled : Session.audioEqEnabled
    readonly property int bandCount: transmit ? Session.txEqBandCount : Session.audioEqBandCount

    function bandHz(i) {
        return transmit ? Session.txEqFrequency(i) : Session.audioEqFrequency(i)
    }
    function bandDb(i) {
        return transmit ? Session.txEqGainDb(i) : Session.audioEqGainDb(i)
    }
    function bandQ(i) {
        return transmit ? Session.txEqQ(i) : Session.audioEqQ(i)
    }
    function responseAt(hz) {
        return transmit ? Session.txEqResponseDb(hz) : Session.audioEqResponseDb(hz)
    }
    function setBand(i, hz, db, q) {
        if (transmit)
            Session.setTxEqBand(i, hz, db, q)
        else
            Session.setAudioEqBand(i, hz, db, q)
    }

    /// Quale punto si sta trascinando, o −1.
    property int active: -1

    /// La rotellina sopra un punto ne cambia la larghezza: il Q è il terzo
    /// grado di libertà, e non c'è un terzo asse su cui trascinarlo.
    property real wheelStep: 0.15

    clip: true

    function xFor(hz) {
        return (hz - spanStartHz) / Math.max(1, spanWidthHz) * width
    }

    function hzFor(x) {
        return spanStartHz + (x / Math.max(1, width)) * spanWidthHz
    }

    function yFor(db) {
        return (0.5 - db / (2 * rangeDb)) * height
    }

    function dbFor(y) {
        return (0.5 - y / Math.max(1, height)) * 2 * rangeDb
    }

    /// Cambia a ogni ritocco: è quello che fa ridisegnare la curva. Le funzioni
    /// non hanno dipendenze che QML possa seguire, e senza un appiglio esplicito
    /// la curva resterebbe quella di prima.
    property int revision: 0

    Connections {
        target: Session
        function onAudioEqChanged() { root.revision++ }
        function onTxEqChanged() { root.revision++ }
    }

    // ── La griglia ───────────────────────────────────────────────────────
    //
    // Lo zero marcato e i due estremi: senza, una campana da tre decibel e una
    // da dieci hanno lo stesso aspetto.
    Repeater {
        model: [-12, -6, 0, 6, 12]

        delegate: Item {
            required property int modelData

            anchors.fill: parent

            Rectangle {
                y: Math.round(root.yFor(parent.modelData))
                width: parent.width
                height: 1
                color: parent.modelData === 0 ? Theme.borderStrong : Theme.border
                opacity: parent.modelData === 0 ? 0.9 : 0.5
            }

            Text {
                x: 2
                y: Math.round(root.yFor(parent.modelData)) - implicitHeight - 1
                visible: parent.modelData !== 0
                text: (parent.modelData > 0 ? "+" : "") + parent.modelData
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
            }
        }
    }

    // ── La curva ─────────────────────────────────────────────────────────
    Shape {
        anchors.fill: parent

        ShapePath {
            id: curve

            strokeColor: root.eqEnabled ? Theme.accent : Theme.textDisabled
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                id: line
                path: root.curvePoints
            }
        }
    }

    /// I punti della curva. Sessanta segmenti bastano: la campana più stretta
    /// che il filtro concede resta comunque larga qualche decina di hertz, e
    /// disegnarne mille costerebbe senza mostrare niente di più.
    readonly property var curvePoints: {
        revision      // dipendenza voluta
        const points = []
        const steps = 60
        for (let i = 0; i <= steps; ++i) {
            const hz = spanStartHz + (i / steps) * spanWidthHz
            points.push(Qt.point(xFor(hz), yFor(responseAt(hz))))
        }
        return points
    }

    // ── I punti ──────────────────────────────────────────────────────────
    Repeater {
        model: root.bandCount

        delegate: Item {
            id: knob

            required property int index

            readonly property real hz: {
                root.revision
                return root.bandHz(index)
            }
            readonly property real db: {
                root.revision
                return root.bandDb(index)
            }
            readonly property real q: {
                root.revision
                return root.bandQ(index)
            }

            x: root.xFor(hz) - width / 2
            y: root.yFor(db) - height / 2
            width: 26
            height: 26
            // Un punto fuori dalla porzione disegnata non si mostra: sarebbe un
            // comando appiccicato al bordo che muove qualcosa che non si vede.
            visible: hz >= root.spanStartHz && hz <= root.spanStartHz + root.spanWidthHz

            // L'alone: dice quanto è larga la campana. Il Q non ha un asse su
            // cui stare, e senza questo l'unico modo di conoscerlo è leggerlo.
            Rectangle {
                anchors.centerIn: parent
                width: Math.max(12, 26 / Math.max(0.3, knob.q))
                height: width
                radius: width / 2
                color: Theme.accent
                opacity: root.active === knob.index ? 0.28 : 0.14
            }

            Rectangle {
                anchors.centerIn: parent
                width: 11
                height: 11
                radius: 5.5
                color: root.eqEnabled ? Theme.accent : Theme.textDisabled
                border.width: root.active === knob.index ? 2 : 0
                border.color: Theme.textPrimary
            }

            HoverHandler {
                id: knobHover
                cursorShape: Qt.SizeAllCursor
            }

            // Trascinare muove insieme frequenza e guadagno: sono le due cose
            // che si guardano, e separarle in due gesti vorrebbe dire fare due
            // volte la stessa prova.
            DragHandler {
                id: drag

                target: null
                cursorShape: Qt.ClosedHandCursor

                onActiveChanged: root.active = active ? knob.index : -1

                onCentroidChanged: {
                    if (!active)
                        return
                    const point = centroid.position
                    const scenePoint = knob.mapToItem(root, point.x, point.y)
                    root.setBand(knob.index,
                                 root.hzFor(scenePoint.x),
                                 root.dbFor(scenePoint.y),
                                 knob.q)
                }
            }

            WheelHandler {
                // La rotellina stringe e allarga la campana. È il gesto che si
                // fa già sullo spettro per lo zoom, e qui significa la stessa
                // cosa: «più stretto», «più largo».
                onWheel: (event) => {
                    const factor = event.angleDelta.y > 0 ? (1 + root.wheelStep)
                                                          : (1 - root.wheelStep)
                    root.setBand(knob.index, knob.hz, knob.db, knob.q * factor)
                    event.accepted = true
                }
            }

            // La targhetta con i tre numeri, mentre si trascina. Non sempre:
            // cinque targhette ferme sopra una curva sono cinque cose da
            // leggere invece della curva.
            Rectangle {
                visible: root.active === knob.index || knobHover.hovered
                anchors.bottom: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: readout.implicitWidth + 2 * Theme.spacing
                height: readout.implicitHeight + 4
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: 1
                border.color: Theme.accent

                Text {
                    id: readout
                    anchors.centerIn: parent
                    text: qsTr("%1 Hz · %2 dB · Q %3")
                          .arg(Math.round(knob.hz))
                          .arg(knob.db.toFixed(1))
                          .arg(knob.q.toFixed(1))
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textPrimary
                }
            }
        }
    }
}
