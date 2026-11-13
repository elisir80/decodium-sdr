// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import DecodiumSdr

TestCase {
    id: testCase
    name: "ChannelQuickSelector"
    width: 500; height: 120; visible: true; when: windowShown
    ListModel {
        id: channelFixtures
        ListElement { label: "RX 1"; modeName: "USB"; channelColor: "#33ccaa" }
        ListElement { label: "RX 2"; modeName: "CW"; channelColor: "#ddaa33" }
    }
    Component {
        id: selectorComponent
        ChannelQuickSelector {
            width: 500
            channels: channelFixtures
            selectedIndex: 0
            onSelected: (row) => selectedIndex = row
        }
    }
    function test_select_and_return() {
        const selector = createTemporaryObject(selectorComponent, testCase)
        verify(selector !== null)
        const first = findChild(selector, "channelQuick0")
        const second = findChild(selector, "channelQuick1")
        verify(first !== null && second !== null)
        verify(first.checked && !second.checked)
        mouseClick(first)
        verify(first.checked && !second.checked)
        mouseClick(second)
        verify(!first.checked && second.checked)
        compare(selector.selectedIndex, 1)
        mouseClick(first)
        verify(first.checked && !second.checked)
        compare(selector.selectedIndex, 0)
    }
}
