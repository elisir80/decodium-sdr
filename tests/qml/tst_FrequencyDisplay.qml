// SPDX-License-Identifier: GPL-3.0-or-later
// La rotellina deve reagire soltanto a un vero movimento verticale.
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "FrequencyDisplay"
    when: windowShown

    Component {
        id: displayComponent
        FrequencyDisplay { }
    }

    function test_wheel_direction_ignores_zero_vertical_delta() {
        const display = createTemporaryObject(displayComponent, testCase)
        verify(display !== null, "display non istanziato")

        compare(display.wheelDirection(120), 1)
        compare(display.wheelDirection(-120), -1)
        compare(display.wheelDirection(0), 0,
                "un evento senza componente verticale non deve sintonizzare")
    }
}
