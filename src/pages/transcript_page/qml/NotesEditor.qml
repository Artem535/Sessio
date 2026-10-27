import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlNotesEditorDialog"
    property bool dark: false
    property bool preview: false
    readonly property var info: workspaceBridge.state
    SystemPalette { id: theme; colorGroup: SystemPalette.Active }
    readonly property color paper: theme.base
    readonly property color ink: theme.text
    readonly property color muted: Qt.tint(paper, Qt.rgba(ink.r, ink.g, ink.b, 0.65))
    readonly property color line: theme.mid
    readonly property color panel: theme.window
    readonly property color accent: theme.highlight
    color: paper
    signal closeRequested()
    signal saved()

    component Copy: Text {
        color: root.ink
        font.pixelSize: 14
        wrapMode: Text.Wrap
        lineHeight: 1.6
        textFormat: Text.PlainText
    }
    component Action: Button {
        id: control
        property bool primary: false
        property bool quiet: false
        font.pixelSize: 14
        padding: 10
        horizontalPadding: 15
        implicitHeight: 42
        background: Rectangle {
            radius: 8
            color: control.primary ? root.accent : control.hovered ? root.line : control.quiet ? "transparent" : root.panel
            border.width: control.primary || control.quiet ? 0 : 1
            border.color: control.activeFocus ? root.accent : root.line
            opacity: control.enabled ? 1 : 0.45
        }
        contentItem: Text {
            text: control.text
            color: control.primary ? theme.highlightedText : control.quiet ? root.muted : root.ink
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: control.enabled ? 1 : 0.45
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.width < 700 ? 20 : 28
            spacing: 20
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Copy { text: qsTr("Meeting notes"); font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Copy { text: (root.info.client || "") + " · " + (root.info.date || ""); color: root.muted; font.pixelSize: 13; Layout.fillWidth: true }
            }
            Action { objectName: "qmlEditorClose"; text: "×"; font.pixelSize: 24; quiet: true; implicitWidth: 40; Accessible.name: qsTr("Close editor"); onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: root.width < 700 ? 20 : 28
                spacing: 12
                ComboBox {
                    id: noteSelector
                    objectName: "qmlEditorNoteSelector"
                    visible: workspaceBridge.notes.length > 0
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "id"
                    model: [{id: "", label: qsTr("New note")}].concat(workspaceBridge.notes.map((n, i) => ({
                        id: n.id, label: qsTr("Note") + " " + (i + 1) + " · " + String(n.body_markdown || "").replace(/\s+/g, " ").slice(0, 60)
                    })))
                    currentIndex: Math.max(0, model.findIndex(n => n.id === workspaceBridge.editorNoteId))
                    onActivated: {
                        workspaceBridge.selectEditorNote(currentValue)
                        currentIndex = Qt.binding(() => Math.max(0, model.findIndex(n => n.id === workspaceBridge.editorNoteId)))
                    }
                    palette.button: root.panel
                    palette.buttonText: root.ink
                    palette.base: root.paper
                    palette.text: root.ink
                    palette.highlight: root.accent
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Action { text: "B"; font.bold: true; quiet: true; enabled: !root.preview && editor.selectionStart !== editor.selectionEnd; implicitWidth: 36; onClicked: workspaceBridge.format(editor.selectionStart, editor.selectionEnd, "bold") }
                    Action { text: "I"; font.italic: true; quiet: true; enabled: !root.preview && editor.selectionStart !== editor.selectionEnd; implicitWidth: 36; onClicked: workspaceBridge.format(editor.selectionStart, editor.selectionEnd, "italic") }
                    Action { text: "☷"; quiet: true; enabled: !root.preview; implicitWidth: 36; onClicked: workspaceBridge.format(editor.selectionStart, editor.selectionEnd, "list") }
                    Item { Layout.fillWidth: true }
                    Action { text: root.preview ? qsTr("Editor") : qsTr("Preview"); quiet: true; onClicked: root.preview = !root.preview }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.line }
                ScrollView {
                    id: editorScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: editor
                        objectName: "qmlModalNoteEditor"
                        width: editorScroll.availableWidth
                        height: Math.max(editorScroll.availableHeight, implicitHeight)
                        readOnly: root.preview
                        color: root.ink
                        selectionColor: root.accent
                        selectedTextColor: theme.highlightedText
                        font.pixelSize: 17
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.RichText
                        selectByMouse: true
                        persistentSelection: true
                        placeholderText: qsTr("What is important to keep from this meeting?")
                        placeholderTextColor: root.muted
                        padding: 4
                        topPadding: 12
                        background: null
                        Component.onCompleted: { workspaceBridge.attachDocument(textDocument); forceActiveFocus() }
                    }
                }
                Repeater {
                    model: root.info.savedAttachments || []
                    Action {
                        required property var modelData
                        text: "↗ " + modelData.file_name
                        quiet: true
                        onClicked: workspaceBridge.openAttachment(workspaceBridge.editorNoteId, modelData.id)
                    }
                }
                Repeater {
                    model: root.info.attachments || []
                    RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Copy { text: modelData.name; color: root.muted; Layout.fillWidth: true }
                        Action { text: "×"; quiet: true; onClicked: workspaceBridge.removeAttachment(index) }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.line }
                Action { text: "+ " + qsTr("Attach files"); quiet: true; onClicked: workspaceBridge.attachFiles() }
            }
            Rectangle {
                objectName: "qmlEditorContext"
                visible: root.width >= 850
                Layout.preferredWidth: root.width >= 1200 ? 360 : 330
                Layout.fillHeight: true
                color: root.panel
                Rectangle { width: 1; height: parent.height; color: root.line }
                ScrollView {
                    id: referenceScroll
                    anchors.fill: parent
                    anchors.margins: 24
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: referenceScroll.availableWidth
                        spacing: 16
                        Copy { text: qsTr("Meeting context"); color: root.accent; font.pixelSize: 12; Layout.fillWidth: true }
                        Copy { text: "✧ " + qsTr("Meeting summary"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Copy { text: root.info.summary || qsTr("No summary yet"); color: root.muted; Layout.fillWidth: true }
                        Action { text: "+ " + qsTr("Add to my notes"); enabled: (root.info.summary || "").length > 0; onClicked: workspaceBridge.appendSummary() }
                        Copy { text: qsTr("For the next meeting"); font.weight: Font.DemiBold; Layout.topMargin: 14; Layout.fillWidth: true; visible: (root.info.agreements || []).length > 0 }
                        Repeater {
                            model: root.info.agreements || []
                            Copy { required property var modelData; text: "• " + modelData.text; color: root.muted; Layout.fillWidth: true; visible: modelData.active !== false }
                        }
                        Copy { text: qsTr("Key points"); font.weight: Font.DemiBold; Layout.topMargin: 14; Layout.fillWidth: true; visible: (root.info.points || []).length > 0 }
                        Repeater {
                            model: root.info.points || []
                            Copy { required property var modelData; text: "• " + modelData.text; color: root.muted; Layout.fillWidth: true }
                        }
                        Copy { text: qsTr("AI suggestions help recall the meeting. The note on the left is your own text."); color: root.muted; font.pixelSize: 12; Layout.topMargin: 12; Layout.fillWidth: true }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.line }
        GridLayout {
            Layout.fillWidth: true
            Layout.margins: root.width < 700 ? 20 : 28
            columns: root.width < 700 ? 1 : 2
            columnSpacing: 20
            rowSpacing: 12
            Copy {
                objectName: "qmlEditorSaveStatus"
                text: workspaceBridge.saveStatus || (workspaceBridge.editorDirty ? qsTr("Unsaved changes") : qsTr("No unsaved changes"))
                color: root.muted
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Action { text: qsTr("Cancel"); onClicked: root.closeRequested() }
                Action {
                    objectName: "qmlModalSaveNote"
                    text: qsTr("Save note")
                    primary: true
                    enabled: editor.length > 0 || (root.info.attachments || []).length > 0 || (root.info.savedAttachments || []).length > 0
                    onClicked: { if (workspaceBridge.saveEditorSession()) root.saved() }
                }
            }
        }
    }
}
