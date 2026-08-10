// SPDX-License-Identifier: GPL-3.0-or-later
// Flag VFO trascinabile sullo spettro, colorato come il canale (§6.1).
//
// Le proprietà hanno prefisso `vfo`/`band` di proposito: il delegate del
// Repeater dichiara i ruoli del modello come `required property`, e un nome in
// comune fra le due parti creerebbe un binding su sé stesso.
import QtQuick
import DecodiumSdr

Item {
    id: root

    required property int vfoRow
    required property color vfoColor
    required property string vfoLabel
    required property real vfoFrequency
    required property int bandLowHz
    required property int bandHighHz
    required property real levelDb
    required property bool vfoSelected

    /// Conversioni fornite dal pannello contenitore.
    required property var xForFrequency
    required property var frequencyAt

    /// Che cosa chiede il gesto. Il componente non tocca la sessione da sé:
    /// così il trascinamento — che è geometria, e la geometria si sbaglia in
    /// silenzio — si può mettere sotto test senza una radio dietro.
    signal tuneRequested(real hz)
    signal selectRequested()
    signal removeRequested()

    readonly property real centerX: xForFrequency(vfoFrequency)
    readonly property real filterLeft: xForFrequency(vfoFrequency + bandLowHz)
    readonly property real filterRight: xForFrequency(vfoFrequency + bandHighHz)

    /// Il canale è fuori da ciò che si sta guardando, e la bandierina si è
    /// fermata al bordo invece di seguirlo. Da quel momento non è più sopra il
    /// suo canale: chi la guarda vede un ricevitore che non è lì, e chi la
    /// trascina muove qualcosa che sta altrove.
    readonly property bool adrift: centerX < 0 || centerX > width

    anchors.fill: parent

    // ── Il gesto, in un posto solo ───────────────────────────────────────
    //
    // Si può afferrare la bandierina in cima o l'asse per tutta la sua
    // altezza, e devono comportarsi allo stesso modo: due copie della stessa
    // geometria diventerebbero due comportamenti diversi al primo ritocco.
    property real grabOffsetHz: 0

    function beginDrag(pointerX) {
        selectRequested()
        // L'offset di presa serve a non far saltare il canale sotto il punto
        // in cui lo si è toccato. Ma vale solo finché la bandierina sta dov'è
        // il canale: quando è alla deriva al bordo, quell'offset è la distanza
        // dal canale — anche di megahertz — e il trascinamento non lo
        // riporterebbe mai sotto il puntatore. Lì il gesto torna a essere
        // quello che ci si aspetta: il canale va dove si punta.
        grabOffsetHz = adrift ? 0 : vfoFrequency - frequencyAt(pointerX)
    }

    function dragTo(pointerX) {
        tuneRequested(Math.round(frequencyAt(pointerX) + grabOffsetHz))
    }

    // ── Banda passante ───────────────────────────────────────────────────
    Rectangle {
        x: Math.min(root.filterLeft, root.filterRight)
        width: Math.max(2, Math.abs(root.filterRight - root.filterLeft))
        y: 0
        height: parent.height
        color: root.vfoColor
        opacity: root.vfoSelected ? 0.16 : 0.09
        visible: x + width > 0 && x < root.width

        Behavior on opacity {
            NumberAnimation { duration: Theme.animationFast }
        }
    }

    // ── Asse del VFO ─────────────────────────────────────────────────────
    Rectangle {
        x: root.centerX - width / 2
        width: root.vfoSelected ? 2 : 1
        y: 0
        height: parent.height
        color: root.vfoColor
        opacity: 0.9
        visible: x > -4 && x < root.width + 4
    }

    // La presa sull'asse. Una riga larga un pixel non si afferra: questa è
    // larga quattordici e invisibile, e copre tutta l'altezza — spettro e
    // waterfall insieme, perché è lì che si guarda quando si sposta un
    // ricevitore, non sull'etichetta in cima.
    //
    // Prenderla seleziona anche il ricevitore: chi tocca una barra sta
    // dicendo di quale si vuole occupare, ed è il modo in cui si passa da un
    // ricevitore all'altro senza cercare l'elenco.
    MouseArea {
        id: axisGrip

        x: root.centerX - width / 2
        width: 14
        y: 0
        height: parent.height
        visible: !root.adrift && x + width > 0 && x < root.width
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton

        onPressed: (mouse) => {
            const pointer = mapToItem(root, mouse.x, mouse.y)
            root.beginDrag(pointer.x)
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const pointer = mapToItem(root, mouse.x, mouse.y)
            root.dragTo(pointer.x)
        }
    }

    // ── Bandierina con etichetta ─────────────────────────────────────────
    /// La bandierina, per il test che ne presidia la posizione.
    readonly property alias flagItem: flag

    Rectangle {
        id: flag

        x: Math.max(0, Math.min(root.width - width, root.centerX - width / 2))
        y: Theme.spacingTight
        width: labelText.implicitWidth + 2 * Theme.spacing
        height: 20
        radius: Theme.radiusSmall
        color: root.vfoSelected ? root.vfoColor
                                : Qt.rgba(root.vfoColor.r, root.vfoColor.g, root.vfoColor.b, 0.35)
        border.width: 1
        border.color: root.vfoColor

        Text {
            id: labelText
            anchors.centerIn: parent
            // Alla deriva la bandierina dice dove sta andando a cercare il suo
            // canale: il livello di un canale fuori dalla banda campionata non
            // è una misura, e mostrarlo lo farebbe sembrare in ascolto.
            text: {
                if (root.adrift) {
                    return root.centerX < 0
                         ? "◀ " + root.vfoLabel
                         : root.vfoLabel + " ▶"
                }
                return root.vfoLabel + "  "
                     + (root.levelDb > -139 ? Math.round(root.levelDb) + " dB" : "—")
            }
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: root.vfoSelected ? Theme.background : Theme.textPrimary
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.SizeHorCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onPressed: (mouse) => {
                const pointer = mapToItem(root, mouse.x, mouse.y)
                root.beginDrag(pointer.x)
            }

            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const pointer = mapToItem(root, mouse.x, mouse.y)
                root.dragTo(pointer.x)
            }

            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    root.removeRequested()
            }
        }
    }
}
