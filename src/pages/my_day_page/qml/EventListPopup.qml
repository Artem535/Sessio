import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Read-and-jump list behind the "Unpaid" / "Without notes" tiles.
Popup {
    id: root
    property string heading: ""
    property string currency: ""
    property var rows: []
    signal rowClicked(var row)

    SystemPalette { id: theme }
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))

    function openWith(title, list) {
        heading = title
        rows = list
        open()
    }

    modal: true
    focus: true
    padding: 16
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, Overlay.overlay ? Overlay.overlay.width - 48 : 480)
    height: Math.min(420, Overlay.overlay ? Overlay.overlay.height - 48 : 420)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 10; color: theme.window; border.color: theme.mid }

    contentItem: ColumnLayout {
        spacing: 10
        Text {
            Layout.fillWidth: true
            text: root.heading
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: theme.text
        }
        ListView {
            id: list
            objectName: "eventListPopupList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                implicitHeight: 40
                padding: 8
                onClicked: {
                    root.close()
                    root.rowClicked(modelData)
                }
                background: Rectangle {
                    radius: 6
                    color: row.hovered ? theme.base : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    Text { text: row.modelData.date; color: root.muted; font.pixelSize: 13 }
                    Text { text: row.modelData.time; color: root.muted; font.pixelSize: 13 }
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        elide: Text.ElideRight
                        color: theme.text
                        font.pixelSize: 14
                    }
                    Text {
                        text: Number(row.modelData.sum).toLocaleString(Qt.locale(), "f", 0) + " " + root.currency
                        color: theme.text
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
