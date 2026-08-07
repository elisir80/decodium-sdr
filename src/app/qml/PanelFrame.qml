// SPDX-License-Identifier: GPL-3.0-or-later
// Cornice comune dei pannelli backend-specifici: dà loro lo stesso aspetto
// del resto dell'interfaccia senza che ognuno se lo ridisegni.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    /// Titolo mostrato in testa al pannello.
    property string title: ""
    /// Contenuto del pannello.
    default property alias content: contentColumn.data

    implicitHeight: layout.implicitHeight + 2 * Theme.spacing
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacingTight

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Rectangle {
                width: 3
                height: 12
                radius: 1.5
                color: Theme.accent
            }

            Text {
                text: root.title
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.letterSpacing: 0.5
                color: Theme.textSecondary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            spacing: Theme.spacingTight
        }
    }
}
