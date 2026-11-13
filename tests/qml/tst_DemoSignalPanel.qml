// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import "../../src/app/qml" as Local

TestCase {
    id: testCase
    name: "DemoSignalPanel"
    width: 400; height: 800; visible: true; when: windowShown
    QtObject {
        id: fake
        property var stored: ({enabled: true, mode: "am", offsetHz: 1500, amplitude: 0.3,
                               modulationHz: 800, amDepth: 0.5, fmDeviationHz: 2500,
                               noiseRms: 0.01, seed: 9, impulsePeriod: 0, impulseAmplitude: 0})
        function nativeCommand(command, args) {
            if (command === "demo.synthetic.status") return stored
            if (command === "demo.synthetic.apply") { stored = args; return {ok: true} }
            return ({})
        }
    }
    Component { id: panelComponent; Local.DemoSignalPanel {} }
    function test_editAndReplay() {
        const panel = createTemporaryObject(panelComponent, testCase, {width: 380, controller: fake})
        verify(panel !== null)
        verify(panel.height > 0)
        compare(findChild(panel, "syntheticEnabled").checked, true)
        compare(findChild(panel, "syntheticMode").currentIndex, 1)
        const offset = findChild(panel, "syntheticOffset")
        compare(offset.text, "1500")
        offset.text = "2400"
        compare(findChild(panel, "syntheticDrift").text, "0")
        findChild(panel, "syntheticDrift").text = "120"
        findChild(panel, "syntheticDriftPeriod").text = "8"
        findChild(panel, "syntheticInterfererOffset").text = "-2500"
        findChild(panel, "syntheticInterfererAmplitude").text = "0.1"
        findChild(panel, "syntheticFading").text = "0.7"
        findChild(panel, "syntheticFadingPeriod").text = "3"
        verify(panel.apply())
        compare(fake.stored.offsetHz, 2400)
        compare(fake.stored.mode, "am")
        compare(fake.stored.driftHz, 120)
        compare(fake.stored.driftPeriodSec, 8)
        compare(fake.stored.interfererOffsetHz, -2500)
        compare(fake.stored.interfererAmplitude, 0.1)
        compare(fake.stored.fadingDepth, 0.7)
        compare(fake.stored.fadingPeriodSec, 3)
        offset.text = "bad"
        verify(!panel.apply())
        compare(fake.stored.offsetHz, 2400)
    }
}
