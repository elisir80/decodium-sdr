// SPDX-License-Identifier: GPL-3.0-or-later
// Display di frequenza a cifre indipendenti.
//
// È il modo in cui si sintonizza una radio a definizione software da sempre:
// ogni cifra è una zona sensibile, la rotellina sopra una cifra cambia quella
// cifra e basta. Chi opera sa già dove mettere il puntatore per muoversi di
// 100 Hz o di 1 MHz, senza cercare un controllo diverso per ogni passo.
//
// Il riporto è quello dell'aritmetica: incrementare il 9 delle centinaia porta
// a 1000, non azzera la cifra. Il comportamento "a rotella" che si ferma sul 9
// è tipico delle radio con encoder meccanico e qui sarebbe solo scomodo.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Frequenza mostrata, in Hz.
    property real frequencyHz: 0
    /// Limiti entro cui la sintonia può muoversi.
    property real minimumHz: 0
    property real maximumHz: 30000000
    property bool editable: true

    /// Emesso quando l'operatore muove una cifra.
    signal tuneRequested(real hz)

    readonly property int digitCount: 9        // fino a 999,999999 MHz
    property int digitSize: Theme.fontDisplay
    property color activeColor: Theme.textPrimary
    property color inactiveColor: Theme.textDisabled

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    /// Cifre della frequenza, dalla più significativa. Sono ricavate una volta
    /// sola per aggiornamento invece che per cifra: a ogni giro di rotellina
    /// il display si ridisegna tutto.
    readonly property var digits: {
        const value = Math.max(0, Math.round(frequencyHz))
        const text = String(value).padStart(digitCount, "0")
        const out = []
        for (let i = 0; i < digitCount; ++i)
            out.push(text.charAt(i))
        return out
    }

    /// Prima cifra diversa da zero: quelle prima si mostrano spente, come su
    /// ogni frequenzimetro.
    readonly property int firstSignificant: {
        for (let i = 0; i < digitCount; ++i) {
            if (digits[i] !== "0")
                return i
        }
        return digitCount - 1
    }

    function nudge(digitIndex, direction) {
        if (!editable)
            return
        // La cifra 0 è quella dei 100 MHz, l'ultima quella degli hertz.
        const step = Math.pow(10, digitCount - 1 - digitIndex)
        const target = Math.round(frequencyHz + direction * step)
        tuneRequested(Math.max(minimumHz, Math.min(maximumHz, target)))
    }

    // Un touchpad può consegnare un evento di scroll con soltanto un delta
    // orizzontale: in quel caso angleDelta.y è zero. Zero non è «giù»;
    // trattarlo come -1 faceva scendere il VFO a ogni frame del gesto.
    function wheelDirection(angleDeltaY) {
        return angleDeltaY > 0 ? 1 : (angleDeltaY < 0 ? -1 : 0)
    }

    Row {
        id: row
        spacing: 0

        Repeater {
            model: root.digitCount

            delegate: Item {
                id: digitCell

                required property int index

                // Separatore delle migliaia dopo le cifre 2 e 5: senza, nove
                // cifre di fila non si leggono a colpo d'occhio.
                readonly property bool separatorAfter: index === 2 || index === 5

                width: digitText.implicitWidth + (separatorAfter ? separator.implicitWidth : 0)
                height: digitText.implicitHeight

                Text {
                    id: digitText
                    text: root.digits[digitCell.index]
                    font.pixelSize: root.digitSize
                    font.family: Theme.monoFamily
                    color: digitCell.index < root.firstSignificant
                           ? root.inactiveColor
                           : (hover.hovered ? Theme.accent : root.activeColor)

                    Behavior on color {
                        ColorAnimation { duration: Theme.animationFast }
                    }
                }

                Text {
                    id: separator
                    anchors.left: digitText.right
                    text: digitCell.separatorAfter ? "." : ""
                    font.pixelSize: root.digitSize
                    font.family: Theme.monoFamily
                    color: root.inactiveColor
                    visible: digitCell.separatorAfter
                }

                // Sottolineatura della cifra sotto il puntatore: dice quale
                // passo si sta per muovere prima di muoverlo.
                Rectangle {
                    anchors.left: digitText.left
                    anchors.right: digitText.right
                    anchors.top: digitText.bottom
                    anchors.topMargin: 1
                    height: 2
                    radius: 1
                    color: Theme.accent
                    visible: hover.hovered && root.editable
                }

                HoverHandler {
                    id: hover
                    enabled: root.editable
                    cursorShape: Qt.SizeVerCursor
                }

                WheelHandler {
                    enabled: root.editable
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const direction = root.wheelDirection(event.angleDelta.y)
                        if (direction === 0)
                            return
                        root.nudge(digitCell.index, direction)
                    }
                }

                TapHandler {
                    enabled: root.editable
                    // Clic sulla metà alta incrementa, sulla metà bassa
                    // decrementa: è la stessa gestualità della rotellina per
                    // chi usa un trackpad senza scroll comodo.
                    onTapped: (event) => {
                        root.nudge(digitCell.index, event.position.y < digitCell.height / 2 ? 1 : -1)
                    }
                }
            }
        }
    }
}
