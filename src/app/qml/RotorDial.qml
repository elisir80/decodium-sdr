// SPDX-License-Identifier: GPL-3.0-or-later
// Il quadrante del rotore: dove puntare, letto come su un controller.
//
// **Perché ha questa forma e non un'altra.** Chi ha un rotore in stazione ha
// davanti un quadrante circolare con la croce nord-sud / est-ovest e un ago.
// Non è nostalgia: è che l'azimut è un angolo, e un numero da solo — «287
// gradi» — va convertito mentalmente in una direzione ogni volta che lo si
// legge. Sul quadrante la direzione **è** la posizione dell'ago, e il numero
// serve solo a scriverlo sul quaderno.
//
// **Gli assi cartesiani sono la parte che la maggior parte dei quadranti
// software non ha**, e sono quelli che rendono leggibile un angolo a colpo
// d'occhio: con la croce si vede subito da che parte del nord si sta e di
// quanto si è oltre l'est. Senza, un ago in mezzo a un cerchio vuoto lo si
// legge come si legge un orologio senza lancette delle ore.
//
// **Due aghi, non uno.** La via breve e la via lunga sono la stessa direzione
// dai due lati, e sono centottanta gradi di rotore. Mostrarne uno solo
// costringerebbe a fare la somma in testa proprio nel momento in cui si sta
// decidendo se girare o no.
import QtQuick
import QtQuick.Shapes
import DecodiumSdr

