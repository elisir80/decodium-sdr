// SPDX-License-Identifier: GPL-3.0-or-later
// Comandi della resa del waterfall, sovrapposti al panadattatore.
//
// Stanno sopra lo spettro e non in un pannello laterale perché si regolano
// guardando l'effetto: spostare un cursore e dover cercare con gli occhi da
// un'altra parte cosa è cambiato rende impossibile trovare il punto giusto.
// Da chiusi restano una barra sottile, per non rubare banda alla vista.
import QtQuick
import QtQuick.Controls.Basic
import QtCore
import DecodiumSdr

Rectangle {
    id: root

    /// Il panadattatore da comandare.
    required property PanadapterView panadapter

    property bool expanded: false

    readonly property bool showRelief: panadapter.waterfallMode === PanadapterView.Relief

    width: expanded ? 208 : compactRow.implicitWidth + 2 * Theme.spacing
    height: expanded ? full.implicitHeight + 2 * Theme.spacing
                     : Theme.controlHeight + 2 * Theme.spacingTight
    radius: Theme.radiusSmall
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.86)
    border.width: 1
    border.color: Theme.border
    clip: true

    Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

    // Le preferenze non possono essere alias verso `panadapter`: un alias QML
    // arriva solo a `id.proprietà`, non a una proprietà di una proprietà. Si
    // ripristina all'avvio e si salva a ogni modifica, con `restored` a fare
    // da guardia perché i binding non scrivano i valori di fabbrica sopra
    // quelli dell'utente prima di averli letti.
    property bool restored: false

    Settings {
        id: prefs
        category: "waterfall"
        property alias expanded: root.expanded
        property int paletteIndex: 0
        property real gamma: 0.85
        property real blackThreshold: 0.06
        property bool autoRange: false
        property int waterfallMode: PanadapterView.Flat
        property real tilt: 58
        property real rotation3d: 0
        property real reliefScale: 0.45
    }

    Component.onCompleted: {
        panadapter.paletteIndex = prefs.paletteIndex
        panadapter.gamma = prefs.gamma
        panadapter.blackThreshold = prefs.blackThreshold
        panadapter.autoRange = prefs.autoRange
        panadapter.waterfallMode = prefs.waterfallMode
        panadapter.tilt = prefs.tilt
        panadapter.rotation3d = prefs.rotation3d
        panadapter.reliefScale = prefs.reliefScale
        restored = true
    }

    // ── Barra compatta ───────────────────────────────────────────────────
    Row {
        id: compactRow
        visible: !root.expanded
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacing
        spacing: Theme.spacingTight

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.showRelief ? qsTr("Rilievo") : qsTr("Piatto")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "·"
            font.pixelSize: Theme.fontSmall
            color: Theme.border
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.panadapter.paletteNames[root.panadapter.paletteIndex]
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "⚙"
            font.pixelSize: Theme.fontNormal
            color: Theme.accent
        }
    }

    // ── Pannello aperto ──────────────────────────────────────────────────
    Column {
        id: full
        visible: root.expanded
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacing
        spacing: Theme.spacingTight

        Row {
            id: headerRow
            width: parent.width
            spacing: Theme.spacingTight

            Text {
                id: headerLabel
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Waterfall")
                font.pixelSize: Theme.fontSmall
                font.bold: true
                color: Theme.textPrimary
            }

            Item {
                width: Math.max(0, headerRow.width - headerLabel.width - closeLabel.width
                                   - 2 * Theme.spacingTight)
                height: 1
            }

            Text {
                id: closeLabel
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                font.pixelSize: Theme.fontNormal
                color: Theme.textSecondary
            }

            // Solo l'intestazione richiude: un'area su tutto il pannello
            // mangerebbe i trascinamenti dei cursori sotto.
            TapHandler {
                onTapped: root.expanded = false
            }
        }

        // ── Vista ────────────────────────────────────────────────────────
        Row {
            width: parent.width
            spacing: Theme.spacingTight

            DsdrButton {
                width: (parent.width - Theme.spacingTight) / 2
                implicitWidth: 0
                text: qsTr("Piatto")
                checkable: true
                checked: !root.showRelief
                onClicked: root.panadapter.waterfallMode = PanadapterView.Flat
            }

            DsdrButton {
                width: (parent.width - Theme.spacingTight) / 2
                implicitWidth: 0
                text: qsTr("Rilievo")
                checkable: true
                checked: root.showRelief
                onClicked: root.panadapter.waterfallMode = PanadapterView.Relief
            }
        }

        // ── Palette ──────────────────────────────────────────────────────
        DsdrComboBox {
            width: parent.width
            model: root.panadapter.paletteNames
            currentIndex: root.panadapter.paletteIndex
            onActivated: (index) => root.panadapter.paletteIndex = index
        }

        // ── Scala automatica ─────────────────────────────────────────────
        DsdrButton {
            width: parent.width
            implicitWidth: 0
            text: qsTr("Scala automatica")
            checkable: true
            checked: root.panadapter.autoRange
            onClicked: root.panadapter.autoRange = checked
        }

        Text {
            width: parent.width
            visible: root.panadapter.autoRange
            text: qsTr("Fondo %1 · picco %2 dB")
                  .arg(Math.round(root.panadapter.noiseFloorDb))
                  .arg(Math.round(root.panadapter.peakLevelDb))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.accent
            elide: Text.ElideRight
        }

        Text {
            text: qsTr("Fondo %1 dB").arg(Math.round(root.panadapter.floorDb))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            id: floorSlider
            width: parent.width
            from: -160; to: -60
            enabled: !root.panadapter.autoRange
            value: root.panadapter.floorDb
            onMoved: root.panadapter.floorDb = value
        }

        Text {
            text: qsTr("Vetta %1 dB").arg(Math.round(root.panadapter.ceilingDb))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            id: ceilingSlider
            width: parent.width
            from: -80; to: 0
            enabled: !root.panadapter.autoRange
            value: root.panadapter.ceilingDb
            onMoved: root.panadapter.ceilingDb = value
        }

        // ── Tono ─────────────────────────────────────────────────────────
        Text {
            text: qsTr("Contrasto %1").arg(root.panadapter.gamma.toFixed(2))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            width: parent.width
            from: 0.2; to: 2.5
            value: root.panadapter.gamma
            onMoved: root.panadapter.gamma = value
        }

        Text {
            text: qsTr("Soglia %1%").arg(Math.round(root.panadapter.blackThreshold * 100))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            width: parent.width
            from: 0; to: 0.6
            value: root.panadapter.blackThreshold
            onMoved: root.panadapter.blackThreshold = value
        }

        // ── Scena in rilievo ─────────────────────────────────────────────
        Text {
            visible: root.showRelief
            text: qsTr("Inclinazione %1°").arg(Math.round(root.panadapter.tilt))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            visible: root.showRelief
            width: parent.width
            from: 15; to: 85
            value: root.panadapter.tilt
            onMoved: root.panadapter.tilt = value
        }

        Text {
            visible: root.showRelief
            text: qsTr("Rotazione %1°").arg(Math.round(root.panadapter.rotation3d))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            visible: root.showRelief
            width: parent.width
            from: -45; to: 45
            value: root.panadapter.rotation3d
            onMoved: root.panadapter.rotation3d = value
        }

        Text {
            visible: root.showRelief
            text: qsTr("Rilievo %1").arg(root.panadapter.reliefScale.toFixed(2))
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            visible: root.showRelief
            width: parent.width
            from: 0.05; to: 1.2
            value: root.panadapter.reliefScale
            onMoved: root.panadapter.reliefScale = value
        }
    }

    // I cursori di livello seguono la misura finché comanda l'auto-range:
    // senza questo, dopo il primo trascinamento manuale il binding si romperebbe
    // e resterebbero fermi su un valore che non è più quello in uso.
    Binding {
        target: floorSlider
        property: "value"
        value: root.panadapter.floorDb
        when: root.panadapter.autoRange
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: ceilingSlider
        property: "value"
        value: root.panadapter.ceilingDb
        when: root.panadapter.autoRange
        restoreMode: Binding.RestoreNone
    }

    // Da chiuso tutto il rettangolo apre: è una barra piccola, cercare un
    // bersaglio dentro un bersaglio non serve a nessuno.
    TapHandler {
        enabled: !root.expanded
        onTapped: root.expanded = true
    }
}
