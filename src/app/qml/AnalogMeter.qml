// SPDX-License-Identifier: GPL-3.0-or-later
// S-meter a lancetta.
//
// La barra colorata dice il livello; una lancetta dice anche *come si sta
// muovendo*. È la differenza fra sapere che un segnale è S7 e vedere che sta
// evanescendo — e su una radio è quest'ultima l'informazione che serve mentre
// si decide se chiamare.
//
// La scala si disegna una volta sola su un Canvas; la lancetta è un rettangolo
// che ruota. Ridipingere il quadrante a ogni aggiornamento del livello
// significherebbe rasterizzare quindici volte al secondo qualcosa che non
// cambia mai.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Livello del segnale, in dBFS.
    required property real levelDb

    /// Estremi della scala. Gli stessi di SignalMeter: i due strumenti devono
    /// dire lo stesso numero, o uno dei due mente.
    property real floorDb: -140
    property real ceilingDb: -20

    implicitHeight: 96

    readonly property real fraction:
        Math.max(0, Math.min(1, (levelDb - floorDb) / (ceilingDb - floorDb)))

    /// Punti S del segnale, secondo [SMeterScale]: sei decibel l'uno, e oltre
    /// S9 si continua a contarli per poterli disporre sul quadrante.
    ///
    /// Prima erano un dodicesimo della dinamica ciascuno, e il quadrante ne
    /// pagava il prezzo due volte: la lancetta a fondo scala puntava sulla
    /// tacca «+60» mentre la lettura sotto diceva «S9+18 dB». Uno dei due
    /// mentiva, e non c'era modo di sapere quale.
    readonly property real sUnits: SMeterScale.units(levelDb, ceilingDb)

    readonly property string readout: SMeterScale.readout(levelDb, ceilingDb)

    /// Apertura del quadrante, in gradi da una parte e dall'altra della
    /// verticale.
    readonly property real sweep: 52

    /// Le dodici posizioni del quadrante: nove punti S e tre gradini da venti
    /// decibel. La lancetta le percorre come le percorre lo strumento grande,
    /// dando alla parte bassa — quella dove si lavora — l'arco che merita.
    function positionFor(u) {
        const p = u <= 9 ? u : 9 + (u - 9) * SMeterScale.dbPerUnit / 20
        return Math.max(0, Math.min(12, p))
    }

    readonly property real needleAngle: -sweep + (positionFor(sUnits) / 12) * 2 * sweep

    /// Il perno sta sotto il bordo inferiore: si vede solo la corona esterna
    /// del quadrante, che è come sono fatti gli strumenti veri.
    readonly property real pivotY: height * 1.18
    readonly property real radius: height * 0.98

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border
        clip: true

        // ── Quadrante ────────────────────────────────────────────────────
        Canvas {
            id: dial
            anchors.fill: parent
            antialiasing: true

            // Il quadrante dipende solo dalla geometria: si ridisegna quando
            // cambia la dimensione, non quando cambia il segnale.
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()

                const cx = width / 2
                const cy = root.pivotY
                const r = root.radius
                const toRad = (deg) => (deg - 90) * Math.PI / 180

                // Arco di fondo, e il tratto oltre S9 in tinta d'allarme:
                // sopra S9 si sta comprimendo, ed è bene che si veda.
                ctx.lineWidth = 2
                ctx.strokeStyle = Theme.borderStrong
                ctx.beginPath()
                ctx.arc(cx, cy, r, toRad(-root.sweep), toRad(root.sweep * 0.5))
                ctx.stroke()

                ctx.strokeStyle = Theme.danger
                ctx.beginPath()
                ctx.arc(cx, cy, r, toRad(root.sweep * 0.5), toRad(root.sweep))
                ctx.stroke()

                // Tacche: una per unità S, più alte sulle dispari, che sono
                // quelle etichettate.
                ctx.font = "9px " + Theme.monoFamily
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"

                for (let s = 1; s <= 12; ++s) {
                    const deg = -root.sweep + (s / 12) * 2 * root.sweep
                    const a = toRad(deg)
                    // Sotto S9 si etichettano le dispari, che è la convenzione
                    // di ogni S-meter. Oltre, la tacca è lunga per tutte e tre
                    // ma il testo va solo agli estremi: a quel raggio due
                    // gradini distano undici punti e un'etichetta ne occupa
                    // nove, quindi +20, +40 e +60 finivano una sull'altra.
                    // Chi legge uno strumento sa interpolare la tacca di mezzo.
                    const major = (s % 2 === 1) || s >= 10
                    const labelled = s <= 9 ? major : (s === 10 || s === 12)
                    const inner = r - (major ? 9 : 5)

                    ctx.strokeStyle = s > 9 ? Theme.danger : Theme.textSecondary
                    ctx.lineWidth = major ? 1.5 : 1
                    ctx.beginPath()
                    ctx.moveTo(cx + Math.cos(a) * inner, cy + Math.sin(a) * inner)
                    ctx.lineTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r)
                    ctx.stroke()

                    if (!labelled)
                        continue

                    // Le etichette stanno dentro l'arco, dove c'è spazio. Quelle
                    // oltre S9 rientrano di più: "9" e "+20" cadono su gradini
                    // adiacenti e allo stesso raggio si toccherebbero, mentre
                    // su due cerchi diversi si leggono entrambe.
                    const lr = r - (s <= 9 ? 20 : 32)
                    const label = s <= 9 ? String(s) : "+" + ((s - 9) * 20)
                    ctx.fillStyle = s > 9 ? Theme.danger : Theme.textDisabled
                    ctx.fillText(label, cx + Math.cos(a) * lr, cy + Math.sin(a) * lr)
                }
            }

            // Il tema può cambiare sotto i piedi: senza questo il quadrante
            // resterebbe con i colori vecchi finché non cambia dimensione.
            Connections {
                target: Theme
                function onTextSecondaryChanged() { dial.requestPaint() }
            }
        }

        // ── Lancetta ─────────────────────────────────────────────────────
        Rectangle {
            id: needle

            x: parent.width / 2 - width / 2
            y: root.pivotY - root.radius + 4
            width: 2
            height: root.radius - 4
            color: root.sUnits > 9 ? Theme.danger : Theme.accent
            antialiasing: true

            transform: Rotation {
                origin.x: needle.width / 2
                origin.y: needle.height
                angle: root.needleAngle

                // Una lancetta vera ha inerzia. Senza, a ogni aggiornamento
                // salta, e su un segnale che evanesce non si legge più niente.
                Behavior on angle {
                    NumberAnimation {
                        duration: 90
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        // Perno
        Rectangle {
            x: parent.width / 2 - width / 2
            y: root.pivotY - height / 2
            width: 7
            height: 7
            radius: 3.5
            color: Theme.borderStrong
        }

        // ── Lettura ──────────────────────────────────────────────────────
        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing
            text: root.readout
            font.pixelSize: Theme.fontNormal
            font.family: Theme.monoFamily
            font.bold: true
            color: root.sUnits > 9 ? Theme.danger : Theme.textPrimary
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing
            text: Math.round(root.levelDb) + " dBFS"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textDisabled
        }
    }
}
