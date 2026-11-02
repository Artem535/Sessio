import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    property var bridge: null
    readonly property var groups: bridge ? bridge.todoGroups : []
    readonly property bool byMeeting: bridge ? bridge.groupByMeeting : false

    SystemPalette { id: theme }
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))

    property bool adding: false
    property var clientOptions: []
    property var meetingOptions: []

    function startAdding() {
        if (!bridge) return
        clientOptions = [{ id: 0, name: qsTr("No client") }].concat(bridge.clientChoices())
        meetingOptions = [{ id: 0, label: qsTr("No meeting") }].concat(
            bridge.meetingChoices().map(m => ({ id: m.id, clientId: m.clientId, label: m.time + " " + m.title })))
        newTitle.text = ""
        clientBox.currentIndex = 0
        meetingBox.currentIndex = 0
        adding = true
        newTitle.forceActiveFocus()
    }
    // Picking a meeting selects that meeting's client in the client picker.
    function syncClientToMeeting() {
        const meeting = meetingOptions[meetingBox.currentIndex]
        if (!meeting || !meeting.clientId) return
        for (let i = 0; i < clientOptions.length; ++i) {
            if (clientOptions[i].id === meeting.clientId) {
                clientBox.currentIndex = i
                return
            }
        }
    }
    function saveNew() {
        if (!bridge || newTitle.text.trim().length === 0) return
        bridge.addTodo(newTitle.text, clientOptions[clientBox.currentIndex].id,
                       meetingOptions[meetingBox.currentIndex].id)
        newTitle.text = ""
        newTitle.forceActiveFocus()
    }

    radius: 8
    color: theme.window
    border.color: theme.mid

    component ComboStyle: ComboBox {
        implicitHeight: 28
        font.pixelSize: 12
        palette.button: theme.base
        palette.buttonText: theme.text
        palette.window: theme.window
        palette.windowText: theme.text
        palette.base: theme.base
        palette.text: theme.text
        palette.highlight: theme.highlight
        palette.highlightedText: theme.highlightedText
        palette.mid: theme.mid
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                objectName: "todoTitle"
                Layout.fillWidth: true
                text: (root.bridge && root.bridge.isToday ? qsTr("To-dos for today") : qsTr("To-dos"))
                      + " · " + (root.bridge ? root.bridge.todoDone : 0) + "/" + (root.bridge ? root.bridge.todoTotal : 0)
                elide: Text.ElideRight
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: theme.text
            }
            Rectangle {
                implicitWidth: seg.implicitWidth + 4
                implicitHeight: 26
                radius: 6
                color: theme.base
                border.color: theme.mid
                Row {
                    id: seg
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [{ label: qsTr("Clients"), meetings: false }, { label: qsTr("Meetings"), meetings: true }]
                        delegate: Rectangle {
                            id: segItem
                            required property var modelData
                            readonly property bool on: root.byMeeting === modelData.meetings
                            width: segText.implicitWidth + 16
                            height: 22
                            radius: 5
                            color: on ? theme.highlight : "transparent"
                            Text {
                                id: segText
                                anchors.centerIn: parent
                                text: segItem.modelData.label
                                font.pixelSize: 12
                                color: segItem.on ? theme.highlightedText : root.muted
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (root.bridge) root.bridge.groupByMeeting = segItem.modelData.meetings
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: groupsColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: groupsColumn
                width: parent.width
                spacing: 8

                Repeater {
                    model: root.groups
                    delegate: Column {
                        id: group
                        required property var modelData
                        width: groupsColumn.width
                        spacing: 2

                        Text {
                            objectName: "todoGroupTitle"
                            width: parent.width
                            text: group.modelData.title.toUpperCase()
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            font.letterSpacing: 0.5
                            color: groupMouse.containsMouse ? theme.highlight : root.muted
                            topPadding: 4
                            MouseArea {
                                id: groupMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !!(root.bridge && (group.modelData.eventId || group.modelData.clientId))
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (group.modelData.eventId)
                                        root.bridge.openEvent(group.modelData.eventId, group.modelData.dayMs)
                                    else if (group.modelData.clientId)
                                        root.bridge.openClient(group.modelData.clientId)
                                }
                            }
                        }

                        Repeater {
                            model: group.modelData.todos
                            delegate: Item {
                                id: todoRow
                                required property var modelData
                                property bool editing: false
                                width: group.width
                                height: Math.max(28, rowLayout.implicitHeight + 6)

                                MouseArea {
                                    id: rowHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }

                                RowLayout {
                                    id: rowLayout
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 8

                                    Rectangle {
                                        objectName: "todoCheck"
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        radius: 4
                                        color: todoRow.modelData.done ? "#4caf7d" : "transparent"
                                        border.width: 1.5
                                        border.color: todoRow.modelData.done ? "#4caf7d" : root.muted
                                        Text {
                                            anchors.centerIn: parent
                                            visible: todoRow.modelData.done
                                            text: "✓"
                                            font.pixelSize: 12
                                            color: "white"
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: if (root.bridge) root.bridge.setTodoDone(todoRow.modelData.id, !todoRow.modelData.done)
                                        }
                                    }

                                    Text {
                                        visible: !todoRow.editing
                                        Layout.fillWidth: true
                                        text: todoRow.modelData.title
                                        wrapMode: Text.Wrap
                                        font.pixelSize: 13
                                        font.strikeout: todoRow.modelData.done
                                        color: todoRow.modelData.done ? root.muted : theme.text
                                        MouseArea {
                                            anchors.fill: parent
                                            onDoubleClicked: {
                                                todoRow.editing = true
                                                editor.text = todoRow.modelData.title
                                                editor.forceActiveFocus()
                                                editor.selectAll()
                                            }
                                        }
                                    }

                                    TextField {
                                        id: editor
                                        objectName: "todoTitleEditor"
                                        visible: todoRow.editing
                                        Layout.fillWidth: true
                                        implicitHeight: 28
                                        font.pixelSize: 13
                                        color: theme.text
                                        background: Rectangle { radius: 6; color: theme.base; border.color: theme.highlight }
                                        function finishEdit() {
                                            if (!todoRow.editing) return
                                            const id = todoRow.modelData.id
                                            const title = text
                                            todoRow.editing = false
                                            if (root.bridge) root.bridge.setTodoTitle(id, title)
                                        }
                                        onAccepted: finishEdit()
                                        onEditingFinished: finishEdit()
                                        Keys.onEscapePressed: todoRow.editing = false
                                    }

                                    ToolButton {
                                        objectName: "todoRename"
                                        visible: !todoRow.editing
                                        text: "✎"
                                        implicitWidth: 28
                                        implicitHeight: 28
                                        Accessible.name: qsTr("Rename")
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Rename")
                                        onClicked: {
                                            todoRow.editing = true
                                            editor.text = todoRow.modelData.title
                                            editor.forceActiveFocus()
                                            editor.selectAll()
                                        }
                                    }

                                    Text {
                                        visible: todoRow.modelData.carriedFrom.length > 0 && !todoRow.editing
                                        text: qsTr("from %1").arg(todoRow.modelData.carriedFrom)
                                        font.pixelSize: 11
                                        color: root.muted
                                    }

                                    Text {
                                        objectName: "todoRemove"
                                        visible: rowHover.containsMouse && !todoRow.editing
                                        text: "✕"
                                        font.pixelSize: 12
                                        color: removeMouse.containsMouse ? "#e0594f" : root.muted
                                        MouseArea {
                                            id: removeMouse
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: if (root.bridge) root.bridge.removeTodo(todoRow.modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: root.groups.length === 0
                    width: parent.width
                    text: qsTr("No to-dos")
                    color: root.muted
                    font.pixelSize: 13
                }

                Text {
                    objectName: "todoAddLink"
                    visible: !root.adding
                    text: "+ " + qsTr("Add to-do")
                    font.pixelSize: 13
                    color: theme.highlight
                    topPadding: 4
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.startAdding()
                    }
                }

                ColumnLayout {
                    visible: root.adding
                    width: parent.width
                    spacing: 6
                    TextField {
                        id: newTitle
                        objectName: "todoNewTitle"
                        Layout.fillWidth: true
                        implicitHeight: 30
                        font.pixelSize: 13
                        color: theme.text
                        placeholderText: qsTr("What needs to be done?")
                        placeholderTextColor: root.muted
                        background: Rectangle { radius: 6; color: theme.base; border.color: newTitle.activeFocus ? theme.highlight : theme.mid }
                        onAccepted: root.saveNew()
                        Keys.onEscapePressed: root.adding = false
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        ComboStyle {
                            id: clientBox
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            model: root.clientOptions
                            textRole: "name"
                        }
                        ComboStyle {
                            id: meetingBox
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            model: root.meetingOptions
                            textRole: "label"
                            onActivated: root.syncClientToMeeting()
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Button {
                            id: saveButton
                            text: qsTr("Add")
                            implicitHeight: 28
                            horizontalPadding: 14
                            enabled: newTitle.text.trim().length > 0
                            onClicked: root.saveNew()
                            background: Rectangle { radius: 6; color: theme.highlight; opacity: saveButton.enabled ? 1 : 0.4 }
                            contentItem: Text { text: saveButton.text; color: theme.highlightedText; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                        Button {
                            id: doneButton
                            text: qsTr("Done")
                            implicitHeight: 28
                            horizontalPadding: 14
                            onClicked: root.adding = false
                            background: Rectangle { radius: 6; color: doneButton.hovered ? theme.mid : theme.base; border.color: theme.mid }
                            contentItem: Text { text: doneButton.text; color: theme.text; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
            }
        }
    }
}
