import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property string value: ""
    property string subtitle: ""
    property bool warn: false
    property bool clickable: false
    signal clicked

    SystemPalette { id: theme }
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))
    readonly property color warmColor: "#e08a3c"

    implicitHeight: 64
    radius: 8
    color: Qt.tint(theme.window, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.06))
    border.color: mouse.containsMouse && root.clickable ? theme.highlight : theme.mid

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 2
        Text {
            Layout.fillWidth: true
            text: root.value
            elide: Text.ElideRight
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: root.warn ? root.warmColor : theme.text
        }
        Text {
            Layout.fillWidth: true
            text: root.subtitle
            elide: Text.ElideRight
            font.pixelSize: 12
            color: root.muted
        }
        Item { Layout.fillHeight: true }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
