// SPDX-License-Identifier: GPL-3.0-or-later
// DECØMETER — strumento di potenza a barre.
//
// Tre archi concentrici: potenza diretta, riflessa, e rapporto di onde
// stazionarie. Sono le tre cose che si guardano trasmettendo, e vanno lette
// insieme: cento watt diretti con un ROS di 3 non sono cento watt in antenna,
// sono venticinque watt che tornano indietro nel finale.
//
// Le barre invece della lancetta, qui, per una ragione precisa: la potenza in
// SSB è tutta picchi: una lancetta non riesce a seguirli e mostra una media
// che non corrisponde a niente. Un segmento che resta acceso — il picco che
// tiene tre secondi — dice quanto si è arrivati davvero.
//
// Lo strumento non inventa: finché nessun backend consegna misure di potenza
// resta spento e lo dichiara. Un wattmetro che mostra un numero plausibile
// accanto a un'antenna vera non è un difetto estetico.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Item {
    id: root

    /// Le misure, o la loro assenza.
    property bool metersAvailable: false
    property real forwardWatt: 0
    property real reflectedWatt: 0
    property real swr: 1
    property bool transmitting: false

    /// Portata a fondo scala, in watt. Le quattro dello strumento di
    /// riferimento: da una chiavetta QRP a un amplificatore.
    readonly property var ranges: [5, 50, 500, 5000]
    property int rangeIndex: 1
    readonly property real fullScale: ranges[Math.max(0, Math.min(ranges.length - 1, rangeIndex))]

    /// La portata si sceglie da sé quando la potenza esce dal fondo scala.
    property bool autoRange: true

    implicitHeight: width * 0.62

    // ── Ballistica ───────────────────────────────────────────────────────
    //
    // Attacco immediato, rilascio lento, picco che tiene: è la risposta di un
    // wattmetro a termocoppia, ed è quella giusta per la voce. Un valore che
    // scende alla stessa velocità con cui sale rende illeggibile la SSB.
    property real displayForward: 0
    property real displayReflected: 0
    property real displaySwr: 1
    property real peakForward: 0
    property real peakReflected: 0

    readonly property real forwardFraction: clamp01(displayForward / fullScale)
    readonly property real reflectedFraction: clamp01(displayReflected / (fullScale * 0.2))
    /// Il ROS si mostra come coefficiente di riflessione: è quello che rende
    /// leggibile una scala che altrimenti finirebbe all'infinito.
    readonly property real swrFraction: clamp01((displaySwr - 1) / (displaySwr + 1))
    readonly property real peakForwardFraction: clamp01(peakForward / fullScale)

    function clamp01(v) {
        return !isFinite(v) ? 0 : Math.max(0, Math.min(1, v))
    }

    Timer {
        id: ballistics

        interval: 50
        running: root.visible
        repeat: true

        onTriggered: {
            const dt = interval / 1000
            const release = (current, target, tau) =>
                target >= current ? target
                                  : current + (target - current) * (1 - Math.exp(-dt / tau))

            const fwd = root.metersAvailable ? root.forwardWatt : 0
            const ref = root.metersAvailable ? root.reflectedWatt : 0

            root.displayForward = release(root.displayForward, fwd, 0.5)
            root.displayReflected = release(root.displayReflected, ref, 0.5)
            root.displaySwr = release(root.displaySwr, root.metersAvailable ? root.swr : 1, 0.4)

            // Il picco tiene, poi scende: tre secondi sono il tempo di
            // guardare lo strumento dopo aver finito la frase.
            root.peakForward = Math.max(root.displayForward, root.peakForward - root.fullScale * dt / 3)
            root.peakReflected = Math.max(root.displayReflected, root.peakReflected - root.fullScale * dt / 3)

            if (root.autoRange)
                root.chooseRange()
        }
    }

    /// Sale di portata quando si sfiora il fondo scala, scende quando si sta
    /// abbondantemente sotto quella inferiore. Il divario fra le due soglie
    /// non è pigrizia: senza, una potenza a cavallo del confine farebbe
    /// rimbalzare la scala avanti e indietro a ogni sillaba.
    function chooseRange() {
        if (displayForward > fullScale * 0.95 && rangeIndex < ranges.length - 1)
            rangeIndex = rangeIndex + 1
        else if (rangeIndex > 0 && displayForward < ranges[rangeIndex - 1] * 0.4)
            rangeIndex = rangeIndex - 1
    }

    onFullScaleChanged: {
        peakForward = 0
        peakReflected = 0
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border
        clip: true

        // ── Gli archi ────────────────────────────────────────────────────
        Canvas {
            id: arcs

            anchors.fill: parent
            anchors.bottomMargin: readout.height + rangeRow.height + 2 * Theme.spacingTight
            antialiasing: true

            // I valori cambiano di continuo: ridipingere è il lavoro di questo
            // Canvas, non un'eccezione. Ma solo qui — la parte numerica sono
            // Text, che si aggiornano da soli.
            Connections {
                target: root
                function onForwardFractionChanged() { arcs.requestPaint() }
                function onReflectedFractionChanged() { arcs.requestPaint() }
                function onSwrFractionChanged() { arcs.requestPaint() }
                function onPeakForwardFractionChanged() { arcs.requestPaint() }
                function onMetersAvailableChanged() { arcs.requestPaint() }
                // La portata cambia i numeri stampati sulla scala. Senza
                // questa riga restavano quelli di prima: lo strumento diceva
                // «50» a fondo scala mentre misurava sui cinque watt, ed è il
                // tipo di errore che si scopre bruciando un finale.
                function onFullScaleChanged() { arcs.requestPaint() }
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                if (width <= 0 || height <= 0)
                    return

                // Il perno sta molto sotto il riquadro: si vede solo la fetta
                // alta della corona, che è come sono fatti gli strumenti a
                // barre — l'arco è quasi piatto e i segmenti restano leggibili.
                //
                // Il raggio è il più grande che ci sta, e i due vincoli vanno
                // scritti per intero o l'arco resta molto più piccolo del
                // riquadro. In altezza serve posto per lo spicchio che l'arco
                // esterno guadagna sui suoi estremi, per le tre bande e per i
                // numeri; in larghezza, per la semicorda e per i numeri agli
                // estremi.
                const cx = width / 2
                const half = 30 * Math.PI / 180
                const cos = Math.cos(half)
                const labelSpace = 14

                // Con spessore `t = outer * 0.055` e la banda più interna a
                // `outer - 4.7t`, l'altezza occupata vale 0.414·outer.
                const outer = Math.max(10, Math.min(
                        (height - labelSpace - 12) / 0.414,
                        (width / 2 - labelSpace - 8) / Math.sin(half)))
                const t = Math.max(3, outer * 0.055)
                const cy = outer + t + labelSpace + 12

                const a0 = -Math.PI / 2 - half
                const a1 = -Math.PI / 2 + half
                const ang = (f) => a0 + (a1 - a0) * f

                const dim = root.metersAvailable ? 1 : 0.28

                // Un arco di segmenti: acceso fino alla frazione, con il
                // segmento del picco lasciato acceso più avanti.
                const band = (radius, thickness, count, lit, peak, colorAt) => {
                    const step = (a1 - a0) / count
                    for (let i = 0; i < count; ++i) {
                        const f = (i + 0.5) / count
                        const a = a0 + step * (i + 0.5)
                        const ca = Math.cos(a)
                        const sa = Math.sin(a)
                        ctx.beginPath()
                        ctx.moveTo(cx + radius * ca, cy + radius * sa)
                        ctx.lineTo(cx + (radius + thickness) * ca, cy + (radius + thickness) * sa)
                        ctx.lineWidth = Math.max(2, step * radius * 0.62)
                        if (f <= lit) {
                            ctx.strokeStyle = colorAt(f)
                            ctx.globalAlpha = dim
                        } else {
                            ctx.strokeStyle = Theme.meterUnlit
                            ctx.globalAlpha = 1
                        }
                        ctx.stroke()
                        ctx.globalAlpha = 1
                    }

                    if (peak > 0.02) {
                        const i = Math.min(count - 1, Math.floor(peak * count))
                        const a = a0 + step * (i + 0.5)
                        const ca = Math.cos(a)
                        const sa = Math.sin(a)
                        ctx.beginPath()
                        ctx.moveTo(cx + radius * ca, cy + radius * sa)
                        ctx.lineTo(cx + (radius + thickness) * ca, cy + (radius + thickness) * sa)
                        ctx.strokeStyle = colorAt(peak)
                        ctx.lineWidth = Math.max(2, step * radius * 0.62)
                        ctx.globalAlpha = dim
                        ctx.stroke()
                        ctx.globalAlpha = 1
                    }
                }

                const powerColor = (f) => f < 0.7 ? Theme.meterSafe
                                        : f < 0.9 ? Theme.meterCaution : Theme.meterDanger
                // Sul ROS le soglie sono più severe: 1.5 è già ambra, 3 è
                // rosso. Non è pignoleria — è dove un finale a stato solido
                // comincia a ridurre da sé.
                const swrColor = (f) => f < 0.2 ? Theme.meterSafe
                                      : f < 0.5 ? Theme.meterCaution : Theme.meterDanger

                band(outer, t, 32, root.forwardFraction, root.peakForwardFraction, powerColor)
                band(outer - t * 2.1, t * 0.8, 24, root.reflectedFraction, 0, powerColor)
                band(outer - t * 3.9, t * 0.8, 20, root.swrFraction, 0, swrColor)

                // Le tacche della scala della potenza, con il valore in watt
                // della portata scelta.
                ctx.strokeStyle = Theme.meterScale
                ctx.fillStyle = Theme.meterScaleText
                ctx.lineWidth = 1
                ctx.font = "9px " + Theme.monoFamily
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"

                // Otto tacche, e il numero solo su una ogni due: a metà scala
                // di cinquanta watt un quarto vale 12,5, e uno strumento che
                // stampa «13» dice una cosa che non è vera. I quarti restano
                // come tacca — si contano — e il numero compare dove è tondo.
                for (let k = 0; k <= 8; ++k) {
                    const f = k / 8
                    const a = ang(f)
                    const ca = Math.cos(a)
                    const sa = Math.sin(a)
                    const r0 = outer + t
                    const major = k % 2 === 0
                    ctx.beginPath()
                    ctx.moveTo(cx + r0 * ca, cy + r0 * sa)
                    ctx.lineTo(cx + (r0 + (major ? 5 : 3)) * ca, cy + (r0 + (major ? 5 : 3)) * sa)
                    ctx.stroke()

                    if (!major)
                        continue

                    const value = root.fullScale * f
                    const label = value >= 1000 ? (value / 1000) + "k"
                                : value >= 10 || value === 0 ? String(Math.round(value))
                                : value.toFixed(1)
                    const lr = r0 + 12
                    ctx.fillText(label, cx + lr * ca, cy + lr * sa)
                }

                ctx.textAlign = "left"
                ctx.fillStyle = Theme.meterScaleText
                ctx.font = "bold 9px " + Theme.monoFamily
                // Il nome di ciascuna banda, al suo estremo sinistro. Il
                // `Math.max` tiene la scritta dentro il riquadro: senza, la
                // banda più esterna — che è anche la più lunga — spingeva
                // «FWD» oltre il bordo e se ne leggeva «WD».
                const labelAt = (radius, text) => {
                    const a = ang(0)
                    ctx.fillText(text,
                                 Math.max(3, cx + (radius + 2) * Math.cos(a) - 26),
                                 cy + (radius + 2) * Math.sin(a) + 1)
                }
                labelAt(outer, "FWD")
                labelAt(outer - t * 2.1, "REF")
                labelAt(outer - t * 3.9, "SWR")
            }
        }

        // ── Portata ──────────────────────────────────────────────────────
        // Le portate stanno fra il quadrante e la lettura, non in cima: sopra
        // finivano addosso al numero di mezzo della scala, che è quello che si
        // guarda per capire dove sta la barra.
        RowLayout {
            id: rangeRow

            anchors.bottom: readout.top
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            spacing: Theme.spacingTight

            Text {
                text: qsTr("FONDO")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
            }

            Repeater {
                model: root.ranges

                delegate: Rectangle {
                    required property int index
                    required property int modelData

                    readonly property bool current: index === root.rangeIndex

                    implicitWidth: rangeLabel.implicitWidth + 2 * Theme.spacingTight
                    implicitHeight: rangeLabel.implicitHeight + Theme.spacingTight
                    radius: Theme.radiusSmall
                    color: current ? Theme.accentDim : "transparent"
                    border.width: 1
                    border.color: current ? Theme.accent : Theme.border

                    Text {
                        id: rangeLabel
                        anchors.centerIn: parent
                        text: modelData >= 1000 ? (modelData / 1000) + qsTr("kW")
                                                : modelData + qsTr("W")
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                        color: parent.current ? Theme.textPrimary : Theme.textDisabled
                    }

                    TapHandler {
                        // Scegliere una portata a mano spegne l'automatismo:
                        // averlo attivo rimetterebbe la scala dove vuole lui
                        // al primo picco, e sembrerebbe che il comando non
                        // funzioni.
                        onTapped: {
                            root.autoRange = false
                            root.rangeIndex = parent.index
                        }
                    }
                }
            }

            Rectangle {
                implicitWidth: autoLabel.implicitWidth + 2 * Theme.spacingTight
                implicitHeight: autoLabel.implicitHeight + Theme.spacingTight
                radius: Theme.radiusSmall
                color: root.autoRange ? Theme.accentDim : "transparent"
                border.width: 1
                border.color: root.autoRange ? Theme.accent : Theme.border

                Text {
                    id: autoLabel
                    anchors.centerIn: parent
                    text: qsTr("AUTO")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: root.autoRange ? Theme.textPrimary : Theme.textDisabled
                }

                TapHandler {
                    onTapped: root.autoRange = !root.autoRange
                }
            }
        }

        // ── Lettura numerica ─────────────────────────────────────────────
        Rectangle {
            id: readout

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingTight
            implicitHeight: readoutGrid.implicitHeight + 2 * Theme.spacingTight
            radius: Theme.radiusSmall
            color: Theme.meterDisplayBackground
            border.width: 1
            border.color: root.swr >= 3 && root.metersAvailable ? Theme.meterDanger : Theme.border

            GridLayout {
                id: readoutGrid

                anchors.fill: parent
                anchors.margins: Theme.spacingTight
                columns: 4
                columnSpacing: Theme.spacing
                rowSpacing: 2

                Text {
                    text: qsTr("FWD")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    font.bold: true
                    color: Theme.meterReadout
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: root.metersAvailable
                          ? root.peakForward.toFixed(root.peakForward >= 100 ? 0 : 1) + " W"
                          : "——"
                    font.pixelSize: Theme.fontNormal
                    font.family: Theme.monoFamily
                    font.bold: true
                    color: Theme.meterReadout
                }

                Text {
                    text: qsTr("SWR")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    font.bold: true
                    color: Theme.meterCaution
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: root.metersAvailable ? root.displaySwr.toFixed(2) : "——"
                    font.pixelSize: Theme.fontNormal
                    font.family: Theme.monoFamily
                    font.bold: true
                    color: root.displaySwr >= 3 ? Theme.meterDanger : Theme.meterCaution
                }

                Text {
                    text: qsTr("REF")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: root.metersAvailable ? root.displayReflected.toFixed(2) + " W" : "——"
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }

                Text {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    // Lo stato dello strumento, che è la prima cosa da sapere:
                    // un wattmetro fermo su zero e un wattmetro senza sensore
                    // si assomigliano troppo.
                    text: !root.metersAvailable ? qsTr("nessun sensore")
                          : root.transmitting ? qsTr("in trasmissione")
                          : qsTr("in attesa")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: root.metersAvailable && root.transmitting
                           ? Theme.transmit : Theme.textDisabled
                }
            }
        }
    }
}
