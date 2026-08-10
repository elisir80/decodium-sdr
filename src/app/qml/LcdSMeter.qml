// SPDX-License-Identifier: GPL-3.0-or-later
// Il quadrante dello strumento, disegnato come il vetro di un apparato.
//
// Non è decorazione. Una barra dice quanto vale il segnale adesso; una
// lancetta dice anche *come si sta muovendo*, e su una radio è quest'ultima
// l'informazione che serve — vedere un segnale evanescere mentre si decide se
// chiamare, o vedere il QSB battere regolare. Il fondo scuro e i riflessi
// servono allo stesso scopo: portano il contrasto sulla lancetta, che è la
// cosa da guardare, e tolgono importanza a tutto il resto.
//
// Il quadrante si dipinge una volta sola su un Canvas: la scala non cambia mai
// e ridipingerla a ogni aggiornamento del livello significherebbe rasterizzare
// quindici volte al secondo qualcosa di immobile. Si muovono solo la lancetta
// e la tacca del picco, che sono due rettangoli ruotati.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Livello del segnale, in dBFS.
    required property real levelDb

    /// Estremi della dinamica. Gli stessi degli altri strumenti: la
    /// conversione in punti S sta in [SMeterScale], non qui.
    property real floorDb: -140
    property real ceilingDb: -20

    /// Contorno, quando c'è: fondo di rumore e rapporto segnale/rumore del
    /// canale. Sono le due misure che dicono se un S5 è un buon S5.
    property real noiseFloorDb: -140
    property real snrDb: 0

    /// Modo e larghezza del filtro, come li mostra la radio sopra la scala.
    property string modeName: ""
    property real bandwidthHz: 0

    /// In trasmissione lo strumento non misura il segnale ricevuto: si spegne
    /// e lo dice, invece di lasciare in aria l'ultimo valore letto — che
    /// resterebbe lì, plausibile e falso.
    property bool transmitting: false

    // Il riquadro è più basso che largo, nella proporzione del vetro di un
    // apparato: dare più altezza non fa crescere il quadrante — il raggio lo
    // decide comunque la larghezza — lascia solo aria morta sotto il perno.
    implicitHeight: width * 0.66

    // ── Geometria ────────────────────────────────────────────────────────
    //
    // Apertura, perno e raggio nelle proporzioni di uno strumento vero: il
    // perno sotto la scala, e della corona si vede solo la parte alta.
    readonly property real sweep: 58
    readonly property real sweepRad: sweep * Math.PI / 180

    readonly property real pivotX: width / 2
    readonly property real pivotY: height * 0.84

    /// Quanto stanno fuori dall'arco le etichette.
    readonly property real labelGap: Math.max(9, height * 0.05)

    /// Spazio riservato in cima alla riga di stato.
    readonly property real topReserve: Theme.fontSmall + 2 * Theme.spacingTight

    /// Il raggio è il più grande che sta dentro il riquadro, di larghezza e di
    /// altezza. I `Math.max` non sono prudenza sprecata: con il pannello
    /// chiuso, o durante il primo layout, altezza e larghezza valgono zero e
    /// senza di essi il raggio diventerebbe negativo — e un arco di raggio
    /// negativo non disegna niente e non dà alcun errore.
    readonly property real radius: Math.max(8, Math.min(
            (width / 2 - 16) / Math.sin(sweepRad) - labelGap,
            pivotY - labelGap - topReserve))

    // ── Il segnale ───────────────────────────────────────────────────────
    readonly property real units: transmitting ? 0 : SMeterScale.units(levelDb, ceilingDb)
    readonly property string readout: SMeterScale.readout(levelDb, ceilingDb)
    readonly property bool overNine: units > 9

    /// Punto dell'arco, da 0 a 1, in cui cade un valore in punti S.
    ///
    /// I due tratti non hanno la stessa scala e non è un errore: sotto S9 un
    /// punto vale sei decibel, sopra ne vale dieci, e dare a ciascun tratto
    /// l'arco che gli spetta in decibel schiaccerebbe la parte bassa — quella
    /// dove si lavora — in un angolo di quadrante.
    function arcFraction(u) {
        if (!isFinite(u))
            return 0
        const f = u <= 9 ? 0.06 + (u - 1) / 8 * 0.52
                         : 0.58 + (u - 9) * SMeterScale.dbPerUnit
                                  / SMeterScale.plusRangeDb * 0.42
        return Math.max(0, Math.min(1, f))
    }

    /// Angolo della lancetta per un valore in punti S.
    function angleFor(u) {
        return -sweep + arcFraction(u) * 2 * sweep
    }

    // ── Picco ────────────────────────────────────────────────────────────
    //
    // Il massimo recente, che scende piano. Su un segnale che batte, la
    // lancetta da sola non basta: quando si è finito di leggerla il picco è
    // già passato.
    property real peakUnits: 0

    onUnitsChanged: if (units > peakUnits) peakUnits = units

    Timer {
        interval: 90
        running: root.visible
        repeat: true
        // Circa un punto S al secondo: abbastanza lento da poterlo leggere,
        // abbastanza svelto da non restare indietro quando la banda cambia.
        onTriggered: root.peakUnits = Math.max(root.units, root.peakUnits - 0.09)
    }

    // ── Il vetro ─────────────────────────────────────────────────────────
    Rectangle {
        id: glass

        anchors.fill: parent
        radius: Theme.radius
        clip: true

        // Retroilluminazione: più chiara verso l'alto, dove sta la scala.
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.lcdGlowCenter }
            GradientStop { position: 0.45; color: Theme.lcdGlowMid }
            GradientStop { position: 1.0; color: Theme.lcdGlowEdge }
        }

        // ── Riga di stato, sopra la scala ────────────────────────────────
        Row {
            id: statusRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingTight
            spacing: Theme.spacing

            Text {
                text: root.modeName
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                font.bold: true
                color: Theme.lcdEtch
            }

            Text {
                // La larghezza del filtro, come la scrive una radio: 2.4k.
                visible: root.bandwidthHz > 0
                text: qsTr("BW %1k").arg((root.bandwidthHz / 1000).toFixed(1))
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.lcdEtchDim
            }
        }

        Rectangle {
            // In trasmissione il riquadro rosso prende il posto della lettura:
            // è la stessa spia che ha ogni apparato, e vuol dire «quello che
            // vedi adesso non è il segnale che entra».
            visible: root.transmitting
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingTight
            width: txLabel.implicitWidth + 2 * Theme.spacingTight
            height: txLabel.implicitHeight + Theme.spacingTight
            radius: Theme.radiusSmall
            color: "transparent"
            border.width: 2
            border.color: Theme.lcdAlert

            Text {
                id: txLabel
                anchors.centerIn: parent
                text: qsTr("TX")
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                font.bold: true
                color: Theme.lcdAlert
            }
        }

        // ── Il quadrante ─────────────────────────────────────────────────
        Canvas {
            id: dial

            anchors.fill: parent
            antialiasing: true

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()

                const cx = root.pivotX
                const cy = root.pivotY
                const r = root.radius
                // Il Canvas conta gli angoli da destra; lo strumento li conta
                // dalla verticale.
                const toRad = (deg) => (deg - 90) * Math.PI / 180
                const at = (u) => toRad(root.angleFor(u))

                // Arco di fondo: chiaro fino a S9, in tinta d'allarme oltre,
                // perché oltre S9 il ricevitore sta comprimendo e va visto.
                ctx.lineWidth = 1.6
                ctx.strokeStyle = Theme.lcdEtch
                ctx.globalAlpha = 0.85
                ctx.beginPath()
                ctx.arc(cx, cy, r, at(0), at(9))
                ctx.stroke()

                ctx.strokeStyle = Theme.lcdAlert
                ctx.beginPath()
                ctx.arc(cx, cy, r, at(9), at(19))
                ctx.stroke()
                ctx.globalAlpha = 1

                ctx.font = Math.round(Theme.fontSmall) + "px " + Theme.monoFamily
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"

                // Tacche: una per punto S fino a nove, poi una ogni dieci
                // decibel. Etichettate le dispari sotto S9 — è la convenzione
                // di ogni S-meter — e le decine pari sopra, che a quel raggio
                // sono le uniche che non si toccano fra loro.
                const marks = []
                for (let s = 1; s <= 9; ++s)
                    marks.push({ u: s, major: s % 2 === 1, over: false,
                                 label: s % 2 === 1 ? String(s) : "" })
                for (let k = 1; k <= 6; ++k)
                    marks.push({ u: 9 + k * 10 / SMeterScale.dbPerUnit,
                                 major: k % 2 === 0, over: true,
                                 label: k % 2 === 0 ? "+" + (k * 10) : "" })

                for (const m of marks) {
                    const a = at(m.u)
                    const len = m.major ? r * 0.09 : r * 0.05
                    const inner = r - len

                    ctx.strokeStyle = m.over ? Theme.lcdAlert : Theme.lcdEtch
                    ctx.lineWidth = m.major ? 2.2 : 1.2
                    ctx.beginPath()
                    ctx.moveTo(cx + Math.cos(a) * inner, cy + Math.sin(a) * inner)
                    ctx.lineTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r)
                    ctx.stroke()

                    if (m.label === "")
                        continue

                    // Le etichette stanno fuori dall'arco: dentro finirebbero
                    // sotto la lancetta proprio quando la si sta leggendo.
                    const lr = r + root.labelGap
                    ctx.fillStyle = m.over ? Theme.lcdAlert : Theme.lcdEtch
                    ctx.fillText(m.label, cx + Math.cos(a) * lr, cy + Math.sin(a) * lr)
                }

                // Il nome della scala, dentro l'arco e a mezza altezza fra il
                // primo gradino e il perno: appoggiato all'inizio dell'arco
                // finiva addosso alla tacca «1».
                ctx.fillStyle = Theme.lcdEtchDim
                ctx.textAlign = "center"
                ctx.fillText("S", cx + Math.cos(at(1.2)) * (r * 0.60),
                                  cy + Math.sin(at(1.2)) * (r * 0.60))
            }

            // Il tema può cambiare sotto i piedi: senza questo il quadrante
            // resterebbe con i colori vecchi finché non cambia dimensione.
            Connections {
                target: Theme
                function onLcdEtchChanged() { dial.requestPaint() }
            }
        }

        // ── Picco ────────────────────────────────────────────────────────
        Rectangle {
            id: peakMark

            visible: !root.transmitting && root.peakUnits > 0.2
            x: root.pivotX - width / 2
            y: root.pivotY - root.radius
            width: 2
            height: root.radius * 0.12
            color: root.peakUnits > 9 ? Theme.lcdAlert : Theme.lcdEtchDim
            antialiasing: true

            transform: Rotation {
                origin.x: peakMark.width / 2
                origin.y: root.radius
                angle: root.angleFor(root.peakUnits)
            }
        }

        // ── Lancetta ─────────────────────────────────────────────────────
        Rectangle {
            id: needle

            x: root.pivotX - width / 2
            y: root.pivotY - root.radius + 2
            width: 3
            height: root.radius - 2
            radius: 1.5
            antialiasing: true
            opacity: root.transmitting ? 0.25 : 1

            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.lcdNeedle }
                GradientStop { position: 1.0; color: Theme.lcdNeedleTail }
            }

            transform: Rotation {
                origin.x: needle.width / 2
                origin.y: needle.height
                angle: root.angleFor(root.units)

                // Una lancetta vera ha inerzia, e ne ha un po' troppa: torna
                // indietro appena oltre il valore e si assesta. Senza, a ogni
                // aggiornamento salta, e su un segnale che evanesce non si
                // legge più niente.
                Behavior on angle {
                    NumberAnimation {
                        duration: 110
                        easing.type: Easing.OutBack
                        easing.overshoot: 1.1
                    }
                }
            }

            Behavior on opacity {
                NumberAnimation { duration: Theme.animationFast }
            }
        }

        Rectangle {
            // Perno
            x: root.pivotX - width / 2
            y: root.pivotY - height / 2
            width: 9
            height: 9
            radius: 4.5
            color: Theme.lcdPivot
        }

        // ── Lettura ──────────────────────────────────────────────────────
        Text {
            id: mainReadout

            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing
            text: root.transmitting ? "—" : root.readout
            font.pixelSize: Theme.fontLarge
            font.family: Theme.monoFamily
            font.bold: true
            color: root.overNine ? Theme.lcdAlert : Theme.lcdEtch
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing
            horizontalAlignment: Text.AlignRight
            // Il livello assoluto e il rapporto segnale/rumore: il primo dice
            // dov'è la lancetta, il secondo se quel segnale si copierà.
            text: root.transmitting
                  ? ""
                  : Math.round(root.levelDb) + " dBFS\nS/N " + Math.round(root.snrDb) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            lineHeight: 1.1
            color: Theme.lcdEtchDim
        }

        // ── Riflessi ─────────────────────────────────────────────────────
        //
        // Non ricevono il mouse e non ridisegnano nulla: sono due gradienti
        // fermi sopra tutto il resto.
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
            // Il bordo interno del vetro.
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: Theme.lcdRim
        }
    }
}
