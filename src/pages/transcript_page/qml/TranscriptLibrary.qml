import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlTranscriptLibrary"
    property bool externalHeader: false
    SystemPalette { id: theme }
    color: theme.window
    property string query: ""
    readonly property var recordsModel: libraryBridge ? libraryBridge.records : []
    readonly property string noticeText: libraryBridge ? libraryBridge.notice : ""
    readonly property var visibleRecords: recordsModel.filter(r => (r.title + " " + r.date).toLowerCase().includes(query.toLowerCase()))
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))
    component Action: Button {
        id: control
        implicitHeight: 40
        padding: 10
        horizontalPadding: 16
        background: Rectangle {
            radius: 8
            color: control.hovered ? theme.mid : theme.window
            border.color: control.activeFocus ? theme.highlight : theme.mid
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
        anchors.margins: 24
        spacing: 24
        Text { visible: !root.externalHeader; text: qsTr("Transcripts"); font.pixelSize: 24; font.weight: Font.DemiBold; color: theme.text }
        TextField {
            visible: !root.externalHeader
            Layout.fillWidth: true
            implicitHeight: 40
            padding: 12
            font.pixelSize: 14
            placeholderText: qsTr("Search recordings and participants…")
            color: theme.text
            placeholderTextColor: root.muted
            onTextChanged: root.query = text
            background: Rectangle { color: theme.base; border.color: theme.mid; radius: 8 }
        }
        Text { text: root.noticeText; visible: text.length > 0; color: root.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.visibleRecords
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: record
                required property var modelData
                width: ListView.view.width
                padding: 20
                onClicked: if (libraryBridge) libraryBridge.openRecord(modelData.id, modelData.material)
                background: Rectangle {
                    radius: 8
                    color: record.hovered ? theme.base : "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.mid; opacity: 0.55 }
                }
                contentItem: RowLayout {
                    spacing: 20
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text { text: record.modelData.title; color: theme.text; font.pixelSize: 17; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { text: record.modelData.date + " · " + record.modelData.status; color: root.muted; font.pixelSize: 13 }
                    }
                    Action {
                        text: qsTr("Open")
                        onClicked: if (libraryBridge) libraryBridge.openRecord(record.modelData.id, record.modelData.material)
                    }
                    Action {
                        text: "⋯"
                        Accessible.name: qsTr("More actions")
                        enabled: !record.modelData.recording
                        onClicked: menu.open()
                        Menu {
                            id: menu
                            padding: 6
                            palette.window: theme.window
                            palette.base: theme.window
                            palette.text: theme.text
                            palette.windowText: theme.text
                            palette.highlight: theme.highlight
                            palette.highlightedText: theme.highlightedText
                            background: Rectangle { radius: 8; color: theme.window; border.color: theme.mid }
                            MenuItem {
                                id: remove
                                text: qsTr("Delete transcript")
                                onTriggered: if (libraryBridge) libraryBridge.removeRecord(record.modelData.id)
                                contentItem: Text { text: remove.text; color: remove.highlighted ? theme.highlightedText : theme.text; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 5; color: remove.highlighted ? theme.highlight : "transparent" }
                            }
                        }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: root.visibleRecords.length === 0; text: qsTr("No recordings found"); color: root.muted }
        }
    }
}
