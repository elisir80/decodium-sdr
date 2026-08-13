// SPDX-License-Identifier: GPL-3.0-or-later
// La linea grigia e il puntamento, nello stesso pannello.
//
// **Perché stanno insieme.** Sono le due metà della stessa domanda. La mappa
// dice *quando* — sulle bande basse le aperture buone durano i minuti in cui
// il percorso corre lungo il terminatore, non tutto il giorno. Il quadrante
// dice *dove* puntare. Tenerle in due pannelli vorrebbe dire guardare la
// mappa, ricordare un numero, e cercarlo altrove: il momento buono dura poco
// e non si passa a cercare finestre.
//
// **Il numero che conta è in fondo.** Non l'azimut e non la distanza — quelli
// li dà qualunque programma — ma **quanti chilometri del percorso stanno
// adesso nella fascia grigia**. È la cifra che dice se conviene chiamare, e
// non si legge a occhio da una mappa: un percorso può attraversare il
// terminatore di sbieco per migliaia di chilometri o tagliarlo di netto in
// duecento, e le due cose sulla mappa si assomigliano.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("LINEA GRIGIA")
    draggable: true
    collapsed: true
    // La mappa vuole larghezza: in una striscia stretta il mondo diventa una
    // fettuccia e il terminatore non si legge più.
    detachable: true

    /// Il proprio QTH, come locatore. È il modo in cui ci si scambia una
    /// posizione fra radioamatori: nessuno detta gradi decimali.
    property string homeLocator: "JN71DC"

    /// Il corrispondente, se se n'è scelto uno.
    property string targetLocator: ""

    readonly property point home: engine.fromLocator(homeLocator)
    readonly property point target: targetLocator === ""
                                    ? Qt.point(NaN, NaN)
                                    : engine.fromLocator(targetLocator)

    readonly property bool haveTarget: !isNaN(target.x) && !isNaN(target.y)

    readonly property real shortPath: haveTarget
        ? engine.bearing(home.y, home.x, target.y, target.x) : -1

    Settings {
        id: prefs
        category: "greyline"
        property alias home: root.homeLocator
        property alias target: root.targetLocator
        property alias longPath: longPathButton.checked
        property alias rotorHost: rotor.host
        property alias rotorPort: rotor.port
    }

    // ── Il rotore ────────────────────────────────────────────────────────
    //
    // Non si collega da solo all'avvio, e non e' una dimenticanza: aprire il
    // programma non deve poter mettere in moto un'antenna in cima a una torre.
    // Ci si collega quando lo si chiede.
    RotorController {
        id: rotor
    }

    Greyline {
        id: engine
        resolution: 240
        refreshSeconds: 60
    }

    // ── La mappa ─────────────────────────────────────────────────────────
    GreylineMap {
        id: map

        Layout.fillWidth: true
        // Due a uno: è il rapporto della proiezione equirettangolare, e
        // qualunque altro schiaccerebbe la mappa senza dirlo.
        Layout.preferredHeight: width / 2
        Layout.maximumHeight: 420

        engine: engine
        home: root.home
        target: root.target
        longPath: longPathButton.checked

        // Un tocco sulla mappa sceglie un corrispondente: è più veloce che
        // digitarne il locatore, e per «chi c'è da quella parte adesso» è
        // esattamente il gesto che si vuole fare.
        TapHandler {
            onTapped: (point) => {
                const geo = map.geoAt(point.position.x, point.position.y)
                root.targetLocator = engine.toLocator(geo.y, geo.x)
            }
        }
    }

    // ── L'ora a cui si sta guardando ─────────────────────────────────────
    //
    // Il cursore sposta il momento avanti e indietro di dodici ore. È il
    // comando che serve davvero: «com'è fra tre ore» si chiede molto più
    // spesso di «com'era il 12 marzo».
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Text {
            text: engine.hourOffset === 0
                  ? qsTr("ADESSO")
                  : (engine.hourOffset > 0 ? qsTr("+%1 h") : qsTr("%1 h"))
                        .arg(engine.hourOffset.toFixed(1))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            font.bold: engine.hourOffset !== 0
            color: engine.hourOffset === 0 ? Theme.textDisabled : Theme.accent
            Layout.preferredWidth: 66
        }

        DsdrSlider {
            Layout.fillWidth: true
            from: -12
            to: 12
            stepSize: 0.25
            snapMode: Slider.SnapAlways
            value: engine.hourOffset
            onMoved: engine.hourOffset = value
        }

        DsdrButton {
            implicitWidth: 62
            implicitHeight: 24
            fontSize: Theme.fontSmall
            text: qsTr("ORA")
            enabled: engine.hourOffset !== 0
            onClicked: engine.hourOffset = 0
        }

        Text {
            text: Qt.formatDateTime(engine.referenceTime(), "dd MMM hh:mm") + " UTC"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textSecondary
        }
    }

    // ── Il puntamento ────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingTight
        spacing: Theme.spacing

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Theme.spacingTight

            RotorDial {
                Layout.preferredWidth: 168
                Layout.preferredHeight: 168

                bearing: root.shortPath
                useLongPath: longPathButton.checked
                // L'indice tratteggiato: dove punta davvero l'antenna. Compare
                // solo se un rotore lo sa dire — finche' non c'e', non si
                // disegna un ago che finge di sapere.
                heading: rotor.connected ? rotor.azimuth : -1
            }

            // Lo scarto fra dove si punta e dove si dovrebbe: e' l'unica cosa
            // che si guarda mentre il rotore gira.
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: rotor.connected && root.haveTarget
                text: {
                    const want = longPathButton.checked ? (root.shortPath + 180) % 360
                                                        : root.shortPath
                    let d = Math.abs(rotor.azimuth - want) % 360
                    if (d > 180)
                        d = 360 - d
                    return d < 1 ? qsTr("in punta")
                                 : qsTr("%1° da girare").arg(Math.round(d))
                }
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: rotor.moving ? Theme.spectrumPeak : Theme.textSecondary
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.spacingTight

            // ── I due locatori ───────────────────────────────────────────
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: Theme.spacingTight
                rowSpacing: Theme.spacingTight

                Text {
                    text: qsTr("QTH")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                TextField {
                    id: homeField

                    Layout.preferredWidth: 92
                    text: root.homeLocator
                    font.pixelSize: Theme.fontNormal
                    font.family: Theme.monoFamily
                    selectByMouse: true
                    // Un locatore sbagliato si vede subito invece di piazzare
                    // la stazione nel golfo di Guinea, che è dove finisce un
                    // valore non valido letto come zero.
                    color: isNaN(root.home.x) ? Theme.danger : Theme.textPrimary
                    placeholderTextColor: Theme.textDisabled
                    onEditingFinished: root.homeLocator = text.toUpperCase()

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceSunken
                        border.width: 1
                        border.color: homeField.activeFocus ? Theme.accent : Theme.border
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (isNaN(root.home.x))
                            return qsTr("locatore non valido")
                        const c = engine.condition(root.home.y, root.home.x)
                        const a = engine.sunAltitude(root.home.y, root.home.x)
                        return (c === 1 ? qsTr("in linea grigia")
                                        : c === 0 ? qsTr("giorno") : qsTr("notte"))
                               + qsTr(" · sole %1°").arg(a.toFixed(1))
                    }
                    font.pixelSize: Theme.fontSmall
                    color: isNaN(root.home.x) ? Theme.danger
                         : engine.condition(root.home.y, root.home.x) === 1
                           ? Theme.spectrumPeak : Theme.textSecondary
                    elide: Text.ElideRight
                }

                Text {
                    text: qsTr("DX")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                TextField {
                    id: targetField

                    Layout.preferredWidth: 92
                    text: root.targetLocator
                    placeholderText: qsTr("locatore")
                    font.pixelSize: Theme.fontNormal
                    font.family: Theme.monoFamily
                    selectByMouse: true
                    color: root.targetLocator !== "" && isNaN(root.target.x)
                           ? Theme.danger : Theme.textPrimary
                    placeholderTextColor: Theme.textDisabled
                    onEditingFinished: root.targetLocator = text.toUpperCase()

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceSunken
                        border.width: 1
                        border.color: targetField.activeFocus ? Theme.accent : Theme.border
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (!root.haveTarget)
                            return qsTr("scegli un punto sulla mappa, o scrivi il locatore")
                        const c = engine.condition(root.target.y, root.target.x)
                        const a = engine.sunAltitude(root.target.y, root.target.x)
                        return (c === 1 ? qsTr("in linea grigia")
                                        : c === 0 ? qsTr("giorno") : qsTr("notte"))
                               + qsTr(" · sole %1°").arg(a.toFixed(1))
                    }
                    font.pixelSize: Theme.fontSmall
                    color: !root.haveTarget ? Theme.textDisabled
                         : engine.condition(root.target.y, root.target.x) === 1
                           ? Theme.spectrumPeak : Theme.textSecondary
                    elide: Text.ElideRight
                }
            }

            // ── Rotta e distanza ─────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingTight
                spacing: Theme.spacingTight
                visible: root.haveTarget

                DsdrButton {
                    implicitWidth: 76
                    implicitHeight: 26
                    fontSize: Theme.fontSmall
                    text: qsTr("BREVE")
                    checked: !longPathButton.checked
                    onClicked: longPathButton.checked = false
                }

                DsdrButton {
                    id: longPathButton

                    implicitWidth: 76
                    implicitHeight: 26
                    fontSize: Theme.fontSmall
                    text: qsTr("LUNGA")
                    checkable: true
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        const km = engine.distanceKm(root.home.y, root.home.x,
                                                     root.target.y, root.target.x)
                        const shown = longPathButton.checked ? 40008 - km : km
                        return qsTr("%1 km").arg(Math.round(shown))
                    }
                    font.pixelSize: Theme.fontNormal
                    font.family: Theme.monoFamily
                    color: Theme.textPrimary
                }
            }

            // ── Quanto del percorso è in linea grigia ────────────────────
            //
            // Questo è il numero per cui esiste il pannello. Gli altri li dà
            // qualunque programma.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingTight
                spacing: 2
                visible: root.haveTarget

                readonly property real overlap: engine.greylineOverlapKm(
                    root.home.y, root.home.x, root.target.y, root.target.x)
                readonly property real total: engine.distanceKm(
                    root.home.y, root.home.x, root.target.y, root.target.x)

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("PERCORSO IN LINEA GRIGIA")
                        font.pixelSize: Theme.fontSmall
                        font.letterSpacing: 1.2
                        color: Theme.textDisabled
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: parent.parent.overlap < 1
                              ? qsTr("nessuno")
                              : qsTr("%1 km · %2%")
                                    .arg(Math.round(parent.parent.overlap))
                                    .arg(Math.round(100 * parent.parent.overlap
                                                    / Math.max(1, parent.parent.total)))
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                        font.bold: parent.parent.overlap > 1
                        color: parent.parent.overlap > 1 ? Theme.spectrumPeak
                                                         : Theme.textDisabled
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 5
                    radius: 2.5
                    color: Theme.surfaceSunken

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1,
                            parent.parent.overlap / Math.max(1, parent.parent.total)))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.spectrumPeak
                    }
                }
            }

            // ── Il rotore (SPEC-006 §3.5) ────────────────────────────────
            //
            // Puntare e' un comando esplicito. Un rotore che parte perche'
            // qualcuno ha toccato una mappa e' una sorpresa su un palo, e sui
            // pali le sorprese costano.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingTight
                spacing: Theme.spacingTight

                Text {
                    text: qsTr("ROTORE")
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Theme.textDisabled
                }

                DsdrButton {
                    implicitWidth: 84
                    implicitHeight: 26
                    fontSize: Theme.fontSmall
                    text: rotor.connected ? qsTr("STACCA") : qsTr("COLLEGA")
                    onClicked: rotor.connected ? rotor.disconnectFromRotor()
                                               : rotor.connectToRotor()
                }

                DsdrButton {
                    implicitWidth: 76
                    implicitHeight: 26
                    fontSize: Theme.fontSmall
                    text: qsTr("PUNTA")
                    enabled: rotor.connected && root.haveTarget
                    onClicked: rotor.pointTo(longPathButton.checked
                                             ? (root.shortPath + 180) % 360
                                             : root.shortPath)
                }

                // Rosso, largo e sempre acceso finche' c'e' un rotore. Non e'
                // decorazione: e' l'unico comando che qualcuno cerchera' di
                // fretta, e un tasto di emergenza che si spegne quando «non
                // serve» non si trova proprio quando serve.
                DsdrButton {
                    implicitWidth: 76
                    implicitHeight: 26
                    fontSize: Theme.fontSmall
                    text: qsTr("FERMA")
                    danger: true
                    enabled: rotor.connected
                    onClicked: rotor.stop()
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (!rotor.connected)
                            return qsTr("rotctld su %1:%2").arg(rotor.host).arg(rotor.port)
                        let s = qsTr("%1°").arg(Math.round(rotor.azimuth))
                        if (rotor.hasElevation)
                            s += qsTr(" · el %1°").arg(Math.round(rotor.elevation))
                        if (rotor.model !== "")
                            s += " · " + rotor.model
                        return s
                    }
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: rotor.connected ? Theme.textSecondary : Theme.textDisabled
                    elide: Text.ElideRight
                }
            }

            Text {
                Layout.fillWidth: true
                visible: rotor.trouble !== ""
                text: rotor.trouble
                font.pixelSize: Theme.fontSmall
                color: Theme.danger
                wrapMode: Text.WordWrap
            }

            // ── La fascia utile ──────────────────────────────────────────
            //
            // Regolabile, e vale la pena dire perché: il terminatore radio non
            // coincide con quello ottico. Gli strati ionizzati stanno a
            // centinaia di chilometri di quota e restano illuminati quando la
            // superficie è già al buio, quindi la linea grigia utile è
            // spostata e sfumata di qualche grado. Sei è un punto di partenza,
            // non una legge.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingTight
                spacing: Theme.spacingTight

                Text {
                    text: qsTr("FASCIA")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                DsdrSlider {
                    Layout.fillWidth: true
                    from: 1
                    to: 15
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: engine.greylineHalfWidth
                    onMoved: engine.greylineHalfWidth = value
                }

                Text {
                    text: qsTr("±%1°").arg(engine.greylineHalfWidth.toFixed(0))
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Il terminatore radio è più largo di quello ottico: la ionosfera resta illuminata quando la superficie è già al buio.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
