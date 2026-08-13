// SPDX-License-Identifier: GPL-3.0-or-later
// Com'è messa la banda, rispetto a com'è di solito.
//
// **Il numero da solo non dice niente.** «Fondo a −98 dB» è un fatto senza
// conseguenze: alto o basso rispetto a cosa? Chi ascolta lo sa per le bande
// che frequenta da anni, e non lo sa per le altre — che sono quelle su cui
// servirebbe saperlo.
//
// Qui il fondo di adesso sta sopra il fondo tipico di quest'ora, e la
// differenza fra le due curve è la risposta. Non c'è nessun altro modo di
// dare quella risposta: nessun servizio in rete conosce il rumore **della tua
// stazione**, che è fatto per metà di propagazione e per metà del quartiere.
//
// **Ventiquattro ore in orizzontale, non «le ultime N».** Il rumore su HF ha
// un andamento giornaliero che si ripete: le sedici del pomeriggio somigliano
// alle sedici di ieri molto più che alle quattro di stanotte. Un asse che
// scorre lo nasconderebbe; un asse fisso da mezzanotte a mezzanotte lo mostra.
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("CONDIZIONI")
    persistKey: "condizioni"
    draggable: true
    collapsed: true

    readonly property var registry: Session.conditions

    /// I due estremi verticali, ricavati dai dati e non fissi: il fondo dei
    /// centosessanta e quello dei dieci stanno a trenta decibel di distanza, e
    /// una scala buona per uno schiaccia l'altro contro il bordo.
    property real floorDb: -120
    property real ceilingDb: -80

    function rescale() {
        let low = 1e9
        let high = -1e9
        const series = [registry ? registry.today : [],
                        registry ? registry.typical : [],
                        registry && isFinite(registry.nowDb) ? [registry.nowDb] : []]
        for (let s = 0; s < series.length; ++s) {
            for (let i = 0; i < series[s].length; ++i) {
                const v = series[s][i]
                if (!isFinite(v))
                    continue
                low = Math.min(low, v)
                high = Math.max(high, v)
            }
        }
        if (low > high)
            return
        // Un margine di tre decibel sopra e sotto: una curva che tocca il
        // bordo si legge come una curva tagliata.
        const pad = Math.max(3, (high - low) * 0.15)
        root.floorDb = low - pad
        root.ceilingDb = high + pad
    }

    Connections {
        target: root.registry
        function onChanged() { root.rescale() }
    }

    Component.onCompleted: rescale()

    // ── L'intestazione ───────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Text {
            text: root.registry && root.registry.onBand
                  ? root.registry.bandName
                  : qsTr("fuori banda")
            font.pixelSize: Theme.fontNormal
            font.bold: true
            font.family: Theme.monoFamily
            color: root.registry && root.registry.onBand ? Theme.accent
                                                         : Theme.textDisabled
        }

        Item { Layout.fillWidth: true }

        // Lo scostamento: il numero che risponde alla domanda. Positivo vuol
        // dire più rumorosa del solito, e il segno si scrive per esteso perché
        // «+4 dB» e «4 dB» si confondono in fretta.
        Text {
            visible: root.registry && root.registry.hasDeparture
            text: {
                const d = root.registry.departureDb
                if (Math.abs(d) < 1.5)
                    return qsTr("come al solito")
                return d > 0 ? qsTr("%1 dB più rumorosa del solito").arg(d.toFixed(0))
                             : qsTr("%1 dB più quieta del solito").arg((-d).toFixed(0))
            }
            font.pixelSize: Theme.fontSmall
            font.bold: true
            color: {
                const d = root.registry.departureDb
                if (Math.abs(d) < 1.5)
                    return Theme.textSecondary
                return d > 0 ? Theme.danger : Theme.success
            }
        }

        Text {
            visible: root.registry && !root.registry.hasDeparture
            text: root.registry && root.registry.typicalDays > 0
                  ? qsTr("ancora nessuna misura in questo quarto d'ora")
                  : qsTr("il confronto arriva domani")
            font.pixelSize: Theme.fontSmall
            color: Theme.textDisabled
        }
    }

    // ── Il grafico ───────────────────────────────────────────────────────
    Item {
        id: chart

        Layout.fillWidth: true
        Layout.preferredHeight: 116

        readonly property real span: Math.max(1, root.ceilingDb - root.floorDb)

        function xFor(bucket) {
            return plot.width * bucket / 95
        }

        function yFor(db) {
            return plot.height * (1 - (db - root.floorDb) / chart.span)
        }

        /// Da una serie di novantasei valori a un elenco di tratti continui.
        ///
        /// Tratti e non una polilinea sola: i quarti d'ora senza misura sono
        /// buchi, e tirare una riga da prima del buco a dopo sarebbe
        /// un'interpolazione che nessuno ha misurato — e che sullo schermo
        /// sembrerebbe un dato come gli altri.
        function segmentsOf(values) {
            const out = []
            let run = []
            for (let i = 0; i < values.length; ++i) {
                if (isFinite(values[i])) {
                    run.push(Qt.point(xFor(i), yFor(values[i])))
                } else if (run.length > 0) {
                    if (run.length > 1)
                        out.push(run)
                    run = []
                }
            }
            if (run.length > 1)
                out.push(run)
            return out
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border
            radius: 3
        }

        // Le ore. Ogni sei, che è il passo con cui si ragiona: alba, mezzodì,
        // tramonto, mezzanotte.
        Repeater {
            model: [6, 12, 18]

            delegate: Item {
                required property int modelData

                Rectangle {
                    x: plot.x + chart.xFor(parent.modelData * 4)
                    y: plot.y
                    width: 1
                    height: plot.height
                    color: Theme.border
                }

                Text {
                    x: plot.x + chart.xFor(parent.modelData * 4) + 3
                    y: plot.y + 2
                    text: qsTr("%1").arg(parent.modelData)
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textDisabled
                }
            }
        }

        Item {
            id: plot

            anchors.fill: parent
            anchors.margins: 5
            clip: true

            // Adesso: una riga verticale che dice dove si è nella giornata.
            Rectangle {
                visible: root.registry && root.registry.onBand
                x: chart.xFor(root.registry ? root.registry.currentBucket : 0)
                width: 1
                height: parent.height
                color: Theme.accent
                opacity: 0.4
            }

            // Il quarto d'ora in corso: un punto, non un tratto. È una misura
            // che si sta ancora formando, e disegnarla come la curva la
            // farebbe sembrare finita.
            Rectangle {
                visible: root.registry && isFinite(root.registry.nowDb)
                width: 5
                height: 5
                radius: 2.5
                color: Theme.spectrumPeak
                x: chart.xFor(root.registry ? root.registry.currentBucket : 0)
                   - width / 2
                y: chart.yFor(root.registry ? root.registry.nowDb : 0) - height / 2
            }

            // Il tipico, sotto e spento: è il riferimento, non il risultato.
            Repeater {
                model: root.registry ? chart.segmentsOf(root.registry.typical) : []

                delegate: Shape {
                    required property var modelData

                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: Theme.textDisabled
                        strokeWidth: 1.4
                        fillColor: "transparent"
                        PathPolyline { path: parent.parent.modelData }
                    }
                }
            }

            // Oggi, acceso.
            Repeater {
                model: root.registry ? chart.segmentsOf(root.registry.today) : []

                delegate: Shape {
                    required property var modelData

                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 1.8
                        fillColor: "transparent"
                        PathPolyline { path: parent.parent.modelData }
                    }
                }
            }
        }

        // La scala verticale, ai due estremi. In mezzo non serve: quello che
        // si legge è la distanza fra le due curve, non il valore assoluto.
        Text {
            x: plot.x + 4
            y: plot.y + 1
            text: qsTr("%1").arg(Math.round(root.ceilingDb))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textDisabled
        }

        Text {
            x: plot.x + 4
            y: plot.y + plot.height - height - 1
            text: qsTr("%1 dB").arg(Math.round(root.floorDb))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textDisabled
        }

        // A registro vuoto il riquadro non resta muto: dire che si sta
        // riempiendo costa una riga e toglie il dubbio che sia rotto.
        Text {
            anchors.centerIn: parent
            visible: !root.registry || !root.registry.onBand
            text: qsTr("il registro annota solo dentro le bande amatoriali")
            font.pixelSize: Theme.fontSmall
            color: Theme.textDisabled
        }
    }

    // ── La legenda ───────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Rectangle { width: 14; height: 2; color: Theme.accent }
        Text {
            text: qsTr("oggi")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Rectangle { width: 5; height: 5; radius: 2.5; color: Theme.spectrumPeak }
        Text {
            text: qsTr("adesso")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Rectangle { width: 14; height: 2; color: Theme.textDisabled }
        Text {
            Layout.fillWidth: true
            text: root.registry && root.registry.typicalDays > 0
                  ? qsTr("il solito · %n giorn%1", "", root.registry.typicalDays)
                        .arg(root.registry.typicalDays === 1 ? "o" : "i")
                  : qsTr("il solito · niente ancora")
            font.pixelSize: Theme.fontSmall
            color: Theme.textDisabled
            elide: Text.ElideRight
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Il fondo è riferito all'antenna: quello che la guardia toglie viene tolto anche qui, altrimenti il grafico racconterebbe il proprio AGC. Ricevitori diversi non si confrontano.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
