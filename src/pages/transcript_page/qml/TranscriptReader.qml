import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlTranscriptReader"
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
    readonly property var info: readerBridge ? readerBridge.reader : ({})
    readonly property var historyModel: readerBridge ? readerBridge.readerHistory : []
    readonly property var rawPhrases: readerBridge ? readerBridge.readerPhrases : []
    property string query: ""
    property string speaker: ""
    property int textSize: 17
    property string focusedPhrase: ""
    property bool historyVisible: width >= 950 && !root.info.notesVisible
    readonly property var phrases: rawPhrases.filter(p => (!speaker || p.speaker === speaker) && (!query || p.text.toLowerCase().includes(query.toLowerCase())))
    readonly property color muted: Qt.tint(theme.base, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, 0.65))
    readonly property color soft: Qt.tint(theme.base, Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.14))
    component Copy: Text { color: theme.text; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 14 }
    component Action: Button {
        id: control
        property bool primary: false
        implicitHeight: 40
        padding: 10
        horizontalPadding: 14
        background: Rectangle { radius: 8; color: control.primary ? theme.highlight : control.hovered ? theme.mid : theme.window; border.color: control.activeFocus ? theme.highlight : theme.mid; opacity: control.enabled ? 1 : 0.4 }
        contentItem: Text { text: control.text; color: control.primary ? theme.highlightedText : theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: control.enabled ? 1 : 0.4 }
    }
    component ThemedMenuItem: MenuItem {
        id: menuItem
        contentItem: Copy {
            text: menuItem.text
            font.pixelSize: 14
            opacity: menuItem.enabled ? 1 : 0.4
        }
        background: Rectangle {
            radius: 6
            color: menuItem.highlighted ? root.soft : "transparent"
        }
    }
    Connections {
        target: readerBridge
        function onReaderPhraseFocused(id) {
            root.query = ""; search.text = ""; root.speaker = ""; speakers.currentIndex = 0
            root.focusedPhrase = id
            const index = root.phrases.findIndex(p => p.id === id)
            if (index >= 0) rows.positionViewAtIndex(index, ListView.Center)
            Qt.callLater(function() {
                const idx = root.phrases.findIndex(p => p.id === id)
                if (idx >= 0) rows.positionViewAtIndex(idx, ListView.Center)
            })
        }
    }
    onInfoChanged: { query = ""; speaker = ""; if (search) search.text = ""; if (speakers) speakers.currentIndex = 0 }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: root.historyVisible
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: theme.window
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                Action { visible: !root.externalHeader; text: qsTr("Back to transcripts"); Layout.fillWidth: true; onClicked: if (readerBridge) readerBridge.readerAction("back") }
                Copy { text: qsTr("Recordings"); font.weight: Font.DemiBold }
                TextField { id: historySearch; Layout.fillWidth: true; color: theme.text; placeholderText: qsTr("Find a recording"); placeholderTextColor: root.muted; background: Rectangle { color: theme.base; border.color: theme.mid; radius: 8 } }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.historyModel.filter(r => (r.title + " " + r.date).toLowerCase().includes(historySearch.text.toLowerCase()))
                    spacing: 8
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: record
                        required property var modelData
                        width: ListView.view.width
                        padding: 14
                        onClicked: if (readerBridge) readerBridge.selectReaderTranscript(modelData.id)
                        background: Rectangle { radius: 8; color: record.modelData.id === root.info.id ? theme.base : record.hovered ? theme.mid : "transparent" }
                        contentItem: ColumnLayout {
                            spacing: 9
                            Copy { text: record.modelData.date; font.pixelSize: 12; color: root.muted; Layout.fillWidth: true }
                            Copy { text: record.modelData.title; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
        Rectangle { implicitWidth: 1; Layout.fillHeight: true; color: theme.mid; visible: root.historyVisible }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                spacing: 12
                RowLayout {
                    visible: !root.externalHeader
                    Layout.fillWidth: true
                    Copy { text: qsTr("Transcript"); font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Action { text: qsTr("History"); visible: root.width < 950 || root.info.notesVisible === true; onClicked: root.historyVisible = !root.historyVisible }
                    Action { text: qsTr("Call notes"); primary: root.info.notesVisible === true; visible: root.info.hasNotes === true; onClicked: if (readerBridge) readerBridge.readerAction("notes") }
                    Action {
                        text: "⋯"
                        Accessible.name: qsTr("More actions")
                        onClicked: more.open()
                        Menu {
                            id: more
                            background: Rectangle { implicitWidth: 190; color: theme.base; border.color: theme.mid; radius: 8 }
                            ThemedMenuItem { text: qsTr("Export text"); enabled: !!root.info.id; onTriggered: if (readerBridge) readerBridge.readerAction("export") }
                            ThemedMenuItem { text: qsTr("Attach clients"); enabled: root.info.canAttach === true; onTriggered: if (readerBridge) readerBridge.readerAction("attach") }
                            ThemedMenuItem { text: qsTr("Mark reviewed"); enabled: root.info.canReview === true; onTriggered: if (readerBridge) readerBridge.readerAction("review") }
                            ThemedMenuItem { text: qsTr("Edit transcript"); enabled: root.info.canDelete === true; onTriggered: if (readerBridge) readerBridge.readerAction("advanced") }
                            ThemedMenuItem { text: qsTr("Delete transcript"); enabled: root.info.canDelete === true; onTriggered: if (readerBridge) readerBridge.readerAction("delete") }
                        }
                    }
                }
                Copy { text: root.info.client || ""; color: root.muted; Layout.fillWidth: true }
                Copy { text: (root.info.status || "") + " · " + (root.info.metadata || ""); color: root.muted; font.pixelSize: 12; Layout.fillWidth: true }
                Copy { text: root.info.notice || ""; visible: text.length > 0; color: root.muted; Layout.fillWidth: true }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.mid }
            Flow {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 10
                TextField { id: search; objectName: "qmlTranscriptSearch"; width: Math.max(150, Math.min(360, root.width - 380)); placeholderText: qsTr("Search transcript…"); color: theme.text; placeholderTextColor: root.muted; onTextChanged: root.query = text; background: Rectangle { color: theme.window; radius: 8; border.color: theme.mid } }
                ThemedSelect {
                    id: speakers
                    model: [qsTr("All speakers")].concat([...new Set(root.rawPhrases.map(p => p.speaker))])
                    onActivated: root.speaker = currentIndex === 0 ? "" : currentText
                    palette.button: theme.window
                    palette.buttonText: theme.text
                }
                Action { text: "A−"; onClicked: root.textSize = Math.max(14, root.textSize - 1) }
                Action { text: "A+"; onClicked: root.textSize = Math.min(25, root.textSize + 1) }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.mid }
            ListView {
                id: rows
                objectName: "qmlTranscriptPhrases"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.phrases
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: phraseLayout.implicitHeight + 42
                    color: root.focusedPhrase === modelData.id ? Qt.tint(theme.base, Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.12)) : theme.base
                    ColumnLayout {
                        id: phraseLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 24
                        spacing: 12
                        RowLayout {
                            Copy { text: modelData.time; font.pixelSize: 12; color: root.muted }
                            Copy { text: "●"; color: modelData.practitioner ? theme.highlight : "#8ebeb9"; font.pixelSize: 10 }
                            Copy {
                                text: modelData.speaker; font.weight: Font.DemiBold; font.pixelSize: 13
                                TapHandler {
                                    enabled: root.info.canDelete === true
                                    acceptedButtons: Qt.RightButton
                                    onTapped: speakerMenu.popup()
                                }
                                Menu {
                                    id: speakerMenu
                                    background: Rectangle { implicitWidth: 190; color: theme.base; border.color: theme.mid; radius: 8 }
                                    ThemedMenuItem { text: qsTr("Rename speaker…"); onTriggered: if (readerBridge) readerBridge.promptSpeakerRename(modelData.trackRole) }
                                }
                            }
                        }
                        TextEdit {
                            text: modelData.text
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            color: theme.text
                            selectionColor: theme.highlight
                            selectedTextColor: theme.highlightedText
                            font.pixelSize: root.textSize
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.mid; opacity: 0.35 }
                }
                Copy { anchors.centerIn: parent; visible: rows.count === 0; text: qsTr("No matching phrases"); color: root.muted }
            }
        }
    }
}
