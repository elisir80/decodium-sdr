// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello del ColibriNANO: guadagno e calibrazione delle bande laterali.
//
// Sul ColibriNANO preamplificatore e attenuatore sono la stessa manopola: un
// solo valore fra −31,5 e +6 dB. Mostrarli come due controlli separati
// sarebbe più fedele ad altre radio, ma non a questa.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("COLIBRINANO")

    readonly property var preampRange: Session.nativeCommand("colibri.preampRange", {}) || ({})
    readonly property var health: Session.nativeCommand("colibri.health", {}) || ({})

    /// Si rilegge a ogni cambio di frequenza: la zona di Nyquist dipende da
    /// dove si è sintonizzati, e cambia sotto le dita.
    property int refresh: 0
    readonly property var nyquist: {
        root.refresh
        return Session.nativeCommand("colibri.nyquist", {}) || ({})
    }

    Connections {
        target: Session
        function onCenterFrequencyChanged() { root.refresh++ }
    }

    /// Le zone aperte sopravvivono alla chiusura. Chi le usa ha un filtro
    /// davanti all'antenna e non lo smonta ogni sera: farsele rispegnere a
    /// ogni avvio vorrebbe dire ritrovare il ricevitore fermo a 55 MHz senza
    /// ricordarsi perché.
    property bool savedExtendedRange: false

    Settings {
        category: "colibri"
        property alias extendedRange: root.savedExtendedRange
    }

    Component.onCompleted: {
        if (root.savedExtendedRange) {
            Session.nativeCommand("colibri.setExtendedRange", { "enabled": true })
            root.refresh++
        }
    }

    property real preampDb: 0

    // ── Guadagno ─────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("Guadagno")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            Layout.fillWidth: true
            from: root.preampRange.min !== undefined ? root.preampRange.min : -31.5
            to: root.preampRange.max !== undefined ? root.preampRange.max : 6
            stepSize: 0.5
            value: root.preampDb
            onMoved: {
                root.preampDb = value
                Session.nativeCommand("colibri.setPreamp", { "db": value })
            }
        }

        Text {
            text: (root.preampDb >= 0 ? "+" : "") + root.preampDb.toFixed(1) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 58
            horizontalAlignment: Text.AlignRight
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Sotto lo zero attenua, sopra amplifica: è un'unica manopola.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // ── Sovraccarico dell'ADC ────────────────────────────────────────────
    // È l'unica telemetria che il device offre, e viaggia su ogni blocco di
    // campioni. Vale mostrarla: un ADC in saturazione produce prodotti di
    // intermodulazione che sembrano stazioni.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        visible: root.health.adcOverload === true

        Rectangle {
            width: 8; height: 8; radius: 4
            color: Theme.danger
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("ADC in sovraccarico — riduci il guadagno")
            font.pixelSize: Theme.fontSmall
            color: Theme.danger
            wrapMode: Text.WordWrap
        }
    }

    // ── Zone di Nyquist ──────────────────────────────────────────────────
    //
    // Il costruttore dichiara 0,1–55 MHz, ed è la banda in cui il device
    // promette qualcosa. Sopra si riceve lo stesso — l'ADC campiona a
    // 122,88 MHz e i segnali si ripiegano dentro la prima zona — ma senza
    // filtro d'ingresso arrivano tutte le zone insieme, sovrapposte.
    //
    // Per questo è un interruttore e non il valore predefinito: chi lo accende
    // sta dicendo che sa cosa aspettarsi, e che davanti ci mette un filtro.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            implicitWidth: 76
            implicitHeight: 26
            text: qsTr("NYQUIST")
            fontSize: Theme.fontSmall
            checkable: true
            checked: root.nyquist.extended === true
            onToggled: {
                Session.nativeCommand("colibri.setExtendedRange", { "enabled": checked })
                root.savedExtendedRange = checked
                root.refresh++
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.nyquist.extended === true
                  ? qsTr("fino a %1 MHz").arg((root.nyquist.maxHz / 1e6).toFixed(0))
                  : qsTr("oltre i 55 MHz, con filtro esterno")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.nyquist.extended === true && root.nyquist.zone > 1
        // Dire in quale zona si sta è l'unico modo di capire perché una
        // stazione non c'è: nella zona sbagliata non ci si arriva, e il
        // silenzio ha lo stesso aspetto di un'antenna staccata.
        text: qsTr("Zona %1 · DDC a %2 MHz%3")
              .arg(root.nyquist.zone)
              .arg((root.nyquist.deviceHz / 1e6).toFixed(3))
              .arg(root.nyquist.inverted === true ? qsTr(" · spettro rovesciato, raddrizzato dal backend") : "")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.nyquist.extended === true
        text: qsTr("Senza un passa-banda davanti all'antenna, le zone arrivano tutte insieme: quello che si vede può stare altrove.")
        font.pixelSize: Theme.fontSmall
        color: Theme.warning
        wrapMode: Text.WordWrap
    }

    // La calibrazione delle bande laterali non è più un comando: il
    // ColibriNANO consegna il flusso con la convenzione di segno opposta alla
    // nostra, sempre, e il backend lo coniuga sempre. Finché non si sapeva da
    // che parte stesse il vero era un interruttore; ora che si sa — provato
    // sull'hardware — lasciarlo vorrebbe dire offrire una posizione sbagliata
    // a chi lo trova girato dalla parte errata.
}
