// SPDX-License-Identifier: GPL-3.0-or-later
// Cornice comune dei pannelli: dà loro lo stesso aspetto del resto
// dell'interfaccia senza che ognuno se lo ridisegni, e la stessa capacità di
// chiudersi.
//
// Il collasso non è un vezzo: su uno schermo con poca altezza utile — un
// portatile, o un desktop con fattore di scala alto — la strip laterale deve
// ospitare sintonia, controlli del device e canali insieme. Chi opera tiene
// aperto quello che sta usando e chiude il resto.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    /// Titolo mostrato in testa al pannello.
    property string title: ""
    /// Contenuto del pannello.
    default property alias content: contentColumn.data

    /// Se falso l'intestazione non reagisce e il pannello resta sempre aperto.
    property bool collapsible: true
    property bool collapsed: false

    /// Chiave con cui si ricorda lo stato. Di default è il titolo: due
    /// pannelli con lo stesso titolo condividerebbero lo stato, ma non è un
    /// caso che si presenti.
    property string persistKey: title

    /// Il pannello si può prendere e spostare nella colonna.
    ///
    /// La cornice non sa nulla dell'ordine: si limita a dire dove è arrivato
    /// il dito, in coordinate della scena, e chi tiene la colonna decide se
    /// quel punto è sopra un altro pannello. Un componente che sapesse il
    /// proprio indice sarebbe legato al contenitore, e questo finisce anche
    /// dentro finestre staccate.
    property bool draggable: false

    /// Il pannello si può staccare in una finestra sua.
    ///
    /// Non tutti ne hanno bisogno: una pulsantiera di bande in una finestra da
    /// sola è una finestra sprecata. Lo dichiara chi il pannello lo scrive,
    /// perché è lui a sapere se ha qualcosa da fare con lo spazio.
    property bool detachable: false

    /// Vero quando il pannello *è* in una finestra sua. Chi ha contenuti che
    /// crescono lo guarda per decidere quanto prendersi.
    property bool detached: false

    signal detachRequested()
    signal dragStarted()
    signal dragMoved(real sceneY)
    signal dragEnded()

    implicitHeight: layout.implicitHeight + 2 * Theme.spacing
    // In finestra la cornice si prende tutto lo spazio che le danno.
    anchors.fill: detached ? parent : undefined
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.width: 1
    border.color: Theme.border
    clip: true

    // Aperto o chiuso, lo stato resta fra un avvio e l'altro: richiuderli
    // tutti a ogni riavvio sarebbe una piccola tassa quotidiana.
    Settings {
        category: "panels/" + root.persistKey
        property alias collapsed: root.collapsed
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: root.collapsed ? 0 : Theme.spacingTight

        // ── Intestazione ─────────────────────────────────────────────────
        RowLayout {
            id: header
            Layout.fillWidth: true
            spacing: Theme.spacing

            Rectangle {
                width: 3
                height: 12
                radius: 1.5
                color: root.collapsed ? Theme.textDisabled : Theme.accent
                visible: !root.draggable

                Behavior on color {
                    ColorAnimation { duration: Theme.animationFast }
                }
            }

            // ── Maniglia ─────────────────────────────────────────────────
            //
            // Prende il posto del trattino colorato quando il pannello si può
            // spostare: due file di punti, il segno che in ogni interfaccia
            // vuol dire «questo si prende». Ha un'area sensibile più larga di
            // quanto si veda, perché un bersaglio di sei pixel si manca.
            Item {
                visible: root.draggable
                implicitWidth: 10
                implicitHeight: 14

                Column {
                    anchors.centerIn: parent
                    spacing: 3

                    Repeater {
                        model: 3

                        delegate: Row {
                            spacing: 3

                            Repeater {
                                model: 2

                                delegate: Rectangle {
                                    width: 2
                                    height: 2
                                    radius: 1
                                    color: dragHandler.active ? Theme.accent
                                         : handleHover.hovered ? Theme.textPrimary
                                         : Theme.textDisabled
                                }
                            }
                        }
                    }
                }

                HoverHandler {
                    id: handleHover
                    cursorShape: dragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }

                // `target: null`: non si trascina la cornice, si trascina
                // l'idea di dove debba stare. Muovere davvero il pannello lo
                // staccherebbe dal layout, e al rilascio tornerebbe al suo
                // posto con uno scatto.
                DragHandler {
                    id: dragHandler
                    target: null
                    xAxis.enabled: false

                    onActiveChanged: {
                        if (active)
                            root.dragStarted()
                        else
                            root.dragEnded()
                    }

                    onCentroidChanged: {
                        if (active)
                            root.dragMoved(centroid.scenePosition.y)
                    }
                }
            }

            Text {
                text: root.title
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.letterSpacing: 0.5
                color: headerHover.hovered ? Theme.textPrimary : Theme.textSecondary
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight

                Behavior on color {
                    ColorAnimation { duration: Theme.animationFast }
                }
            }

            // ── Stacca ───────────────────────────────────────────────────
            //
            // Prima del chevron e con il suo gesto: l'intestazione tutta
            // apre e chiude, e un bersaglio dentro di essa deve prendersi il
            // proprio tocco prima che lo faccia lei.
            Rectangle {
                visible: root.detachable && !root.detached
                implicitWidth: 18
                implicitHeight: 16
                radius: Theme.radiusSmall
                color: detachHover.hovered ? Theme.surface : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "\u2197"
                    font.pixelSize: Theme.fontSmall
                    color: detachHover.hovered ? Theme.accent : Theme.textDisabled
                }

                HoverHandler {
                    id: detachHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: root.detachRequested()
                }

                ToolTip.visible: detachHover.hovered
                ToolTip.text: qsTr("Stacca in una finestra")
            }

            // Chevron: punta in basso quando è aperto, a sinistra quando è
            // chiuso. La rotazione dice da sola in che verso andrà il clic.
            Canvas {
                id: chevron

                visible: root.collapsible
                implicitWidth: 10
                implicitHeight: 6
                rotation: root.collapsed ? -90 : 0
                opacity: headerHover.hovered ? 1.0 : 0.6

                Behavior on rotation {
                    NumberAnimation {
                        duration: Theme.animationNormal
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on opacity {
                    NumberAnimation { duration: Theme.animationFast }
                }

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.lineTo(width, 0)
                    ctx.lineTo(width / 2, height)
                    ctx.closePath()
                    ctx.fillStyle = Theme.textSecondary
                    ctx.fill()
                }
            }

            // Gli handler stanno qui e non sul contenitore esterno: piu' in
            // alto coprirebbero anche il contenuto, e un clic su un pulsante
            // di banda chiuderebbe il pannello.
            HoverHandler {
                id: headerHover
                enabled: root.collapsible
                cursorShape: Qt.PointingHandCursor
            }

            // Tutta l'intestazione e' sensibile, non il solo chevron: un
            // bersaglio piccolo su un pannello che si apre e chiude spesso e'
            // una seccatura.
            TapHandler {
                enabled: root.collapsible
                onTapped: root.collapsed = !root.collapsed
            }
        }


        // ── Contenuto ────────────────────────────────────────────────────
        ColumnLayout {
            id: contentColumn

            Layout.fillWidth: true
            // Staccato il contenuto riempie la finestra; in colonna si
            // dimensiona su di sé, perché lì l'altezza la decide il contenuto
            // e non il contenitore.
            Layout.fillHeight: root.detached
            Layout.preferredHeight: root.detached ? -1
                                  : (root.collapsed ? 0 : implicitHeight)
            spacing: Theme.spacingTight
            opacity: root.collapsed ? 0 : 1
            visible: opacity > 0
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: Theme.animationNormal
                    easing.type: Easing.OutCubic
                }
            }
            Behavior on opacity {
                NumberAnimation { duration: Theme.animationFast }
            }
        }
    }
}
