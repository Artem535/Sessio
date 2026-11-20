import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlMyDay"

    SystemPalette { id: theme }
    color: theme.window
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))

    readonly property var bridge: myDayBridge
    readonly property var tiles: bridge ? bridge.tiles : ({})
    readonly property var timeline: bridge ? bridge.timeline : []
    readonly property string currency: tiles.currency ? tiles.currency : ""
    property var collapsed: ({})

    readonly property var sections: {
        const defs = [
            { key: "morning", title: qsTr("Morning") },
            { key: "afternoon", title: qsTr("Afternoon") },
            { key: "evening", title: qsTr("Evening") }
        ]
        const result = []
        for (const d of defs) {
            const items = timeline.filter(t => t.section === d.key)
            if (items.length > 0)
                result.push({ key: d.key, title: d.title, items: items })
        }
        return result
    }

    function money(value) {
        return Number(value).toLocaleString(Qt.locale(), "f", 0) + " " + currency
    }
    function toggleSection(key) {
        const copy = Object.assign({}, collapsed)
        copy[key] = !copy[key]
        collapsed = copy
    }

    component NavButton: Button {
        id: control
        implicitHeight: 30
        horizontalPadding: 12
        background: Rectangle {
            radius: 6
            color: control.hovered && control.enabled ? theme.mid : theme.base
            border.color: theme.mid
            opacity: control.enabled ? 1 : 0.4
        }
        contentItem: Text {
            text: control.text
            color: theme.text
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: control.enabled ? 1 : 0.4
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                objectName: "myDayTitle"
                text: qsTr("My day")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: theme.text
            }
            Text {
                objectName: "myDayDateTitle"
                Layout.fillWidth: true
                text: root.bridge ? root.bridge.dateTitle : ""
                elide: Text.ElideRight
                font.pixelSize: 14
                color: root.muted
            }
            NavButton {
                objectName: "myDayPrevious"
                text: "‹"
                Accessible.name: qsTr("Previous day")
                onClicked: root.bridge.previousDay()
            }
            NavButton {
                objectName: "myDayToday"
                text: qsTr("Today")
                enabled: root.bridge ? !root.bridge.isToday : false
                onClicked: root.bridge.goToday()
            }
            NavButton {
                objectName: "myDayNext"
                text: "›"
                Accessible.name: qsTr("Next day")
                onClicked: root.bridge.nextDay()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StatTile {
                objectName: "tileMeetings"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                value: qsTr("%n meeting(s)", "", root.tiles.meetings ? root.tiles.meetings : 0)
                subtitle: root.tiles.meetingsSubtitle ? root.tiles.meetingsSubtitle : ""
            }
            StatTile {
                objectName: "tileIncome"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                value: root.money(root.tiles.income ? root.tiles.income : 0)
                subtitle: qsTr("planned income")
            }
            StatTile {
                objectName: "tileUnpaid"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                readonly property int count: root.tiles.unpaidCount ? root.tiles.unpaidCount : 0
                warn: count > 0
                clickable: count > 0
                value: count > 0 ? qsTr("%n unpaid", "", count) : "✓ " + qsTr("All paid")
                subtitle: count > 0 ? root.money(root.tiles.unpaidSum) + " · " + qsTr("open") : ""
                onClicked: popup.openWith(qsTr("Unpaid"), root.bridge.unpaidList())
            }
            StatTile {
                objectName: "tileWithoutNotes"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                readonly property int count: root.tiles.withoutNotesCount ? root.tiles.withoutNotesCount : 0
                warn: count > 0
                clickable: count > 0
                value: count > 0 ? qsTr("%n without notes", "", count) : "✓ " + qsTr("All notes written")
                subtitle: count > 0 ? qsTr("this week") + " · " + qsTr("open") : ""
                onClicked: popup.openWith(qsTr("Without notes"), root.bridge.withoutNotesList())
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                radius: 8
                color: theme.window
                border.color: theme.mid

                Flickable {
                    id: timelineFlick
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: width
                    contentHeight: sectionsColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    Column {
                        id: sectionsColumn
                        width: timelineFlick.width
                        spacing: 8

                        Repeater {
                            model: root.sections
                            delegate: Column {
                                id: section
                                required property var modelData
                                readonly property bool isCollapsed: !!root.collapsed[modelData.key]
                                width: sectionsColumn.width
                                spacing: 6

                                Text {
                                    objectName: "timelineSection_" + section.modelData.key
                                    width: parent.width
                                    text: (section.isCollapsed ? "▸ " : "▾ ") + section.modelData.title
                                          + (section.isCollapsed ? " (" + section.modelData.items.length + ")" : "")
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: root.muted
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.toggleSection(section.modelData.key)
                                    }
                                }
                                Repeater {
                                    model: section.isCollapsed ? [] : section.modelData.items
                                    delegate: TimelineCard {
                                        required property var modelData
                                        width: section.width
                                        item: modelData
                                        onOpened: root.bridge.openEvent(modelData.id, modelData.dayMs)
                                        onJoined: root.bridge.joinEvent(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    objectName: "timelineEmpty"
                    anchors.centerIn: parent
                    visible: root.timeline.length === 0
                    text: qsTr("No meetings")
                    font.pixelSize: 14
                    color: root.muted
                }
            }

            TodoColumn {
                objectName: "todoColumn"
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                bridge: root.bridge
            }
        }
    }

    EventListPopup {
        id: popup
        objectName: "eventListPopup"
        currency: root.currency
        onRowClicked: row => root.bridge.openEvent(row.id, row.dayMs)
    }
}
