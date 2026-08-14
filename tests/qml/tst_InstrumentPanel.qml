// SPDX-License-Identifier: GPL-3.0-or-later
// Il pannello che ospita i due strumenti.
//
// Il difetto che presidia non produce errori: uno strumento nascosto che
// continua a occupare la sua altezza lascia un buco nella colonna, e un buco
// non somiglia a un difetto — somiglia a una scelta di chi ha disegnato la
// pagina. Si vede solo mettendo i numeri uno accanto all'altro.
//
// Ogni pannello nasce qui con `collapsed: false` dichiarato: lo stato di
// apertura si ricorda fra un avvio e l'altro, e senza questo il primo test che
// chiude un pannello fa nascere chiusi tutti quelli delle prove successive —
// che poi falliscono misurando l'altezza di un'intestazione.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "InstrumentPanel"
    width: 360
    height: 900
    visible: true
    when: windowShown

    readonly property int panelWidth: 340

    Component {
        id: panelComponent
        SMeterPanel { width: testCase.panelWidth; collapsed: false }
    }

    Component {
        id: powerComponent
        DecoMeter { width: testCase.panelWidth }
    }

    Component {
        id: rdsComponent

        RdsPanel {
            width: testCase.panelWidth
            channelIndex: 0
            fmStereo: true
            fmRds: true
            rdsAutomaticAf: false
            rdsRegion: 0
            rdsSynced: true
            rdsCountryCode: 2
            rdsProgramCoverage: 1
            rdsReferenceNumber: 42
            rdsPi: "1234"
            rdsCallsign: "IURADIO"
            rdsProgramType: "Pop Music"
            rdsAlternateFrequencies: "99.700 MHz · 101.200 MHz"
            rdsProgramService: "DECODIUM"
            rdsRadioText: "Segnale RDS di prova"
        }
    }

    /// Cerca in profondità un figlio per nome: i due tasti del selettore
    /// nascono dentro un Repeater, e il loro posto nell'albero non è una cosa
    /// su cui una prova debba fare affidamento.
    function findByName(item, name) {
        if (item.objectName === name)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findByName(item.children[i], name)
            if (found)
                return found
        }
        return null
    }

    // Il selettore si preme. È la richiesta da cui è nato questo pannello —
    // poter passare da uno strumento all'altro — e verificarla assegnando la
    // proprietà da fuori non proverebbe niente: chi usa il programma non ha
    // una proprietà, ha due tasti.
    function test_pressing_the_selector_switches_instrument() {
        const panel = createTemporaryObject(panelComponent, testCase)
        // La scelta si ricorda, quindi da dove parta il pannello dipende da
        // che cosa ha lasciato scritto la prova precedente: si dichiara qui
        // invece di darlo per buono. Quello che si verifica è il tasto.
        panel.instrument = 0
        wait(120)

        const bars = findByName(panel, "instrument-1")
        verify(bars !== null, "tasto delle barre non trovato")
        mouseClick(bars, bars.width / 2, bars.height / 2)
        wait(120)
        compare(panel.instrument, 1, "premere BARRE non ha cambiato lettura")

        // Il tasto della potenza c'è solo dove c'è una potenza da misurare:
        // lo dichiarano le capability del backend. Qui non c'è nessuna radio
        // collegata, quindi lo si dichiara a mano.
        panel.powerMeterAvailable = true
        wait(120)

        const power = findByName(panel, "instrument-2")
        verify(power !== null, "tasto della potenza non trovato")
        mouseClick(power, power.width / 2, power.height / 2)
        wait(120)
        compare(panel.instrument, 2, "premere POTENZA non ha cambiato strumento")
        compare(panel.title, "DECØMETER")

        const needle = findByName(panel, "instrument-0")
        verify(needle !== null, "tasto dell'ago non trovato")
        mouseClick(needle, needle.width / 2, needle.height / 2)
        wait(120)
        compare(panel.instrument, 0, "non si torna allo strumento di prima")
        compare(panel.title, "DECØMETER-S")
    }

    function test_instrument_choice_switches_the_title_data() {
        return [
            { tag: "ago",     instrument: 0, title: "DECØMETER-S" },
            { tag: "barre",   instrument: 1, title: "DECØMETER-S" },
            { tag: "potenza", instrument: 2, title: "DECØMETER" },
        ]
    }

    // Il titolo dice quale strumento è attivo: a pannello chiuso è tutto
    // quello che resta, e senza di lui non si saprebbe cosa si sta per
    // riaprire.
    function test_instrument_choice_switches_the_title(data) {
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null, "pannello non istanziato")

        // Senza wattmetro il pannello torna al segnale, e il titolo con lui:
        // qui si sta verificando il legame fra strumento e titolo, non quella
        // regola — che ha una prova sua.
        panel.powerMeterAvailable = true

        // Lo strumento si sceglie dopo la creazione, come fa chi preme il
        // selettore. Passarlo fra le proprietà iniziali non servirebbe: la
        // scelta si ricorda fra un avvio e l'altro, e il valore salvato viene
        // applicato dopo — è la stessa ragione per cui il pannello riapre
        // sullo strumento che si stava guardando.
        panel.instrument = data.instrument
        wait(50)
        compare(panel.title, data.title)
    }

    // Senza una radio che trasmetta, il tasto della potenza non c'è.
    //
    // Non spento: assente. Un tasto grigio resta lì a far chiedere che cosa
    // manchi per accenderlo, e su un RTL-SDR la risposta è «un'altra radio».
    //
    // E se lo strumento scelto era la potenza, il pannello torna al segnale
    // invece di mostrare un quadrante di trattini: la scelta salvata di ieri
    // non deve poter lasciare oggi uno strumento vuoto.
    function test_without_a_transmitter_there_is_no_power_button() {
        const panel = createTemporaryObject(panelComponent, testCase)
        panel.powerMeterAvailable = true
        panel.instrument = 2
        wait(120)

        panel.powerMeterAvailable = false
        wait(120)

        const power = findByName(panel, "instrument-2")
        verify(power === null || !power.visible,
               "il tasto della potenza c'è senza niente da misurare")
        compare(panel.instrument, 0, "lo strumento non è tornato al segnale")
        compare(panel.title, "DECØMETER-S")
    }

    // Lo strumento nascosto non deve occupare spazio.
    //
    // Il confronto è con lo strumento da solo, non con l'altro pannello: senza
    // una sessione connessa il quadrante del segnale non ha canali da mostrare
    // ed è legittimamente vuoto, mentre quello della potenza c'è sempre. Il
    // pannello vale il suo strumento più l'intestazione e il selettore; se ci
    // stesse dentro anche il quadrante spento, sarebbe alto quasi il doppio.
    function test_the_hidden_instrument_takes_no_room() {
        const meter = createTemporaryObject(powerComponent, testCase)
        wait(120)
        const meterHeight = meter.implicitHeight
        verify(meterHeight > 80, "lo strumento non ha altezza: " + meterHeight)

        const panel = createTemporaryObject(panelComponent, testCase)
        panel.powerMeterAvailable = true
        panel.instrument = 2
        wait(300)

        verify(panel.implicitHeight > meterHeight,
               "il pannello non contiene lo strumento: " + panel.implicitHeight)
        verify(panel.implicitHeight < meterHeight * 1.5,
               "il pannello è alto " + panel.implicitHeight + " per uno strumento da "
               + meterHeight + ": il quadrante spento sta ancora occupando spazio")
    }

    // Chiuso, il pannello si riduce alla sua intestazione, quale che sia lo
    // strumento scelto.
    function test_collapsing_hides_the_instrument() {
        const panel = createTemporaryObject(panelComponent, testCase)
        panel.powerMeterAvailable = true
        panel.instrument = 2
        wait(300)
        const open = panel.implicitHeight
        verify(open > 80, "il pannello aperto non ha contenuto: " + open)

        panel.collapsed = true
        wait(300)
        verify(panel.implicitHeight < open / 2,
               "chiuso il pannello resta alto " + panel.implicitHeight
               + " contro " + open + " da aperto")

        // Riaperto prima di uscire: lo stato si ricorda, e lasciarlo chiuso
        // vorrebbe dire consegnare alla prova successiva un pannello che
        // nasce chiuso.
        panel.collapsed = false
        wait(120)
    }

    function test_invalid_persisted_s9_calibration_is_repaired() {
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null, "pannello S-meter non istanziato")

        panel.s9ReferenceDb = 13.9
        wait(50)
        compare(panel.s9ReferenceDb, panel.defaultS9ReferenceDb,
                "una S9 sopra il fondo scala non è stata riparata")
        verify(!panel.calibrated, "la tara non valida è rimasta marcata valida")
    }

    function test_rds_panel_displays_decoded_station_data() {
        const panel = createTemporaryObject(rdsComponent, testCase)
        verify(panel !== null, "pannello RDS non istanziato")
        wait(80)

        compare(findByName(panel, "rds-status").text, "SYNC")
        compare(findByName(panel, "rds-ps").text, "DECODIUM")
        compare(findByName(panel, "rds-radio-text").text, "Segnale RDS di prova")
        compare(findByName(panel, "rds-pi").text, "1234")
        compare(findByName(panel, "rds-pty").text, "Pop Music")
        compare(findByName(panel, "rds-af").text, "99.700 MHz · 101.200 MHz")
    }
}
