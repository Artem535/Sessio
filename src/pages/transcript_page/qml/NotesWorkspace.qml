import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "qmlNotesWorkspace"
    property bool dark: false
    property bool sourceOpen: false
    property bool historyOpen: false
    property bool callPanel: false
    // The side panel of a running call leaves the final summary to the post-call window, but
    // an older meeting picked from History is a finished record and shows everything.
    readonly property bool liveCallPanel: callPanel && info.current === true
    property string highlightedSource: ""
    readonly property var info: workspaceBridge ? workspaceBridge.state : ({})
    readonly property var historyModel: workspaceBridge ? workspaceBridge.history : []
    readonly property var notesModel: workspaceBridge ? workspaceBridge.notes : []
    readonly property var activeAgreements: (info.agreements || []).filter(a => a.active !== false && (a.text || "").length > 0)
    readonly property bool summaryShown: !liveCallPanel && info.ended === true && ((info.summary || "").length > 0 || (info.points || []).length > 0 || (info.tasks || []).length > 0
                                         || (info.openQuestions || []).length > 0
                                         || ["error", "running", "queued", "stale", "cancelled", "interrupted", "unavailable"].includes(info.summaryState))
    readonly property var phrasesModel: workspaceBridge ? workspaceBridge.phrases : []
    SystemPalette { id: theme; colorGroup: SystemPalette.Active }
    readonly property color paper: theme.base
    readonly property color ink: theme.text
    readonly property color muted: Qt.tint(paper, Qt.rgba(ink.r, ink.g, ink.b, 0.65))
    readonly property color line: theme.mid
    readonly property color accent: theme.highlight
    readonly property color soft: Qt.tint(paper, Qt.rgba(accent.r, accent.g, accent.b, 0.12))
    color: theme.window
    signal advancedRequested()
    function revealSource(id) {
        highlightedSource = String(id)
        sourceOpen = true
        phraseSearch.clear()
        Qt.callLater(function() {
            for (let i = 0; i < root.phrasesModel.length; ++i)
                if (root.phrasesModel[i].id === highlightedSource) {
                    phrases.positionViewAtIndex(i, ListView.Center)
                    break
                }
        })
    }
    component Action: Button {
        id: control
        property bool primary: false
        property bool quiet: false
        font.pixelSize: 15
        font.weight: Font.Medium
        padding: 10
        horizontalPadding: 14
        implicitHeight: 40
        background: Rectangle {
            radius: 6
            color: control.primary ? root.accent : control.hovered ? root.soft : control.quiet ? "transparent" : root.paper
            border.width: control.primary || control.quiet ? 0 : 1
            border.color: control.activeFocus ? root.accent : root.line
            opacity: control.enabled ? 1 : 0.4
        }
        contentItem: Text {
            text: control.text
            color: control.primary ? theme.highlightedText : control.quiet ? root.muted : root.ink
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: control.enabled ? 1 : 0.4
        }
    }
    component Copy: Text {
        color: root.ink
        font.pixelSize: 16
        wrapMode: Text.Wrap
        lineHeight: 1.5
        textFormat: Text.PlainText
    }
    component AgreementRow: GridLayout {
        required property var modelData
        columns: root.width < 600 ? 1 : 2
        Layout.fillWidth: true
        Copy { text: "•  " + modelData.text; Layout.fillWidth: true }
        Action {
            objectName: "qmlAddToCalendar"
            visible: modelData.canAddToCalendar === true
            text: qsTr("Add to calendar")
            quiet: true
            onClicked: if (workspaceBridge) workspaceBridge.addAgreementToCalendar(modelData.id)
        }
        Action {
            visible: modelData.inCalendar === true
            text: qsTr("In calendar") + " ↗"
            quiet: true
            onClicked: if (workspaceBridge) workspaceBridge.openAgreementEvent(modelData.id)
        }
    }
    component SourceButton: Action {
        property var sources: []
        text: qsTr("View source") + " ↗"
        visible: sources && sources.length > 0 && root.info.sourcesCurrent
        quiet: true
        onClicked: root.revealSource(sources[0])
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: root.width < 600 ? Math.max(86, headerRow.implicitHeight + 44) : 86
            color: "transparent"
            GridLayout {
                id: headerRow
                columns: root.width < 600 ? 2 : 3
                anchors.fill: parent
                anchors.margins: 22
                ColumnLayout {
                    spacing: 3
                    Layout.columnSpan: root.width < 600 ? 2 : 1
                    Layout.fillWidth: true
                    Copy { text: root.info.client || qsTr("Meeting notes"); font.pixelSize: 23; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Copy { text: root.info.date || ""; color: root.muted; font.pixelSize: 14; Layout.fillWidth: true }
                }
                Action { text: qsTr("History"); visible: root.width < 1000; quiet: true; onClicked: root.historyOpen = !root.historyOpen }
                Action { text: qsTr("More") + " ⋯"; quiet: true; onClicked: actions.open()
                    Menu {
                        id: actions
                        background: Rectangle {
                            implicitWidth: 180
                            color: root.paper
                            border.color: root.line
                            radius: 8
                        }
                        MenuItem {
                            id: exportMenuItem
                            text: qsTr("Export")
                            contentItem: Copy { text: exportMenuItem.text; font.pixelSize: 14 }
                            background: Rectangle { color: exportMenuItem.highlighted ? root.soft : "transparent"; radius: 6 }
                            onTriggered: if (workspaceBridge) workspaceBridge.exportDocument()
                        }
                        MenuItem {
                            id: advancedMenuItem
                            text: qsTr("Advanced view")
                            contentItem: Copy { text: advancedMenuItem.text; font.pixelSize: 14 }
                            background: Rectangle { color: advancedMenuItem.highlighted ? root.soft : "transparent"; radius: 6 }
                            onTriggered: root.advancedRequested()
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                visible: root.width >= 1000 || root.historyOpen
                Layout.preferredWidth: root.width < 700 ? 200 : root.width > 1800 ? 320 : 260
                Layout.fillHeight: true
                color: root.color
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    Copy { text: qsTr("History"); font.weight: Font.DemiBold; font.pixelSize: 14 }
                    TextField {
                        id: historySearch
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search meetings…")
                        color: root.ink
                        placeholderTextColor: root.muted
                        font.pixelSize: 14
                        background: Rectangle { color: root.paper; radius: 6; border.color: root.line }
                    }
                    Action { text: qsTr("This meeting"); Layout.fillWidth: true; primary: root.info.current === true
                        onClicked: if (workspaceBridge) workspaceBridge.selectMaterial(workspaceBridge.currentMaterialId)
                    }
                    ListView {
                        id: historyList
                        objectName: "qmlMeetingHistory"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 7
                        model: root.historyModel.filter(e => String(e.preview + e.date).toLowerCase().includes(historySearch.text.toLowerCase()))
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: historyDelegate
                            required property var modelData
                            width: historyList.width
                            height: Math.max(88, contentItem.implicitHeight + 24)
                            padding: 12
                            background: Rectangle {
                                radius: 7
                                color: historyDelegate.modelData.id === root.info.materialId ? root.paper : historyDelegate.hovered ? root.soft : "transparent"
                                border.width: historyDelegate.modelData.id === root.info.materialId ? 1 : 0
                                border.color: root.line
                                Rectangle { width: 3; height: parent.height - 24; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; radius: 2; color: root.accent; visible: modelData.id === root.info.materialId }
                            }
                            contentItem: Column {
                                spacing: 9
                                Copy { text: modelData.date; font.pixelSize: 13; color: root.muted; width: parent.width }
                                Copy { text: modelData.preview || qsTr("Meeting notes"); font.pixelSize: 15; font.weight: Font.Medium; width: parent.width; maximumLineCount: 3; elide: Text.ElideRight }
                            }
                            onClicked: { if (workspaceBridge) workspaceBridge.selectMaterial(modelData.id); root.historyOpen = false }
                        }
                    }
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: root.line }
            }
            ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: scroll.availableWidth
                    height: Math.max(scroll.availableHeight, implicitHeight)
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.width < 700 ? 16 : 28
                        Layout.rightMargin: root.width < 700 ? 16 : 28
                        Layout.topMargin: 20
                        Copy { text: qsTr("Meeting materials"); font.pixelSize: 14; color: root.muted; Layout.fillWidth: true }
                        Action { text: root.sourceOpen ? qsTr("Close transcript") : qsTr("Open transcript"); quiet: true; onClicked: root.sourceOpen = !root.sourceOpen }
                    }
                    Rectangle {
                        objectName: "qmlNotesPaper"
                        visible: (root.info.materialId && root.info.materialId !== "0") || root.info.prompterVisible === true
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: documentColumn.implicitHeight + 68
                        Layout.leftMargin: root.width < 700 ? 12 : 28
                        Layout.rightMargin: root.width < 700 ? 12 : 28
                        implicitHeight: documentColumn.implicitHeight + 58
                        radius: 10
                        color: root.paper
                        border.color: root.line
                        ColumnLayout {
                            id: documentColumn
                            anchors.fill: parent
                            anchors.margins: root.width < 700 ? 20 : 34
                            spacing: 20
                            Copy { text: root.info.date || ""; font.pixelSize: 14; color: root.muted; Layout.fillWidth: true }
                            Copy { text: root.info.title || qsTr("Meeting notes"); font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            ColumnLayout {
                                visible: !root.liveCallPanel && root.info.canChangePayment === true
                                Layout.fillWidth: true
                                Copy { text: qsTr("Payment status"); font.pixelSize: 14; color: root.muted; Layout.fillWidth: true }
                                ComboBox {
                                    objectName: "qmlPaymentStatus"
                                    Layout.fillWidth: true
                                    textRole: "label"
                                    valueRole: "id"
                                    model: [
                                        { id: 1, label: qsTr("Pending") },
                                        { id: 2, label: qsTr("Paid") },
                                        { id: 3, label: qsTr("Canceled") },
                                        { id: 4, label: qsTr("Refunded") },
                                        { id: 5, label: qsTr("Skipped") }
                                    ]
                                    currentIndex: Math.max(0, Number(root.info.paymentStatus || 1) - 1)
                                    onActivated: {
                                        if (workspaceBridge) workspaceBridge.setPaymentStatus(currentValue)
                                        currentIndex = Qt.binding(function() { return Math.max(0, Number(root.info.paymentStatus || 1) - 1) })
                                    }
                                }
                                Copy { text: root.info.paymentError || ""; visible: text.length > 0; color: root.muted; Layout.fillWidth: true }
                            }
                            Rectangle { implicitHeight: 1; color: root.line; Layout.fillWidth: true; Layout.topMargin: 2; Layout.bottomMargin: 4 }
                            Repeater {
                                model: root.notesModel
                                ColumnLayout {
                                    id: savedEntry
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Copy { text: modelData.body_markdown || ""; textFormat: Text.MarkdownText; Layout.fillWidth: true; font.pixelSize: 17 }
                                    Repeater {
                                        model: modelData.attachments || []
                                        Action { required property var modelData; text: "↗ " + modelData.file_name
                                            property string entryId: savedEntry.modelData.id
                                            onClicked: if (workspaceBridge) workspaceBridge.openAttachment(entryId, modelData.id)
                                        }
                                    }
                                    Action { text: qsTr("Edit"); quiet: true; onClicked: if (workspaceBridge) workspaceBridge.editNote(modelData.id) }
                                    Rectangle { implicitHeight: 1; color: root.line; Layout.fillWidth: true; Layout.topMargin: 8 }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Copy { text: (workspaceBridge && workspaceBridge.editing) ? qsTr("Edit note") : qsTr("My notes"); font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                Action { text: "B"; quiet: true; font.bold: true; implicitWidth: 30; enabled: editor.selectionStart !== editor.selectionEnd; onClicked: if (workspaceBridge) workspaceBridge.format(editor.selectionStart, editor.selectionEnd, "bold") }
                                Action { text: "I"; quiet: true; font.italic: true; implicitWidth: 30; enabled: editor.selectionStart !== editor.selectionEnd; onClicked: if (workspaceBridge) workspaceBridge.format(editor.selectionStart, editor.selectionEnd, "italic") }
                                Action { text: "•"; quiet: true; implicitWidth: 30; onClicked: if (workspaceBridge) workspaceBridge.format(editor.selectionStart, editor.selectionEnd, "list") }
                            }
                            TextArea {
                                id: editor
                                objectName: "qmlNoteEditor"
                                enabled: root.info.materialId !== "0"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: 170
                                Layout.preferredHeight: Math.max(170, implicitHeight)
                                color: root.ink
                                selectionColor: root.accent
                                selectedTextColor: theme.highlightedText
                                placeholderText: qsTr("Write a note…")
                                placeholderTextColor: root.muted
                                font.pixelSize: 17
                                wrapMode: TextEdit.Wrap
                                textFormat: TextEdit.RichText
                                selectByMouse: true
                                persistentSelection: true
                                padding: 0
                                background: null
                                Component.onCompleted: if (workspaceBridge) workspaceBridge.attachDocument(textDocument)
                            }
                            Repeater {
                                model: root.info.attachments || []
                                RowLayout {
                                    required property var modelData
                                    required property int index
                                    Copy { text: "↗ " + modelData.name; Layout.fillWidth: true; font.pixelSize: 12 }
                                    Action { text: "×"; quiet: true; onClicked: if (workspaceBridge) workspaceBridge.removeAttachment(index) }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Action { text: qsTr("Attach files"); quiet: true; onClicked: if (workspaceBridge) workspaceBridge.attachFiles() }
                                Action { text: qsTr("Cancel edit"); visible: workspaceBridge ? workspaceBridge.editing : false; onClicked: if (workspaceBridge) workspaceBridge.cancelEdit() }
                                Action { objectName: "qmlSaveNote"; text: qsTr("Save note"); primary: true; enabled: editor.length > 0 || (workspaceBridge && workspaceBridge.editing) || (root.info.attachments || []).length > 0; onClicked: if (workspaceBridge) workspaceBridge.saveNote() }
                            }
                            Copy { text: workspaceBridge ? workspaceBridge.saveStatus : ""; visible: text.length > 0; color: root.muted; font.pixelSize: 12; Layout.fillWidth: true }
                            Rectangle {
                                objectName: "qmlPrompterSection"
                                Layout.fillWidth: true
                                visible: root.info.prompterVisible === true
                                color: root.soft
                                radius: 7
                                implicitHeight: prompterColumn.implicitHeight + 36
                                ColumnLayout {
                                    id: prompterColumn
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    anchors.margins: 18
                                    spacing: 12
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        Copy { text: "✧ " + qsTr("Prompter"); color: root.accent; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        Rectangle {
                                            visible: (root.info.prompterBadge || "").length > 0
                                            radius: 10
                                            color: root.paper
                                            border.color: root.line
                                            implicitHeight: 24
                                            implicitWidth: prompterBadgeLabel.implicitWidth + 16
                                            Copy {
                                                id: prompterBadgeLabel
                                                anchors.centerIn: parent
                                                text: root.info.prompterBadge || ""
                                                font.pixelSize: 12
                                                color: root.muted
                                            }
                                        }
                                    }
                                    Copy { text: qsTr("Private suggestions. Check the source before saving."); font.pixelSize: 12; color: root.muted; Layout.fillWidth: true }
                                    Copy { text: root.info.prompterStatus || ""; visible: text.length > 0; font.pixelSize: 13; color: root.muted; Layout.fillWidth: true }
                                    Flow {
                                        Layout.fillWidth: true
                                        visible: root.info.prompterControls === true
                                        spacing: 8
                                        Action {
                                            objectName: "qmlPrompterStart"
                                            text: qsTr("Start")
                                            primary: !root.info.prompterRunning
                                            enabled: !root.info.prompterRunning
                                            onClicked: if (workspaceBridge) workspaceBridge.startPrompter()
                                        }
                                        Action {
                                            objectName: "qmlPrompterPause"
                                            text: qsTr("Pause")
                                            enabled: root.info.prompterRunning === true
                                            onClicked: if (workspaceBridge) workspaceBridge.pausePrompter()
                                        }
                                    }
                                    Rectangle {
                                        visible: (root.info.prompterHint || "").length > 0
                                        Layout.fillWidth: true
                                        radius: 6
                                        color: root.paper
                                        border.color: root.line
                                        implicitHeight: hintColumn.implicitHeight + 24
                                        ColumnLayout {
                                            id: hintColumn
                                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                            anchors.margins: 12
                                            spacing: 8
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Copy { text: qsTr("You could clarify"); font.pixelSize: 13; font.weight: Font.DemiBold; color: root.accent; Layout.fillWidth: true }
                                                Action { text: qsTr("Hide"); quiet: true; onClicked: if (workspaceBridge) workspaceBridge.dismissPrompterHint() }
                                            }
                                            Copy { text: root.info.prompterHint || ""; font.pixelSize: 14; Layout.fillWidth: true }
                                        }
                                    }
                                    Repeater {
                                        model: root.info.prompterCards || []
                                        Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            radius: 6
                                            color: root.paper
                                            border.color: root.line
                                            implicitHeight: cardColumn.implicitHeight + 24
                                            ColumnLayout {
                                                id: cardColumn
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                                anchors.margins: 12
                                                spacing: 8
                                                Copy { text: modelData.title || ""; font.pixelSize: 13; font.weight: Font.DemiBold; color: root.accent; Layout.fillWidth: true }
                                                Copy { text: modelData.action || ""; font.pixelSize: 15; font.weight: Font.Medium; Layout.fillWidth: true }
                                                Copy {
                                                    visible: !modelData.sourceValid
                                                    text: qsTr("Needs source check")
                                                    font.pixelSize: 12
                                                    color: root.muted
                                                    Layout.fillWidth: true
                                                }
                                                Copy {
                                                    visible: (modelData.previous || "").length > 0
                                                    text: qsTr("Earlier: %1").arg(modelData.previous || "")
                                                    font.pixelSize: 12
                                                    color: root.muted
                                                    Layout.fillWidth: true
                                                }
                                                Repeater {
                                                    model: modelData.sources || []
                                                    ColumnLayout {
                                                        required property var modelData
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        Copy {
                                                            visible: (modelData.quote || "").length > 0
                                                            text: "“" + (modelData.quote || "") + "”"
                                                            font.pixelSize: 13
                                                            color: root.muted
                                                            Layout.fillWidth: true
                                                        }
                                                        Action {
                                                            text: modelData.label || qsTr("Source")
                                                            quiet: true
                                                            onClicked: {
                                                                if (workspaceBridge)
                                                                    workspaceBridge.showPrompterSource(modelData.id)
                                                                root.revealSource(modelData.id)
                                                            }
                                                        }
                                                    }
                                                }
                                                Flow {
                                                    Layout.fillWidth: true
                                                    spacing: 8
                                                    Action {
                                                        visible: modelData.canAccept === true
                                                        text: qsTr("Accept")
                                                        primary: true
                                                        onClicked: if (workspaceBridge) workspaceBridge.prompterCardAction(modelData.id, "accept")
                                                    }
                                                    Action {
                                                        visible: modelData.canResolve === true
                                                        text: qsTr("Choose the agreement")
                                                        primary: true
                                                        onClicked: if (workspaceBridge) workspaceBridge.prompterCardAction(modelData.id, "resolve")
                                                    }
                                                    Action {
                                                        text: qsTr("Edit")
                                                        onClicked: if (workspaceBridge) workspaceBridge.prompterCardAction(modelData.id, "edit")
                                                    }
                                                    Action {
                                                        visible: modelData.canSaveNote === true
                                                        enabled: !modelData.noteSaved
                                                        text: modelData.noteSaved ? qsTr("Saved to notes") : qsTr("To notes")
                                                        quiet: true
                                                        onClicked: if (workspaceBridge) workspaceBridge.prompterCardAction(modelData.id, "save_note")
                                                    }
                                                    Action {
                                                        text: qsTr("Dismiss")
                                                        quiet: true
                                                        onClicked: if (workspaceBridge) workspaceBridge.prompterCardAction(modelData.id, "dismiss")
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                objectName: "qmlSummaryOffer"
                                Layout.fillWidth: true
                                visible: !root.liveCallPanel && root.info.summaryState === "offered"
                                color: root.soft
                                radius: 7
                                implicitHeight: offerColumn.implicitHeight + 38
                                ColumnLayout {
                                    id: offerColumn
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    anchors.margins: 19
                                    spacing: 12
                                    Copy { text: "✧ " + qsTr("Meeting summary"); color: root.accent; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                    Copy { text: qsTr("Summarize the whole meeting: overview, key points, agreements and tasks, open questions."); color: root.muted; font.pixelSize: 14; Layout.fillWidth: true }
                                    Action {
                                        objectName: "qmlSummarizeMeeting"
                                        text: qsTr("Summarize meeting")
                                        primary: true
                                        enabled: !root.info.busy
                                        onClicked: if (workspaceBridge) workspaceBridge.summarizeMeeting()
                                    }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                visible: root.summaryShown
                                color: root.soft
                                radius: 7
                                implicitHeight: summaryColumn.implicitHeight + 38
                                ColumnLayout {
                                    id: summaryColumn
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    anchors.margins: 19
                                    spacing: 12
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Copy { text: "✧ " + qsTr("Meeting summary"); color: root.accent; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        Copy { objectName: "qmlSummaryEngine"; text: root.info.summaryEngine ? qsTr("Prepared by AI · %1").arg(root.info.summaryEngine) : qsTr("Prepared by AI"); font.pixelSize: 11; color: root.muted }
                                    }
                                    Copy { objectName: "qmlSummaryOverview"; text: root.info.summary || ""; visible: text.length > 0; Layout.fillWidth: true }
                                    Copy { text: root.info.summaryProblem || ""; visible: ["error", "stale", "running", "queued", "cancelled", "interrupted", "unavailable"].includes(root.info.summaryState); color: root.muted; font.pixelSize: 14; Layout.fillWidth: true }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        Action { text: "+ " + qsTr("Add to my notes"); quiet: true; enabled: !(workspaceBridge && workspaceBridge.editing) && (root.info.summary || "").length > 0; onClicked: if (workspaceBridge) workspaceBridge.appendSummary() }
                                        SourceButton { sources: root.info.summarySources || [] }
                                        Action { objectName: "qmlSummaryRetry"; text: qsTr("Retry"); quiet: true; visible: ["error", "stale", "cancelled", "interrupted", "unavailable"].includes(root.info.summaryState); enabled: !root.info.busy; onClicked: if (workspaceBridge) workspaceBridge.retrySummary() }
                                        Action { text: qsTr("Create again"); quiet: true; visible: root.info.summaryState === "ready"; enabled: !root.info.busy; onClicked: if (workspaceBridge) workspaceBridge.summarizeMeeting() }
                                    }
                                    ColumnLayout {
                                        objectName: "qmlSummaryPoints"
                                        Layout.fillWidth: true
                                        spacing: 6
                                        visible: (root.info.points || []).length > 0
                                        Copy { text: qsTr("Key points"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        Repeater {
                                            model: root.info.points || []
                                            RowLayout {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                Copy { text: "•  " + modelData.text; Layout.fillWidth: true }
                                                SourceButton { sources: modelData.evidence_ids || [] }
                                            }
                                        }
                                    }
                                    ColumnLayout {
                                        objectName: "qmlSummaryTasks"
                                        Layout.fillWidth: true
                                        spacing: 6
                                        visible: root.activeAgreements.length > 0 || (root.info.tasks || []).length > 0
                                        Copy { text: qsTr("Agreements and tasks"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        Repeater {
                                            model: root.activeAgreements
                                            AgreementRow {}
                                        }
                                        Repeater {
                                            model: root.info.tasks || []
                                            RowLayout {
                                                required property var modelData
                                                required property int index
                                                Layout.fillWidth: true
                                                Copy { text: "•  " + modelData.text; color: modelData.accepted ? root.muted : root.ink; Layout.fillWidth: true }
                                                SourceButton { sources: modelData.evidence_ids || [] }
                                                Action {
                                                    visible: !modelData.accepted
                                                    text: qsTr("Accept")
                                                    quiet: true
                                                    onClicked: if (workspaceBridge) workspaceBridge.acceptSummaryTask(index)
                                                }
                                            }
                                        }
                                    }
                                    ColumnLayout {
                                        objectName: "qmlSummaryQuestions"
                                        Layout.fillWidth: true
                                        spacing: 6
                                        visible: (root.info.openQuestions || []).length > 0
                                        Copy { text: qsTr("Open questions"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        Repeater {
                                            model: root.info.openQuestions || []
                                            RowLayout {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                Copy { text: "•  " + modelData.text; Layout.fillWidth: true }
                                                SourceButton { sources: modelData.evidence_ids || [] }
                                            }
                                        }
                                    }
                                }
                            }
                            Copy { text: qsTr("For the next meeting"); visible: !root.summaryShown && root.activeAgreements.length > 0; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Repeater {
                                model: root.summaryShown ? [] : root.activeAgreements
                                AgreementRow {}
                            }
                            ColumnLayout {
                                objectName: "qmlLivePoints"
                                Layout.fillWidth: true
                                spacing: 6
                                visible: (root.info.livePoints || []).length > 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Copy { text: qsTr("From the call"); font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                    Copy { objectName: "qmlLiveEngine"; text: root.info.liveEngine || ""; visible: text.length > 0; font.pixelSize: 11; color: root.muted }
                                }
                                Repeater {
                                    model: root.info.livePoints || []
                                    GridLayout {
                                        required property var modelData
                                        columns: root.width < 600 ? 1 : 2
                                        Layout.fillWidth: true
                                        Copy { text: "•  " + modelData.text; Layout.fillWidth: true }
                                        SourceButton { sources: modelData.evidence_ids || [] }
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true; implicitHeight: 35 }
                }
            }
            Rectangle {
                visible: root.sourceOpen
                Layout.preferredWidth: root.width < 1000 ? Math.min(320, root.width * 0.45) : 370
                Layout.fillHeight: true
                color: root.paper
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        Copy { text: qsTr("Transcript"); font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Action { text: "×"; quiet: true; onClicked: root.sourceOpen = false }
                    }
                    TextField {
                        id: phraseSearch
                        objectName: "qmlPhraseSearch"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search transcript…")
                        color: root.ink
                        placeholderTextColor: root.muted
                        background: Rectangle { radius: 6; border.color: root.line; color: root.paper }
                    }
                    ListView {
                        id: phrases
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 12
                        clip: true
                        model: root.phrasesModel.filter(e => String(e.speaker + e.text).toLowerCase().includes(phraseSearch.text.toLowerCase()))
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: phrases.width
                            height: phraseColumn.implicitHeight + 24
                            radius: 6
                            color: modelData.id === root.highlightedSource ? root.soft : "transparent"
                            Column {
                                id: phraseColumn
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                anchors.margins: 12
                                spacing: 10
                                Copy { text: modelData.speaker + " · " + modelData.time; font.pixelSize: 11; color: root.muted; width: parent.width }
                                Copy { text: modelData.text; font.pixelSize: 15; width: parent.width }
                            }
                        }
                    }
                    Action {
                        text: qsTr("Edit transcript")
                        quiet: true
                        visible: root.info.current === true && root.phrasesModel.length > 0
                        onClicked: { if (workspaceBridge) workspaceBridge.showSource(root.phrasesModel[0].id); root.sourceOpen = false }
                    }
                }
                Rectangle { width: 1; height: parent.height; color: root.line }
            }
        }
    }
}