Item {
    id: root

    implicitWidth: 200
    implicitHeight: 200

    /// L'azimut della via breve, in gradi da nord. Negativo: nessun bersaglio.
    property real bearing: -1

    /// La via lunga. Si ricava dalla breve, ma la si passa lo stesso: chi usa
    /// questo componente per altro potrebbe averne una diversa.
    property real longPathBearing: bearing >= 0 ? (bearing + 180) % 360 : -1

    /// Dove sta puntando l'antenna adesso, se qualcuno lo sa dire. Negativo:
    /// non lo sappiamo, e non si disegna un ago che finge di saperlo.
    property real heading: -1

    /// Quale delle due vie si sta usando: cambia quale ago è quello acceso.
    property bool useLongPath: false

    readonly property real ringRadius: Math.min(width, height) / 2 - 14

    /// Da azimut a coordinate sul quadrante. Zero in alto e senso orario, come
    /// una bussola: in trigonometria lo zero è a destra e il senso è
    /// antiorario, e prendere quella convenzione qui vorrebbe dire un
    /// quadrante che si legge specchiato.
    function pointAt(azimuth, radius) {
        const a = (azimuth - 90) * Math.PI / 180
        return Qt.point(width / 2 + radius * Math.cos(a),
                        height / 2 + radius * Math.sin(a))
    }

    // ── Il fondo ─────────────────────────────────────────────────────────
    Rectangle {
        anchors.centerIn: parent
        width: root.ringRadius * 2 + 20
        height: width
        radius: width / 2
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border
    }

    // ── Gli assi cartesiani ──────────────────────────────────────────────
    //
    // La croce arriva fino al bordo e la corona resta libera: è la griglia su
    // cui si legge l'angolo, non una decorazione al centro.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.border
            strokeWidth: 1
            fillColor: "transparent"
            startX: root.width / 2 - root.ringRadius
            startY: root.height / 2
            PathLine { x: root.width / 2 + root.ringRadius; y: root.height / 2 }
        }

        ShapePath {
            strokeColor: Theme.border
            strokeWidth: 1
            fillColor: "transparent"
            startX: root.width / 2
            startY: root.height / 2 - root.ringRadius
            PathLine { x: root.width / 2; y: root.height / 2 + root.ringRadius }
        }

        // Le diagonali, più tenui: servono a leggere i quarantacinque gradi
        // senza contare le tacche.
        ShapePath {
            strokeColor: Theme.border
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [2, 4]
            fillColor: "transparent"
            startX: root.pointAt(45, root.ringRadius).x
            startY: root.pointAt(45, root.ringRadius).y
            PathLine {
                x: root.pointAt(225, root.ringRadius).x
                y: root.pointAt(225, root.ringRadius).y
            }
        }

        ShapePath {
            strokeColor: Theme.border
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [2, 4]
            fillColor: "transparent"
            startX: root.pointAt(135, root.ringRadius).x
            startY: root.pointAt(135, root.ringRadius).y
            PathLine {
                x: root.pointAt(315, root.ringRadius).x
                y: root.pointAt(315, root.ringRadius).y
            }
        }
    }

    // ── Le tacche ────────────────────────────────────────────────────────
    //
    // Una ogni dieci gradi, lunga ogni trenta. È la spaziatura dei controller
    // meccanici, e non è arbitraria: dieci gradi è la precisione con cui si
    // riesce a fermare un rotore, e più fitto sarebbe una scala che promette
    // quello che la meccanica non mantiene.
    Repeater {
        model: 36

        delegate: Rectangle {
            id: tick

            required property int index

            readonly property int azimuth: index * 10
            readonly property bool major: azimuth % 30 === 0

            width: 1
            height: major ? 9 : 5
            color: major ? Theme.textSecondary : Theme.textDisabled
            antialiasing: true

            x: root.pointAt(tick.azimuth, root.ringRadius - tick.height / 2).x
               - tick.width / 2
            y: root.pointAt(tick.azimuth, root.ringRadius - tick.height / 2).y
               - tick.height / 2

            // La tacca punta al centro, non in alto: dentro un `Rotation`
            // `parent` non e' il rettangolo — e' quello che ha causato
            // trentasei righe di «Unable to assign [undefined]» al primo
            // avvio. Con un `id` non c'e' ambiguita'.
            transform: Rotation {
                origin.x: tick.width / 2
                origin.y: tick.height / 2
                angle: tick.azimuth
            }
        }
    }

    // ── I punti cardinali ────────────────────────────────────────────────
    Repeater {
        model: [
            { label: qsTr("N"), azimuth: 0 },
            { label: qsTr("E"), azimuth: 90 },
            { label: qsTr("S"), azimuth: 180 },
            { label: qsTr("O"), azimuth: 270 },
        ]

        delegate: Text {
            required property var modelData

            text: modelData.label
            font.pixelSize: Theme.fontSmall
            font.bold: modelData.azimuth === 0
            color: modelData.azimuth === 0 ? Theme.accent : Theme.textSecondary
            x: root.pointAt(modelData.azimuth, root.ringRadius + 8).x - width / 2
            y: root.pointAt(modelData.azimuth, root.ringRadius + 8).y - height / 2
        }
    }

    // ── L'ago della via lunga ────────────────────────────────────────────
    //
    // Sempre disegnato e sempre spento, anche quando non lo si sta usando:
    // vederlo lì dice che esiste, ed è un'informazione che a chi non ci pensa
    // mai vale più di un'opzione in un menù.
    Shape {
        anchors.fill: parent
        visible: root.longPathBearing >= 0
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.useLongPath ? Theme.accent : Theme.textDisabled
            strokeWidth: root.useLongPath ? 2.4 : 1.4
            fillColor: "transparent"
            startX: root.width / 2
            startY: root.height / 2
            PathLine {
                x: root.pointAt(root.longPathBearing, root.ringRadius - 4).x
                y: root.pointAt(root.longPathBearing, root.ringRadius - 4).y
            }
        }
    }

    // ── L'ago della via breve ────────────────────────────────────────────
    Shape {
        anchors.fill: parent
        visible: root.bearing >= 0
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.useLongPath ? Theme.textDisabled : Theme.accent
            strokeWidth: root.useLongPath ? 1.4 : 2.4
            fillColor: "transparent"
            startX: root.width / 2
            startY: root.height / 2
            PathLine {
                x: root.pointAt(root.bearing, root.ringRadius - 4).x
                y: root.pointAt(root.bearing, root.ringRadius - 4).y
            }
        }
    }

    // La punta dell'ago in uso: un triangolo sul bordo, come l'indice di un
    // controller. Senza, i due aghi si distinguono solo per lo spessore.
    Shape {
        anchors.fill: parent
        visible: root.bearing >= 0
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            id: needleTip

            readonly property real tip: root.useLongPath ? root.longPathBearing
                                                         : root.bearing

            strokeColor: "transparent"
            fillColor: Theme.accent
            startX: root.pointAt(needleTip.tip, root.ringRadius).x
            startY: root.pointAt(needleTip.tip, root.ringRadius).y
            PathLine {
                x: root.pointAt(needleTip.tip - 5, root.ringRadius - 11).x
                y: root.pointAt(needleTip.tip - 5, root.ringRadius - 11).y
            }
            PathLine {
                x: root.pointAt(needleTip.tip + 5, root.ringRadius - 11).x
                y: root.pointAt(needleTip.tip + 5, root.ringRadius - 11).y
            }
        }
    }

    // ── Dove sta puntando davvero l'antenna ──────────────────────────────
    //
    // Un secondo indice, tratteggiato, che compare solo se qualcuno sa dirci
    // la posizione del rotore. Il divario fra questo e l'ago è quanto resta
    // da girare — che è la sola cosa che si guarda mentre gira.
    Shape {
        anchors.fill: parent
        visible: root.heading >= 0
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.success
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            fillColor: "transparent"
            startX: root.width / 2
            startY: root.height / 2
            PathLine {
                x: root.pointAt(root.heading, root.ringRadius - 4).x
                y: root.pointAt(root.heading, root.ringRadius - 4).y
            }
        }
    }

    // ── Il perno e il numero ─────────────────────────────────────────────
    Rectangle {
        anchors.centerIn: parent
        width: 44
        height: 26
        radius: 3
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Text {
            anchors.centerIn: parent
            text: root.bearing < 0
                  ? qsTr("—")
                  : Math.round(root.useLongPath ? root.longPathBearing : root.bearing)
                        + "°"
            font.pixelSize: Theme.fontNormal
            font.family: Theme.monoFamily
            font.bold: true
            color: root.bearing < 0 ? Theme.textDisabled : Theme.accent
        }
    }
}
