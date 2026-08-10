// SPDX-License-Identifier: GPL-3.0-or-later
// DECØMETER-S — lo strumento del segnale, in due modi di disegnarlo.
//
// L'ago e le barre non sono due strumenti: sono lo stesso strumento con due
// letture diverse, e chi opera sceglie in base a cosa sta ascoltando. L'ago ha
// inerzia e la mostra — su un segnale che evanesce si vede il QSB battere,
// cosa che una barra non racconta. Le barre non hanno inerzia e tengono il
// picco: su una stazione che passa rapporti in SSB dicono dove si è arrivati,
// dove l'ago mostrerebbe una media di niente.
//
// Sul quadrante ci sono due scale, e servono a due domande diverse. Quella
// esterna in punti S risponde a «che rapporto passo»; quella interna, in
// decibel, a «quanto è forte davvero» — la prima è a gradini di sei decibel e
// si arrotonda, la seconda no. Fra il fondo di rumore e il segnale c'è un
// cuneo: è il margine che separa quello che si sente da quello che lo copre,
// ed è la cosa che dice se un segnale si copierà.
//
// I decibel sono dBFS e non dBm: nessun backend consegna una calibrazione
// assoluta, e chiamarli dBm significherebbe promettere una taratura che non
// c'è. La scala e le distanze sono le stesse; è lo zero a non essere noto.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Item {
    id: root

    /// Livello del segnale, in dBFS.
    required property real levelDb

    /// Fondo di rumore del canale, nella stessa unità.
    property real noiseFloorDb: -140

    /// Rapporto segnale/rumore, come lo misura il DSP.
    property real snrDb: 0

    /// Estremi della dinamica. La conversione in punti S sta in [SMeterScale].
    property real floorDb: -140
    property real ceilingDb: -20

    /// Modo e larghezza del filtro, come li scrive una radio sopra la scala.
    property string modeName: ""
    property real bandwidthHz: 0

    /// Da dove viene la misura: il nome del backend, che è anche il modo di
    /// dire con che taratura si sta leggendo.
    property string sourceLabel: ""

    /// Il canale mostrato, e il secondo se ce n'è uno.
    property string channelLabel: "RX A"
    property bool hasSecondChannel: false
    property real secondLevelDb: -140
    property string secondChannelLabel: "RX B"

    /// Le barre al posto dell'ago.
    property bool bars: false

    /// In trasmissione lo strumento non misura il segnale ricevuto: si spegne
    /// e lo dice, invece di lasciare in aria l'ultimo valore letto.
    property bool transmitting: false

    /// Come si legge il numero: 0 il picco, 1 la media, 2 il valore efficace.
    /// Cambia la lettura, non l'ago — l'ago segue sempre il segnale.
    property int readingMode: 0

    /// Quale canale mostra il quadrante: 0 solo A, 1 solo B, 2 tutti e due.
    property int channelMode: 0

    readonly property bool showsSecond: hasSecondChannel && channelMode !== 0

    implicitHeight: width * 0.58

    // ── Il segnale ───────────────────────────────────────────────────────
    readonly property real units: transmitting ? 0 : SMeterScale.units(levelDb, ceilingDb)
    readonly property bool overNine: units > 9

    /// Posizione sull'arco, da 0 a 1. I punti da S1 a S9 prendono il primo
    /// 55%, i sessanta decibel oltre S9 il resto: è la spartizione di ogni
    /// S-meter, e dare a ciascun tratto l'arco che gli spetta in decibel
    /// schiaccerebbe la parte bassa — quella dove si lavora.
    function arcFraction(u) {
        if (!isFinite(u))
            return 0
        const f = u <= 9 ? (u - 1) / 8 * 0.55
                         : 0.55 + (u - 9) * SMeterScale.dbPerUnit
                                  / SMeterScale.plusRangeDb * 0.45
        return Math.max(0, Math.min(1, f))
    }

    function fractionForLevel(db) {
        return arcFraction(SMeterScale.units(db, ceilingDb))
    }

    readonly property real targetFraction: arcFraction(units)
    readonly property real floorFraction: transmitting ? 0 : fractionForLevel(noiseFloorDb)
    readonly property real secondFraction:
        showsSecond && !transmitting ? fractionForLevel(secondLevelDb) : 0

    // ── Le tre letture ───────────────────────────────────────────────────
    //
    // Sono tre misure diverse dello stesso segnale, non tre modi di scriverlo.
    // Il picco dice quanto è arrivato forte, la media quanto è stato forte, e
    // il valore efficace media le potenze invece dei decibel — su un segnale
    // che batte i due numeri differiscono, ed è la differenza a dire se sta
    // evanescendo o se è costante.
    property real peakLevelDb: -160
    property real peakHeldFor: 0
    property real averageLevelDb: -160
    property real rmsPower: 0

    readonly property real readingDb: {
        if (transmitting)
            return levelDb
        switch (readingMode) {
        case 1:  return averageLevelDb
        case 2:  return rmsPower > 0 ? 10 * Math.log(rmsPower) / Math.LN10 : floorDb
        default: return peakLevelDb
        }
    }

    readonly property string readout: SMeterScale.readout(readingDb, ceilingDb)

    // ── Ballistica ───────────────────────────────────────────────────────
    //
    // L'ago è una massa su una molla, smorzata poco sotto il critico: sale in
    // un sesto di secondo e rientra con un soprassalto appena visibile. È il
    // comportamento di uno strumento vero, e serve a leggerlo — un ago che si
    // ferma di colpo sembra sempre fermo.
    property real needleFraction: 0
    property real needleSpeed: 0
    property real peakFraction: 0

    Timer {
        interval: 16
        running: root.visible
        repeat: true

        onTriggered: {
            const dt = interval / 1000
            const omega = 20
            const damping = 0.8

            root.needleSpeed += (omega * omega * (root.targetFraction - root.needleFraction)
                                 - 2 * damping * omega * root.needleSpeed) * dt
            root.needleFraction = Math.max(0, Math.min(1.02,
                                           root.needleFraction + root.needleSpeed * dt))

            // Il picco tiene tre secondi — il tempo di guardare lo strumento
            // dopo che la stazione ha finito di parlare — poi rientra piano.
            const level = root.transmitting ? root.floorDb : root.levelDb
            if (level >= root.peakLevelDb) {
                root.peakLevelDb = level
                root.peakFraction = root.targetFraction
                root.peakHeldFor = 0
            } else {
                root.peakHeldFor += dt
                if (root.peakHeldFor > 3) {
                    const k = 1 - Math.exp(-dt / 1.5)
                    root.peakLevelDb += (level - root.peakLevelDb) * k
                    root.peakFraction += (root.targetFraction - root.peakFraction) * k
                }
            }

            const slow = 1 - Math.exp(-dt / 1.2)
            root.averageLevelDb += (level - root.averageLevelDb) * slow

            // Il valore efficace media le potenze, non i decibel: sono due
            // conti diversi e danno due numeri diversi.
            const power = Math.pow(10, level / 10)
            root.rmsPower += (power - root.rmsPower) * slow
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        clip: true

        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.lcdGlowCenter }
            GradientStop { position: 0.55; color: Theme.lcdGlowMid }
            GradientStop { position: 1.0; color: Theme.lcdGlowEdge }
        }

        // ── Il quadrante ─────────────────────────────────────────────────
        Canvas {
            id: dial

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: display.top
            anchors.margins: Theme.spacingTight
            antialiasing: true

            Connections {
                target: root
                function onNeedleFractionChanged() { dial.requestPaint() }
                function onPeakFractionChanged() { dial.requestPaint() }
                function onFloorFractionChanged() { dial.requestPaint() }
                function onSecondFractionChanged() { dial.requestPaint() }
                function onBarsChanged() { dial.requestPaint() }
                function onTransmittingChanged() { dial.requestPaint() }
                function onShowsSecondChanged() { dial.requestPaint() }
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                if (width <= 0 || height <= 0)
                    return

                // Apertura di cento gradi, perno in basso dentro il riquadro:
                // è il quadrante di uno strumento a settore, e il perno si
                // vede — un ago che spunta dal bordo non è uno strumento.
                const cx = width / 2
                const half = 50 * Math.PI / 180
                const a0 = -Math.PI / 2 - half
                const a1 = -Math.PI / 2 + half
                const ang = (f) => a0 + (a1 - a0) * Math.max(0, Math.min(1.02, f))

                const cos = Math.cos
                const sin = Math.sin
                const labelSpace = Math.max(11, height * 0.11)
                const corner = Math.max(9, height * 0.10)

                // Il raggio più grande che sta nel riquadro, contando i numeri
                // sopra l'arco, la riga delle scritte in alto e il perno.
                const r = Math.max(12, Math.min(
                        (height - labelSpace - corner - 4) / (1 - cos(half) * 0.55),
                        (width / 2 - labelSpace - 10) / sin(half)))
                const cy = r * 0.98 + labelSpace + corner

                const dim = root.transmitting ? 0.25 : 1
                const at = (radius, f) => [cx + radius * cos(ang(f)), cy + radius * sin(ang(f))]

                const wedge = (r0, r1, f0, f1, color, alpha) => {
                    ctx.beginPath()
                    ctx.arc(cx, cy, r1, ang(f0), ang(f1))
                    ctx.arc(cx, cy, r0, ang(f1), ang(f0), true)
                    ctx.closePath()
                    ctx.fillStyle = color
                    ctx.globalAlpha = alpha
                    ctx.fill()
                    ctx.globalAlpha = 1
                }

                // La fascia oltre S9: da lì in poi il ricevitore comprime, e
                // il fondo del quadrante lo dice prima ancora dei numeri.
                wedge(r * 0.99, r * 1.11, 0.55, 1, Theme.lcdAlert, 0.12)

                // Il cuneo fra il fondo di rumore e il segnale.
                if (!root.transmitting && root.needleFraction > root.floorFraction)
                    wedge(r * 0.72, r * 0.98, root.floorFraction, root.needleFraction,
                          Theme.meterReadout, 0.15)

                // ── Scala esterna: i punti S ─────────────────────────────
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"

                const tick = (radius, f, len, color, lineWidth) => {
                    const p0 = at(radius, f)
                    const p1 = at(radius + len, f)
                    ctx.beginPath()
                    ctx.moveTo(p0[0], p0[1])
                    ctx.lineTo(p1[0], p1[1])
                    ctx.strokeStyle = color
                    ctx.lineWidth = lineWidth
                    ctx.stroke()
                }
                const label = (radius, f, text, color, font) => {
                    const p = at(radius, f)
                    ctx.font = font
                    ctx.fillStyle = color
                    ctx.fillText(text, p[0], p[1])
                }

                const scaleFont = Math.round(Theme.fontSmall) + "px " + Theme.monoFamily
                const smallFont = Math.max(8, Theme.fontSmall - 2) + "px " + Theme.monoFamily

                ctx.beginPath()
                ctx.arc(cx, cy, r, ang(0), ang(1))
                ctx.strokeStyle = Theme.meterScale
                ctx.lineWidth = 1
                ctx.stroke()

                for (let s = 1; s <= 9; ++s) {
                    const f = root.arcFraction(s)
                    tick(r, f, r * 0.075, Theme.lcdEtch, s % 2 === 1 ? 1.6 : 1)
                    if (s % 2 === 1)
                        label(r + labelSpace * 0.92, f, String(s), Theme.lcdEtch, scaleFont)
                }
                for (let over = 10; over <= 60; over += 10) {
                    const f = root.arcFraction(9 + over / SMeterScale.dbPerUnit)
                    tick(r, f, r * 0.075, Theme.lcdAlert, 1.3)
                    if (over % 20 === 0)
                        label(r + labelSpace * 0.92, f, "+" + over, Theme.lcdAlert, scaleFont)
                }

                // ── Scala interna: i decibel ─────────────────────────────
                //
                // I sei valori cadono su S1, S5, S9 e sui tre gradini oltre:
                // sono gli stessi punti della scala esterna, letti nell'altra
                // unità. Averli entrambi vuol dire non dover convertire.
                //
                // Il raggio è quello che tiene i sei numeri separati: più
                // dentro l'arco si accorcia, e «+40» finisce addosso a «+60».
                const inner = r * 0.80
                for (const u of [1, 5, 9, 9 + 20 / SMeterScale.dbPerUnit,
                                 9 + 40 / SMeterScale.dbPerUnit,
                                 9 + 60 / SMeterScale.dbPerUnit]) {
                    const f = root.arcFraction(u)
                    tick(inner, f, r * 0.05, Theme.meterScale, 1)
                    label(inner - r * 0.08, f,
                          String(Math.round(SMeterScale.levelFor(u, root.ceilingDb))),
                          Theme.lcdEtchDim, smallFont)
                }
                // L'unità della scala interna, sull'asse verticale fra i
                // numeri e il perno: più in basso finirebbe sopra il perno,
                // che è dove va a morire l'ago.
                label(r * 0.60, 0.5, "dBFS", Theme.lcdEtchDim, smallFont)

                // Il cursore del fondo di rumore, sul bordo esterno.
                if (!root.transmitting) {
                    const fa = ang(root.floorFraction)
                    const tipR = r + r * 0.085
                    ctx.beginPath()
                    ctx.moveTo(cx + tipR * cos(fa), cy + tipR * sin(fa))
                    ctx.lineTo(cx + (tipR + 6) * cos(fa - 0.022), cy + (tipR + 6) * sin(fa - 0.022))
                    ctx.lineTo(cx + (tipR + 6) * cos(fa + 0.022), cy + (tipR + 6) * sin(fa + 0.022))
                    ctx.closePath()
                    ctx.fillStyle = Theme.meterReadout
                    ctx.fill()
                }

                if (root.bars) {
                    // ── Barre ────────────────────────────────────────────
                    const band = (radius, thickness, count, lit, peak, colorAt, offAlpha) => {
                        const step = (a1 - a0) / count
                        for (let i = 0; i < count; ++i) {
                            const f = (i + 0.5) / count
                            const a = a0 + step * (i + 0.5)
                            const ca = cos(a)
                            const sa = sin(a)
                            ctx.beginPath()
                            ctx.moveTo(cx + radius * ca, cy + radius * sa)
                            ctx.lineTo(cx + (radius + thickness) * ca, cy + (radius + thickness) * sa)
                            ctx.lineWidth = Math.max(2, step * radius * 0.7)
                            if (f <= lit) {
                                ctx.strokeStyle = colorAt(f)
                                ctx.globalAlpha = dim
                            } else {
                                ctx.strokeStyle = Theme.meterUnlit
                                ctx.globalAlpha = offAlpha
                            }
                            ctx.stroke()
                            ctx.globalAlpha = 1
                        }

                        if (peak > 0.02) {
                            const i = Math.min(count - 1, Math.floor(peak * count))
                            const a = a0 + step * (i + 0.5)
                            const ca = cos(a)
                            const sa = sin(a)
                            ctx.beginPath()
                            ctx.moveTo(cx + radius * ca, cy + radius * sa)
                            ctx.lineTo(cx + (radius + thickness) * ca, cy + (radius + thickness) * sa)
                            ctx.strokeStyle = Theme.meterCaution
                            ctx.lineWidth = Math.max(2, step * radius * 0.7)
                            ctx.stroke()
                        }
                    }

                    const sColor = (f) => f < 0.55 ? Theme.meterSafe : Theme.lcdAlert
                    const showA = root.channelMode !== 1
                    band(r * 0.76, r * 0.19, 36, showA ? root.needleFraction : 0,
                         showA && !root.transmitting ? root.peakFraction : 0, sColor, 1)

                    if (root.showsSecond)
                        band(r * 0.44, r * 0.12, 26, root.secondFraction, 0,
                             () => Theme.meterReadout, 0.6)
                } else {
                    // ── Ago ──────────────────────────────────────────────
                    //
                    // Quello del picco resta indietro, sottile e ambra: due
                    // aghi sullo stesso quadrante sono la lettura di un
                    // wattmetro a picco, e qui dicono la stessa cosa.
                    if (!root.transmitting && root.peakFraction > 0.02) {
                        const pa = ang(root.peakFraction)
                        ctx.beginPath()
                        ctx.moveTo(cx + r * 0.28 * cos(pa), cy + r * 0.28 * sin(pa))
                        ctx.lineTo(cx + r * 1.05 * cos(pa), cy + r * 1.05 * sin(pa))
                        ctx.strokeStyle = Theme.meterCaution
                        ctx.lineWidth = 1.3
                        ctx.globalAlpha = 0.55
                        ctx.stroke()
                        ctx.globalAlpha = 1
                    }

                    const na = ang(root.needleFraction)
                    ctx.beginPath()
                    ctx.moveTo(cx + r * 0.22 * cos(na), cy + r * 0.22 * sin(na))
                    ctx.lineTo(cx + r * 1.06 * cos(na), cy + r * 1.06 * sin(na))
                    ctx.strokeStyle = Theme.lcdNeedle
                    ctx.lineWidth = 1.8
                    ctx.globalAlpha = dim
                    ctx.stroke()
                    ctx.globalAlpha = 1

                    // Il perno si appoggia in basso, non al centro degli
                    // archi: il centro sta sotto il riquadro — è così che si
                    // ottiene un settore invece di un semicerchio — e un ago
                    // che esce dal bordo senza perno non è uno strumento.
                    const pivotR = Math.max(5, r * 0.075)
                    const pivotY = Math.min(cy, height - pivotR - 1)
                    ctx.beginPath()
                    ctx.arc(cx, pivotY, pivotR, 0, 2 * Math.PI)
                    ctx.fillStyle = Theme.surfaceRaised
                    ctx.fill()
                    ctx.strokeStyle = Theme.borderStrong
                    ctx.lineWidth = 1
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.arc(cx, pivotY, Math.max(2, r * 0.022), 0, 2 * Math.PI)
                    ctx.fillStyle = Theme.lcdPivot
                    ctx.fill()
                }

                // ── Le scritte agli angoli ───────────────────────────────
                ctx.font = "bold " + Math.max(8, Theme.fontSmall - 2) + "px " + Theme.monoFamily
                ctx.textBaseline = "top"
                ctx.textAlign = "left"
                ctx.fillStyle = Theme.lcdEtchDim
                ctx.fillText("S-UNIT · IARU R1", 2, 1)
                ctx.textAlign = "right"
                ctx.fillText(root.channelLabel, width - 2, 1)

                // Modo e filtro in basso a sinistra, dove non coprono la
                // scala: sono la cornice della misura, non la misura.
                ctx.textAlign = "left"
                ctx.textBaseline = "bottom"
                ctx.fillStyle = Theme.lcdEtch
                ctx.fillText(root.modeName, 2, height - 1)
                if (root.bandwidthHz > 0) {
                    ctx.textAlign = "right"
                    ctx.fillStyle = Theme.lcdEtchDim
                    ctx.fillText((root.bandwidthHz / 1000).toFixed(1) + "k", width - 2, height - 1)
                }

                if (root.bars && root.showsSecond) {
                    ctx.textAlign = "left"
                    ctx.textBaseline = "middle"
                    ctx.fillStyle = Theme.meterReadout
                    const p = at(r * 0.44, 0)
                    ctx.fillText(root.secondChannelLabel, Math.max(2, p[0] - 4), p[1] + 10)
                }
            }
        }

        // ── La lettura ───────────────────────────────────────────────────
        Rectangle {
            id: display

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: chips.top
            anchors.margins: Theme.spacingTight
            implicitHeight: readoutGrid.implicitHeight + 2 * Theme.spacingTight
            radius: Theme.radiusSmall
            color: Theme.meterDisplayBackground
            border.width: 1
            border.color: Theme.border

            GridLayout {
                id: readoutGrid

                anchors.fill: parent
                anchors.margins: Theme.spacingTight
                columns: 2
                columnSpacing: Theme.spacing
                rowSpacing: 1

                // Da dove viene la misura, e come la si sta leggendo: le due
                // cose che cambiano il significato dei numeri sotto.
                Text {
                    text: root.sourceLabel
                    font.pixelSize: Math.max(8, Theme.fontSmall - 2)
                    font.family: Theme.monoFamily
                    color: Theme.lcdEtchDim
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    horizontalAlignment: Text.AlignRight
                    text: root.modeNames[root.readingMode] + " · " + root.channelNames[root.channelMode]
                    font.pixelSize: Math.max(8, Theme.fontSmall - 2)
                    font.family: Theme.monoFamily
                    color: Theme.lcdEtchDim
                }

                Text {
                    text: root.transmitting ? "—" : root.readout
                    font.pixelSize: Theme.fontLarge
                    font.family: Theme.monoFamily
                    font.bold: true
                    color: root.overNine ? Theme.lcdAlert : Theme.meterCaution
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: root.transmitting ? "" : root.readingDb.toFixed(1) + " dBFS"
                    font.pixelSize: Theme.fontNormal
                    font.family: Theme.monoFamily
                    font.bold: true
                    color: Theme.meterReadout
                }

                Text {
                    text: root.transmitting ? "" : qsTr("SNR %1 dB").arg(Math.round(root.snrDb))
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.lcdEtch
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    // Il fondo di rumore, che è l'altra metà della misura: un
                    // S5 su un fondo basso si copia, lo stesso S5 su un fondo
                    // alto no.
                    text: root.transmitting
                          ? qsTr("in trasmissione")
                          : qsTr("FONDO %1 dBFS").arg(Math.round(root.noiseFloorDb))
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: root.transmitting ? Theme.transmit : Theme.lcdEtchDim
                }
            }
        }

        // ── I tasti della lettura ────────────────────────────────────────
        RowLayout {
            id: chips

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingTight
            spacing: Theme.spacingTight

            Repeater {
                model: root.modeNames

                delegate: DecoMeterChip {
                    required property int index
                    required property string modelData

                    Layout.fillWidth: true
                    text: modelData
                    current: root.readingMode === index
                    onPressed: root.readingMode = index
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            Repeater {
                model: root.channelNames

                delegate: DecoMeterChip {
                    required property int index
                    required property string modelData

                    Layout.fillWidth: true
                    text: modelData
                    current: root.channelMode === index
                    // Senza un secondo canale non c'è niente da mostrare in B:
                    // il tasto si spegne invece di offrire una vista vuota
                    // (CONSTITUTION §7).
                    enabled: index === 0 || root.hasSecondChannel
                    onPressed: root.channelMode = index
                }
            }
        }

        // ── Il vetro ─────────────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.lcdSheen }
                GradientStop { position: 0.35; color: Theme.lcdSheenSoft }
                GradientStop { position: 0.62; color: "transparent" }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: Theme.lcdRim
        }
    }

    /// I nomi delle tre letture e dei tre modi di canale, in un posto solo:
    /// li usano i tasti e la riga di stato del display.
    readonly property var modeNames: ["PK", "AVG", "RMS"]
    readonly property var channelNames: ["A", "B", "A+B"]
}
