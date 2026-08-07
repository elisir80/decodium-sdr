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

    implicitHeight: layout.implicitHeight + 2 * Theme.spacing
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

                Behavior on color {
                    ColorAnimation { duration: Theme.animationFast }
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
            Layout.preferredHeight: root.collapsed ? 0 : implicitHeight
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
