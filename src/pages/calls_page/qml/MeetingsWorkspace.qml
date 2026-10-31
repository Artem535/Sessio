import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlMeetingsWorkspace"
    property bool externalHeader: false
    SystemPalette { id: theme }
    component ThemedSelect: ComboBox {
        id: control
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 34
        font.pixelSize: 14
        palette.window: theme.window
        palette.base: theme.base
        palette.text: theme.text
        palette.button: theme.window
        palette.buttonText: theme.text
        palette.highlight: theme.highlight
        palette.highlightedText: theme.highlightedText
        contentItem: Text {
            text: control.displayText
            font: control.font
            color: theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            text: "⌄"
            color: theme.text
            font.pixelSize: 18
            x: control.width - width - 12
            y: (control.height - height) / 2
        }
        background: Rectangle {
            radius: 8
            color: theme.window
            border.color: control.activeFocus ? theme.highlight : theme.mid
        }
        delegate: ItemDelegate {
            id: option
            required property int index
            required property var modelData
            width: control.width
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                text: control.textRole ? option.modelData[control.textRole] : option.modelData
                color: option.highlighted ? theme.highlightedText : theme.text
                font: control.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: 5; color: option.highlighted ? theme.highlight : theme.window }
        }
        popup: Popup {
            y: control.height + 6
            width: control.width
            padding: 6
            implicitHeight: Math.min(contentItem.implicitHeight + 12, 320)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                ScrollBar.vertical: ScrollBar {}
            }
            background: Rectangle { radius: 8; color: theme.window; border.color: theme.mid }
        }
    }
    color: theme.window
    property string selectedId: ""
    property bool upcoming: true
    property string query: ""
    readonly property var records: meetingsBridge ? (upcoming ? meetingsBridge.meetings : meetingsBridge.materials) : []
    readonly property var selected: records.find(r => r.id === selectedId) || records[0] || ({})
    readonly property string callerName: meetingsBridge ? meetingsBridge.name : ""
    readonly property string errorText: meetingsBridge ? meetingsBridge.error : ""
    readonly property string inviteCode: meetingsBridge ? meetingsBridge.code : ""
    readonly property string invitePasscode: meetingsBridge ? meetingsBridge.passcode : ""
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))
    component Copy: Text { color: theme.text; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 14 }
    Connections {
        target: meetingsBridge
        function onMeetingPreselected(ref) { modeSelector.currentIndex = 0; root.upcoming = true; root.selectedId = ref }
        function onInvitationPrefilled() {
            joinMode.currentIndex = 0
            code.text = root.inviteCode
            passcode.text = root.invitePasscode
            joinDialog.open()
        }
    }
    component Action: Button {
        id: control
        property bool primary: false
        implicitHeight: 42
        padding: 10
        horizontalPadding: 16
        background: Rectangle { radius: 8; color: control.primary ? theme.highlight : control.hovered ? theme.mid : theme.window; border.color: control.activeFocus ? theme.highlight : theme.mid; opacity: control.enabled ? 1 : 0.4 }
        contentItem: Text { text: control.text; color: control.primary ? theme.highlightedText : theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: control.enabled ? 1 : 0.4 }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            visible: !root.externalHeader
            Layout.fillWidth: true
            Layout.margins: 24
            Copy { text: qsTr("Meetings"); font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Action { text: qsTr("Join a meeting"); primary: true; onClicked: joinDialog.open() }
        }
        Rectangle { visible: !root.externalHeader; Layout.fillWidth: true; implicitHeight: 1; color: theme.mid }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: root.width > 1100 ? 310 : 250
                Layout.fillHeight: true
                color: theme.window
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    ThemedSelect {
                        id: modeSelector
                        objectName: "qmlMeetingMode"
                        Layout.fillWidth: true
                        model: [qsTr("Your calls today"), qsTr("Meeting history")]
                        onActivated: { root.upcoming = currentIndex === 0; root.selectedId = "" }
                        palette.button: theme.window
                        palette.buttonText: theme.text
                    }
                    TextField { Layout.fillWidth: true; placeholderText: qsTr("Search meetings…"); color: theme.text; placeholderTextColor: root.muted; onTextChanged: root.query = text; background: Rectangle { color: theme.base; radius: 8; border.color: theme.mid } }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.records.filter(r => (r.title + " " + r.date).toLowerCase().includes(root.query.toLowerCase()))
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: record
                            required property var modelData
                            width: ListView.view.width
                            padding: 14
                            onClicked: root.selectedId = modelData.id
                            background: Rectangle { radius: 8; color: root.selected.id === record.modelData.id ? theme.base : record.hovered ? theme.mid : "transparent"; Rectangle { width: 3; height: parent.height - 16; anchors.verticalCenter: parent.verticalCenter; color: theme.highlight; visible: root.selected.id === record.modelData.id } }
                            contentItem: ColumnLayout {
                                spacing: 8
                                Copy { text: record.modelData.date; color: root.muted; font.pixelSize: 12; Layout.fillWidth: true }
                                Copy { text: record.modelData.title; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                Copy { text: root.upcoming ? qsTr("Scheduled") : qsTr("Call materials"); color: theme.highlight; font.pixelSize: 12 }
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.mid }
            ScrollView {
                id: document
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: document.availableWidth
                    spacing: 28
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 28
                        spacing: 20
                        Copy { text: root.selected.title || qsTr("No meetings to show"); font.pixelSize: 25; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Copy { text: root.selected.date || (root.upcoming ? qsTr("Scheduled online meetings appear here.") : qsTr("Materials appear here after a call.")); color: root.muted; Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.mid }
                        Copy { visible: root.upcoming && !!root.selected.id; text: qsTr("Before joining, check your name. Camera and microphone settings are shown on the next screen."); color: root.muted; Layout.fillWidth: true }
                        TextField { id: displayName; visible: root.upcoming; Layout.fillWidth: true; Layout.maximumWidth: 420; text: root.callerName; placeholderText: qsTr("Name in calls"); placeholderTextColor: root.muted; color: theme.text; onTextEdited: if (meetingsBridge) meetingsBridge.name = text; background: Rectangle { radius: 8; color: theme.window; border.color: theme.mid } }
                        Action { visible: root.upcoming && !!root.selected.id; text: qsTr("Check devices and join"); primary: true; enabled: root.selected.enabled === true; onClicked: if (meetingsBridge) meetingsBridge.joinOwn(root.selected.id) }
                        Action { visible: root.upcoming && !root.selected.id; text: qsTr("Join a meeting"); primary: true; onClicked: joinDialog.open() }
                        Copy { text: root.errorText; visible: text.length > 0; color: "#ef7777"; Layout.fillWidth: true }
                        Action { text: qsTr("Open in browser"); visible: root.errorText.length > 0 && meetingsBridge && meetingsBridge.jitsiFallbackUrl.length > 0; onClicked: meetingsBridge.openJitsiFallback() }
                        Copy { visible: !root.upcoming && !!root.selected.summary; text: qsTr("Meeting summary"); font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Copy { visible: !root.upcoming; text: root.selected.summary || ""; font.pixelSize: 17; lineHeight: 1.7; Layout.fillWidth: true }
                        Action { visible: !root.upcoming && !!root.selected.id; text: qsTr("Open notes and materials"); primary: true; onClicked: if (meetingsBridge) meetingsBridge.openMaterial(root.selected.id) }
                        Copy { text: qsTr("Transcripts"); visible: !root.upcoming && (root.selected.transcripts || []).length > 0; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Repeater {
                            model: root.upcoming ? [] : (root.selected.transcripts || [])
                            Action { required property var modelData; text: modelData.title; onClicked: if (meetingsBridge) meetingsBridge.openTranscript(modelData.id) }
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: joinDialog
        objectName: "qmlMeetingInvitation"
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 600)
        modal: true
        title: qsTr("Join a meeting")
        padding: 20
        header: Rectangle {
            color: theme.base
            radius: 12
            implicitHeight: 52
            Copy {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 20
                text: joinDialog.title
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
        }
        background: Rectangle { radius: 12; color: theme.base; border.color: theme.mid }
        contentItem: ColumnLayout {
            spacing: 16
            ComboBox {
                id: joinMode
                objectName: "qmlJoinMode"
                Layout.fillWidth: true
                model: [qsTr("Sessio invitation"), qsTr("Jitsi link")]
            }
            Copy { text: qsTr("Paste the Sessio invitation link, or enter its code and passcode. The invitation selects Jitsi automatically."); visible: joinMode.currentIndex === 0; color: root.muted; font.pixelSize: 14; Layout.fillWidth: true }
            Copy { text: qsTr("Paste a room link from a Jitsi server that allows guest access. Browser sign-in, passwords and lobby admission are not supported here."); visible: joinMode.currentIndex === 1; color: root.muted; font.pixelSize: 14; Layout.fillWidth: true }
            Copy { text: qsTr("To host a Jitsi call: create an event in Calendar, enable Online, select Jitsi, and save. Use Copy invite to send the invitation to participants."); color: root.muted; font.pixelSize: 14; Layout.fillWidth: true }
            TextField {
                Layout.fillWidth: true
                text: root.callerName
                placeholderText: qsTr("Name in calls")
                placeholderTextColor: root.muted
                color: theme.text
                onTextEdited: if (meetingsBridge) meetingsBridge.name = text
                background: Rectangle { radius: 8; color: theme.window; border.color: theme.mid }
            }
            TextField {
                id: code
                objectName: "qmlMeetingCode"
                Layout.fillWidth: true
                text: root.inviteCode
                placeholderText: joinMode.currentIndex === 0 ? qsTr("Invitation code or link") : qsTr("https://meet.example.org/room")
                placeholderTextColor: root.muted
                color: theme.text
                background: Rectangle { radius: 8; color: theme.window; border.color: theme.mid }
            }
            TextField {
                id: passcode
                visible: joinMode.currentIndex === 0
                Layout.fillWidth: true
                text: root.invitePasscode
                placeholderText: qsTr("Passcode")
                placeholderTextColor: root.muted
                color: theme.text
                echoMode: TextInput.Password
                background: Rectangle { radius: 8; color: theme.window; border.color: theme.mid }
            }
            Copy { text: root.errorText; color: "#ef7777"; visible: text.length > 0; Layout.fillWidth: true }
            Action { text: qsTr("Open in browser"); visible: root.errorText.length > 0 && meetingsBridge && meetingsBridge.jitsiFallbackUrl.length > 0; onClicked: meetingsBridge.openJitsiFallback() }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                Action { text: qsTr("Cancel"); onClicked: joinDialog.reject() }
                Action { text: qsTr("Continue"); primary: true; enabled: code.text.trim().length > 0; onClicked: { if (meetingsBridge) { if (joinMode.currentIndex === 0) meetingsBridge.joinInvitation(code.text, passcode.text); else meetingsBridge.joinJitsi(code.text) } if (!root.errorText) joinDialog.close() } }
            }
        }
    }
}
