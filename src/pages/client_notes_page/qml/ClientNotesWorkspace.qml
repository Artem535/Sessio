import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlClientNotesWorkspace"
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
    readonly property var info: journalBridge ? journalBridge.document : ({})
    readonly property var participantsModel: journalBridge ? journalBridge.participants : []
    readonly property var journalModel: journalBridge ? journalBridge.journal : []
    readonly property string activeParticipantId: journalBridge ? journalBridge.participantId : ""
    readonly property string draftText: journalBridge ? journalBridge.draft : ""
    readonly property var pendingFilesModel: journalBridge ? journalBridge.pendingFiles : []
    readonly property string statusText: journalBridge ? journalBridge.status : ""
    property bool composing: true
    property bool preview: false
    property bool historyVisible: width >= 850
    property string selectedParticipant: activeParticipantId
    onSelectedParticipantChanged: { search.text = ""; filter.currentIndex = 0; root.composing = true }
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))
    readonly property color soft: Qt.tint(theme.base, Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.14))
    component Copy: Text {
        color: theme.text
        textFormat: Text.PlainText
        font.pixelSize: 14
        wrapMode: Text.Wrap
    }
    component Action: Button {
        id: control
        property bool primary: false
        implicitHeight: 40
        padding: 10
        horizontalPadding: 15
        background: Rectangle {
            radius: 8
            color: control.primary ? theme.highlight : control.hovered ? theme.mid : theme.window
            border.color: control.activeFocus ? theme.highlight : theme.mid
            opacity: control.enabled ? 1 : 0.4
        }
        contentItem: Text {
            text: control.text
            font.pixelSize: 14
            color: control.primary ? theme.highlightedText : theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: control.enabled ? 1 : 0.4
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            visible: !root.externalHeader
            Layout.fillWidth: true
            Layout.margins: 24
            spacing: 16
            Copy { text: qsTr("Notes"); font.pixelSize: 24; font.weight: Font.DemiBold }
            ThemedSelect {
                id: participant
                objectName: "qmlNotesParticipant"
                Layout.fillWidth: true
                Layout.maximumWidth: 380
                model: [{id: "", title: qsTr("Select participant")}].concat(root.participantsModel)
                textRole: "title"
                valueRole: "id"
                currentIndex: Math.max(0, model.findIndex(p => p.id === root.activeParticipantId))
                onActivated: {
                    if (currentValue && journalBridge) journalBridge.selectParticipant(currentValue)
                    currentIndex = Qt.binding(() => Math.max(0, model.findIndex(p => p.id === root.activeParticipantId)))
                }
                Accessible.name: qsTr("Participant")
                palette.button: theme.window
                palette.buttonText: theme.text
            }
            Item { Layout.fillWidth: true }
            Action { text: qsTr("Client card"); visible: root.width > 700; enabled: root.activeParticipantId.length > 0; onClicked: if (journalBridge) journalBridge.openWorkspaceClient() }
        }
        Rectangle { visible: !root.externalHeader; Layout.fillWidth: true; implicitHeight: 1; color: theme.mid }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                visible: root.historyVisible
                Layout.preferredWidth: root.width >= 1200 ? 300 : 250
                Layout.fillHeight: true
                color: theme.window
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Action {
                        Layout.fillWidth: true
                        text: "+ " + qsTr("New note")
                        primary: true
                        enabled: root.activeParticipantId.length > 0
                        onClicked: { root.composing = true; editor.forceActiveFocus() }
                    }
                    Action { text: qsTr("Export"); Layout.fillWidth: true; enabled: root.activeParticipantId.length > 0; onClicked: if (journalBridge) journalBridge.exportWorkspaceJournal() }
                    TextField {
                        id: search
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search notes and summaries…")
                        placeholderTextColor: root.muted
                        color: theme.text
                        onTextChanged: if (journalBridge) journalBridge.searchJournal(text, filter.currentIndex)
                        background: Rectangle { color: theme.base; radius: 8; border.color: theme.mid }
                    }
                    ThemedSelect {
                        id: filter
                        Layout.fillWidth: true
                        model: [qsTr("All materials"), qsTr("Sessions and protocols"), qsTr("Free notes"), qsTr("Agreements")]
                        onActivated: if (journalBridge) journalBridge.searchJournal(search.text, currentIndex)
                        palette.button: theme.window
                        palette.buttonText: theme.text
                    }
                    ListView {
                        objectName: "qmlNotesHistory"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.journalModel
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: entry
                            required property var modelData
                            width: ListView.view.width
                            padding: 14
                            onClicked: { if (journalBridge) journalBridge.selectJournalEntry(modelData.key); root.composing = false; if (root.width < 850) root.historyVisible = false }
                            background: Rectangle {
                                radius: 8
                                color: root.info.key === entry.modelData.key ? theme.base : entry.hovered ? theme.mid : "transparent"
                                Rectangle { width: 3; height: parent.height - 16; anchors.verticalCenter: parent.verticalCenter; color: theme.highlight; visible: root.info.key === entry.modelData.key }
                            }
                            contentItem: ColumnLayout {
                                spacing: 8
                                Copy { text: entry.modelData.date; color: root.muted; font.pixelSize: 11; Layout.fillWidth: true }
                                Copy { text: entry.modelData.title; font.weight: Font.DemiBold; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true }
                                Copy { text: entry.modelData.preview; color: root.muted; font.pixelSize: 12; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true }
                                Copy { text: entry.modelData.session ? qsTr("Session") : qsTr("Free note"); color: theme.highlight; font.pixelSize: 11 }
                            }
                        }
                    }
                    Copy { visible: root.journalModel.length === 0; text: qsTr("No notes found"); color: root.muted; Layout.fillWidth: true }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.mid; visible: root.historyVisible }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 24
                spacing: 18
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Action { text: qsTr("History"); visible: root.width < 850; onClicked: root.historyVisible = !root.historyVisible }
                    Action { text: qsTr("Read"); enabled: !!root.info.key; onClicked: root.composing = false }
                    Action { text: qsTr("New note"); primary: root.composing; enabled: root.activeParticipantId.length > 0; onClicked: root.composing = true }
                    Action {
                        text: qsTr("Open meeting")
                        visible: root.info.meeting === true
                        onClicked: { if ((root.info.materials || []).length > 1) materialsMenu.open(); else if (journalBridge) journalBridge.openWorkspaceMeeting("") }
                        Menu {
                            id: materialsMenu
                            background: Rectangle { implicitWidth: 200; color: theme.base; border.color: theme.mid; radius: 8 }
                            Repeater {
                                model: root.info.materials || []
                                MenuItem {
                                    id: materialItem
                                    required property var modelData
                                    text: modelData.title
                                    contentItem: Copy { text: materialItem.text; font.pixelSize: 14 }
                                    background: Rectangle { color: materialItem.highlighted ? root.soft : "transparent"; radius: 6 }
                                    onTriggered: if (journalBridge) journalBridge.openWorkspaceMeeting(modelData.id)
                                }
                            }
                        }
                    }
                    Action {
                        text: qsTr("Open transcript")
                        visible: root.info.transcript === true
                        onClicked: { if ((root.info.transcripts || []).length > 1) transcriptsMenu.open(); else if (journalBridge) journalBridge.openWorkspaceTranscript("") }
                        Menu {
                            id: transcriptsMenu
                            background: Rectangle { implicitWidth: 200; color: theme.base; border.color: theme.mid; radius: 8 }
                            Repeater {
                                model: root.info.transcripts || []
                                MenuItem {
                                    id: transcriptItem
                                    required property var modelData
                                    text: qsTr("Transcript") + " " + modelData.title
                                    contentItem: Copy { text: transcriptItem.text; font.pixelSize: 14 }
                                    background: Rectangle { color: transcriptItem.highlighted ? root.soft : "transparent"; radius: 6 }
                                    onTriggered: if (journalBridge) journalBridge.openWorkspaceTranscript(modelData.id)
                                }
                            }
                        }
                    }
                }
                ScrollView {
                    id: reading
                    visible: !root.composing
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    TextArea {
                        width: reading.availableWidth
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.MarkdownText
                        text: root.info.markdown || (root.activeParticipantId ? qsTr("Select a note or create a new one.") : qsTr("Select a participant to view their notes."))
                        font.pixelSize: 17
                        wrapMode: TextEdit.Wrap
                        color: theme.text
                        background: null
                        padding: 4
                    }
                }
                Repeater {
                    model: root.composing ? [] : (root.info.attachments || [])
                    Action {
                        required property var modelData
                        required property int index
                        text: "↗ " + modelData.name
                        onClicked: if (journalBridge) journalBridge.openWorkspaceAttachment(index)
                    }
                }
                ColumnLayout {
                    visible: root.composing
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    RowLayout {
                        Layout.fillWidth: true
                        Copy { text: qsTr("New note"); font.pixelSize: 21; Layout.fillWidth: true }
                        Action { text: "B"; enabled: !root.preview; onClicked: root.format("**") }
                        Action { text: "I"; enabled: !root.preview; onClicked: root.format("*") }
                        Action { text: "•"; enabled: !root.preview; onClicked: root.format("\n- ", "") }
                        Action { text: root.preview ? qsTr("Editor") : qsTr("Preview"); onClicked: root.preview = !root.preview }
                    }
                    Copy { text: root.info.client || ""; color: root.muted; Layout.fillWidth: true }
                    ScrollView {
                        id: editScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        Column {
                        width: editScroll.availableWidth
                        TextArea {
                            id: editor
                            objectName: "qmlPersistentNoteEditor"
                            width: editScroll.availableWidth
                            height: Math.max(editScroll.availableHeight, implicitHeight)
                            enabled: root.activeParticipantId.length > 0
                            readOnly: root.preview
                            text: root.draftText
                            onTextChanged: if (!root.preview && journalBridge && text !== root.draftText) journalBridge.setDraft(text)
                            textFormat: TextEdit.PlainText
                            visible: !root.preview
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: theme.text
                            font.pixelSize: 17
                            placeholderText: qsTr("Write a note in Markdown…")
                            placeholderTextColor: root.muted
                            background: null
                        }
                        TextArea {
                            width: editScroll.availableWidth
                            visible: root.preview
                            readOnly: true
                            text: root.draftText
                            textFormat: TextEdit.MarkdownText
                            color: theme.text
                            font.pixelSize: 17
                            wrapMode: TextEdit.Wrap
                            background: null
                        }
                        }
                    }
                    Repeater {
                        model: root.pendingFilesModel
                        RowLayout {
                            required property string modelData
                            required property int index
                            Copy { text: modelData; color: root.muted; Layout.fillWidth: true }
                            Action { text: "×"; onClicked: if (journalBridge) journalBridge.removeWorkspaceAttachment(index) }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Action { text: qsTr("Attach files"); onClicked: if (journalBridge) journalBridge.attachWorkspaceFiles() }
                        Action { text: root.info.link || qsTr("Link to a session"); onClicked: if (journalBridge) journalBridge.linkWorkspaceSession() }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.mid }
                    RowLayout {
                        Layout.fillWidth: true
                        Copy { text: root.statusText; color: root.muted; Layout.fillWidth: true }
                        Action { objectName: "qmlSaveClientNote"; text: qsTr("Add note"); primary: true; enabled: root.draftText.trim().length > 0 || root.pendingFilesModel.length > 0; onClicked: if (journalBridge) journalBridge.saveWorkspaceNote() }
                    }
                }
            }
        }
    }
    function format(prefix, suffix) {
        if (suffix === undefined) suffix = prefix
        const start = editor.selectionStart
        const end = editor.selectionEnd
        const selected = editor.selectedText
        editor.remove(start, end)
        editor.insert(start, prefix + selected + suffix)
        editor.forceActiveFocus()
    }
}
