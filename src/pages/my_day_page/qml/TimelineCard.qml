import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One meeting row: time column on the left, card on the right.
Item {
    id: root
    required property var item
    signal opened
    signal joined

    SystemPalette { id: theme }
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))
    readonly property color warmColor: "#e08a3c"
    readonly property bool highlighted: item.state === "current" || item.state === "next"
    readonly property bool dimmed: item.state === "past" || item.canceled

    implicitHeight: card.implicitHeight
    height: implicitHeight

    Rectangle {
        id: timePill
        x: 0
        y: 6
        width: 52
        height: 22
        radius: 11
        color: root.highlighted ? theme.highlight : "transparent"
        opacity: root.dimmed ? 0.5 : 1
        Text {
            anchors.centerIn: parent
            text: root.item.start
            font.pixelSize: 13
            color: root.highlighted ? theme.highlightedText : root.muted
        }
    }

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.leftMargin: 62
        anchors.right: parent.right
        implicitHeight: body.implicitHeight + 16
        height: implicitHeight
        radius: 8
        color: hover.containsMouse ? theme.alternateBase : theme.base
        border.width: root.highlighted ? 2 : 1
        border.color: root.highlighted ? theme.highlight : theme.mid
        opacity: root.dimmed ? 0.5 : 1

        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.opened()
        }

        RowLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 8
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        objectName: "timelineCardTitle"
                        Layout.fillWidth: true
                        text: (root.item.canceled ? qsTr("Canceled") + ": " : "") + root.item.title
                        elide: Text.ElideRight
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: theme.text
                    }
                    Rectangle {
                        visible: root.item.unpaid
                        implicitWidth: unpaidText.implicitWidth + 10
                        implicitHeight: unpaidText.implicitHeight + 2
                        radius: 4
                        color: Qt.rgba(root.warmColor.r, root.warmColor.g, root.warmColor.b, 0.2)
                        Text {
                            id: unpaidText
                            anchors.centerIn: parent
                            text: qsTr("unpaid")
                            font.pixelSize: 11
                            color: root.warmColor
                        }
                    }
                    Rectangle {
                        visible: root.item.withoutNote
                        implicitWidth: noNoteText.implicitWidth + 10
                        implicitHeight: noNoteText.implicitHeight + 2
                        radius: 4
                        color: theme.mid
                        Text {
                            id: noNoteText
                            anchors.centerIn: parent
                            text: qsTr("no note")
                            font.pixelSize: 11
                            color: theme.text
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.item.timeRange
                          + (root.item.inPerson ? " · " + qsTr("in person") : "")
                          + (root.item.personal ? " · " + qsTr("personal") : "")
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: root.muted
                }
            }

            Text {
                objectName: "timelineCardLink"
                visible: root.item.online
                text: "\u{1F517}"
                font.pixelSize: 14
                color: theme.highlight
            }

            Button {
                id: joinButton
                objectName: "timelineCardJoin"
                visible: root.highlighted && root.item.joinable
                text: qsTr("Join")
                implicitHeight: 28
                horizontalPadding: 12
                onClicked: root.joined()
                background: Rectangle {
                    radius: 6
                    color: joinButton.hovered ? Qt.lighter(theme.highlight, 1.1) : theme.highlight
                }
                contentItem: Text {
                    text: joinButton.text
                    color: theme.highlightedText
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
