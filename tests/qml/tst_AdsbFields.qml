// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "AdsbFields"
    visible: true
    when: windowShown
    width: 800; height: 600
    QtObject {
        id: fake
        property bool enabled: false
        property string status: "test"
        property string icao: ""
        property int typeCode: -1
        property string callsign: ""
        property bool hasBarometricAltitude: false
        property int barometricAltitudeFeet: 0
        property bool hasGroundSpeed: false
        property real groundSpeedKnots: 0
        property bool hasGroundTrack: false
        property real groundTrackDegrees: 0
        property bool hasAirspeed: false
        property real airspeedKnots: 0
        property bool trueAirspeed: false
        property bool hasMagneticHeading: false
        property real magneticHeadingDegrees: 0
        property bool hasVerticalRate: false
        property int verticalRateFeetPerMinute: 0
        property int frameCount: 0
        property int dropped: 0
        property string lastFrame: ""
    }
    Component { id: component; AdsbDialog { controller: fake } }
    function test_fieldsAB() {
        let dialog = createTemporaryObject(component, testCase)
        verify(dialog !== null)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        let icao = findChild(dialog, "adsbIcao")
        let type = findChild(dialog, "adsbType")
        let callsign = findChild(dialog, "adsbCallsign")
        let altitude = findChild(dialog, "adsbAltitude")
        let speed = findChild(dialog, "adsbSpeed")
        let track = findChild(dialog, "adsbTrack")
        let airspeed = findChild(dialog, "adsbAirspeed")
        let heading = findChild(dialog, "adsbHeading")
        let vertical = findChild(dialog, "adsbVerticalRate")
        verify(speed.text.includes("non disponibile"))
        verify(track.text.includes("non disponibile"))
        verify(airspeed.text.includes("non disponibile"))
        verify(heading.text.includes("non disponibile"))
        verify(vertical.text.includes("non disponibile"))
        verify(icao.text.includes("sconosciuto"))
        verify(type.text.includes("sconosciuto"))
        verify(callsign.text.includes("sconosciuto"))
        verify(altitude.text.includes("sconosciuta"))
        fake.icao = "40621D"; fake.typeCode = 11
        fake.callsign = "KLM1023"; fake.hasBarometricAltitude = true
        fake.barometricAltitudeFeet = 38000
        tryVerify(function() { return altitude.text.includes("38000 ft") })
        verify(icao.text.includes("40621D"))
        verify(type.text.includes("11"))
        verify(callsign.text.includes("KLM1023"))
        fake.hasGroundSpeed = true; fake.groundSpeedKnots = 159.2
        fake.hasGroundTrack = true; fake.groundTrackDegrees = 182.9
        fake.hasVerticalRate = true; fake.verticalRateFeetPerMinute = -832
        tryVerify(function() { return speed.text.includes("159.2 kt") })
        verify(track.text.includes("182.9°"))
        verify(vertical.text.includes("-832 ft/min"))
        fake.hasAirspeed = true; fake.airspeedKnots = 250; fake.trueAirspeed = true
        fake.hasMagneticHeading = true; fake.magneticHeadingDegrees = 90
        tryVerify(function() { return airspeed.text.includes("250.0 kt (TAS)") })
        verify(heading.text.includes("90.0°"))
        fake.trueAirspeed = false
        tryVerify(function() { return airspeed.text.includes("IAS") })
        fake.hasAirspeed = false; fake.hasMagneticHeading = false
        tryVerify(function() { return airspeed.text.includes("non disponibile") && heading.text.includes("non disponibile") })
        fake.hasGroundSpeed = false; fake.hasGroundTrack = false; fake.hasVerticalRate = false
        tryVerify(function() { return speed.text.includes("non disponibile") && track.text.includes("non disponibile") && vertical.text.includes("non disponibile") })
        fake.hasBarometricAltitude = false
        tryVerify(function() { return altitude.text.includes("sconosciuta") })
        dialog.close()
    }
}
