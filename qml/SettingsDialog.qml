import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    width: Math.min(540, Overlay.overlay ? Overlay.overlay.width - 36 : 540)
    height: Math.min(436, Overlay.overlay ? Overlay.overlay.height - 24 : 436)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property int activeTab: 0
    property string draftHotkey: "Alt+1"
    property bool recordingHotkey: false
    property string recordHint: ""
    property string draftViewMode: "grid"
    property string draftTileSize: "medium"

    readonly property bool hotkeyEmpty: draftHotkey.trim().length === 0
    readonly property bool hotkeyMatchesSaved: draftHotkey.trim() === controller.hotkeyKey.trim()
    readonly property bool hotkeyConflict: !hotkeyEmpty && hotkeyMatchesSaved && !controller.hotkeyRegistered
    readonly property bool hotkeyActive: !hotkeyEmpty && hotkeyMatchesSaved && controller.hotkeyRegistered

    function openSettings() {
        recordingHotkey = false
        recordHint = ""
        activeTab = 0
        draftHotkey = controller.hotkeyKey
        hideOnBlurCheck.checked = controller.hideOnBlur
        hideAfterCheck.checked = controller.hideAfterLaunch
        singleClickCheck.checked = controller.singleClickLaunch
        showAtCursorCheck.checked = controller.showAtCursor
        topMostCheck.checked = controller.topMost
        draftViewMode = (controller.viewMode === "list") ? "list" : "grid"
        draftTileSize = (controller.tileSize === "small" || controller.tileSize === "large")
            ? controller.tileSize : "medium"
        open()
    }

    function stopRecording() {
        if (recordingHotkey) {
            recordingHotkey = false
            controller.pauseHotkeysForRecording(false)
        }
    }

    function startRecording() {
        recordHint = ""
        recordingHotkey = true
        controller.pauseHotkeysForRecording(true)
        hotkeyCatcher.forceActiveFocus()
    }

    onActiveTabChanged: {
        if (activeTab !== 0) {
            stopRecording()
        }
        if (bodyScroll.contentItem) {
            bodyScroll.contentItem.contentY = 0
        }
    }
    function formatCapturedKey(event) {
        const k = event.key
        if (k === Qt.Key_Control || k === Qt.Key_Shift || k === Qt.Key_Alt || k === Qt.Key_Meta || k === Qt.Key_AltGr) {
            return ""
        }
        const mods = event.modifiers
        if (k === Qt.Key_Escape && mods === Qt.NoModifier) {
            return "__CANCEL__"
        }
        let mainName = ""
        let allowBare = false
        if (k >= Qt.Key_A && k <= Qt.Key_Z) {
            mainName = String.fromCharCode(65 + (k - Qt.Key_A))
        } else if (k >= Qt.Key_0 && k <= Qt.Key_9) {
            mainName = String.fromCharCode(48 + (k - Qt.Key_0))
        } else if (k >= Qt.Key_F1 && k <= Qt.Key_F24) {
            mainName = "F" + (k - Qt.Key_F1 + 1)
            allowBare = true
        } else if (k === Qt.Key_Space) {
            mainName = "Space"
        } else if (k === Qt.Key_Tab || k === Qt.Key_Backtab) {
            mainName = "Tab"
        } else if (k === Qt.Key_Return || k === Qt.Key_Enter) {
            mainName = "Enter"
        } else if (k === Qt.Key_Backspace) {
            mainName = "Back"
        } else if (k === Qt.Key_Insert) {
            mainName = "Insert"
        } else if (k === Qt.Key_Delete) {
            mainName = "Delete"
        } else if (k === Qt.Key_Home) {
            mainName = "Home"
        } else if (k === Qt.Key_End) {
            mainName = "End"
        } else if (k === Qt.Key_PageUp) {
            mainName = "PageUp"
        } else if (k === Qt.Key_PageDown) {
            mainName = "PageDown"
        } else if (k === Qt.Key_Up) {
            mainName = "Up"
        } else if (k === Qt.Key_Down) {
            mainName = "Down"
        } else if (k === Qt.Key_Left) {
            mainName = "Left"
        } else if (k === Qt.Key_Right) {
            mainName = "Right"
        } else if (k === Qt.Key_QuoteLeft || k === Qt.Key_AsciiTilde) {
            mainName = "`"
        } else if (k === Qt.Key_Print) {
            mainName = "PrintScreen"
            allowBare = true
        }
        if (mainName.length === 0) {
            return "__UNSUPPORTED__"
        }
        const hasMod = (mods & (Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier | Qt.MetaModifier)) !== 0
        if (!hasMod && !allowBare) {
            return "__NEED_MOD__"
        }
        const parts = []
        if (mods & Qt.MetaModifier) parts.push("Win")
        if (mods & Qt.ControlModifier) parts.push("Ctrl")
        if (mods & Qt.AltModifier) parts.push("Alt")
        if (mods & Qt.ShiftModifier) parts.push("Shift")
        parts.push(mainName)
        return parts.join("+")
    }

    function saveAndClose() {
        stopRecording()
        controller.applySettings({
            popupHotkey: draftHotkey.trim(),
            hideOnBlur: hideOnBlurCheck.checked,
            hideAfterLaunch: hideAfterCheck.checked,
            singleClickLaunch: singleClickCheck.checked,
            showAtCursor: showAtCursorCheck.checked,
            topMost: topMostCheck.checked,
            viewMode: (draftViewMode === "list") ? "list" : "grid",
            tileSize: (draftTileSize === "small" || draftTileSize === "large") ? draftTileSize : "medium"
        })
        close()
    }
    onOpened: controller.setDialogOpen(true)
    onClosed: {
        stopRecording()
        controller.setDialogOpen(false)
    }

    Overlay.modal: Rectangle {
        color: theme.withAlpha("#000000", theme.dark ? 0.50 : 0.26)
    }

    background: Rectangle {
        color: theme.surface
        radius: Math.max(10, theme.cardRadius)
        border.width: 1
        border.color: theme.windowEdge
    }

    component SettingBtn: Button {
        id: sbtn
        property bool accentStyle: false
        implicitHeight: 30
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        contentItem: Text {
            text: sbtn.text
            color: sbtn.accentStyle ? theme.accent : theme.text
            font.pixelSize: 12
            font.weight: sbtn.accentStyle ? Font.Medium : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: sbtn.accentStyle
                ? (sbtn.down ? theme.withAlpha(theme.accent, 0.24)
                    : (sbtn.hovered ? theme.withAlpha(theme.accent, 0.18) : theme.withAlpha(theme.accent, 0.11)))
                : (sbtn.down ? theme.pressed : (sbtn.hovered ? theme.controlHover : theme.control))
            radius: Math.min(theme.controlRadius, 6)
            border.width: 1
            border.color: sbtn.accentStyle ? theme.withAlpha(theme.accent, 0.45) : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }

    component DialogCloseBtn: Button {
        id: scbtn
        implicitWidth: 30
        implicitHeight: 30
        padding: 0
        hoverEnabled: true
        contentItem: Canvas {
            property color strokeColor: scbtn.hovered ? "#FFFFFF" : theme.subtext
            onStrokeColorChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const s = Math.min(width, height)
                if (s <= 0) return
                const c = strokeColor
                ctx.strokeStyle = "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
                ctx.lineWidth = 1.35
                ctx.lineCap = "round"
                const cx = width / 2
                const cy = height / 2
                const r = 4.5
                ctx.beginPath()
                ctx.moveTo(cx - r, cy - r)
                ctx.lineTo(cx + r, cy + r)
                ctx.moveTo(cx + r, cy - r)
                ctx.lineTo(cx - r, cy + r)
                ctx.stroke()
            }
        }
        background: Rectangle {
            color: scbtn.hovered ? theme.danger : "transparent"
            radius: Math.min(theme.controlRadius, 6)
        }
    }
    component SettingSwitch: CheckBox {
        id: sw
        property bool rightAligned: false
        implicitHeight: rightAligned ? 36 : Math.max(24, contentItem.implicitHeight)
        leftPadding: rightAligned ? 14 : 0
        rightPadding: rightAligned ? 14 : 0
        spacing: 10
        hoverEnabled: true

        indicator: Rectangle {
            id: swTrack
            implicitWidth: 36
            implicitHeight: 20
            width: 36
            height: 20
            x: sw.rightAligned ? (sw.width - sw.rightPadding - width) : sw.leftPadding
            y: (sw.height - height) / 2
            radius: 10
            color: sw.checked
                ? (sw.down ? theme.pressed : (sw.hovered ? theme.accent : theme.accentFill))
                : (theme.dark
                    ? (sw.down ? theme.pressed : (sw.hovered ? theme.controlHover : theme.control))
                    : (sw.down ? theme.pressed : (sw.hovered ? theme.hover : theme.withAlpha(theme.text, 0.12))))
            border.width: sw.checked ? 0 : 1
            border.color: sw.checked ? "transparent" : theme.controlBorder
            Behavior on color { ColorAnimation { duration: 140 } }

            Rectangle {
                width: 14
                height: 14
                radius: 7
                y: 3
                x: sw.checked ? 19 : 3
                color: "#FFFFFF"
                border.width: (!sw.checked && !theme.dark) ? 1 : 0
                border.color: theme.withAlpha("#000000", 0.14)
                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
        }

        contentItem: Text {
            text: sw.text
            color: theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
            leftPadding: sw.rightAligned ? 0 : (sw.indicator.width + sw.spacing)
            rightPadding: sw.rightAligned ? (sw.indicator.width + 12) : 0
            verticalAlignment: Text.AlignVCenter
        }

        background: Item {
            Rectangle {
                visible: sw.rightAligned
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 2
                anchors.bottomMargin: 2
                radius: 6
                color: sw.rightAligned && sw.hovered ? theme.hover : "transparent"
                Behavior on color { ColorAnimation { duration: 90 } }
            }
        }
    }

    component CardBox: Rectangle {
        color: theme.cardSurface
        radius: 8
        border.width: 1
        border.color: theme.softBorder
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 50

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: "设置"
                color: theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Rectangle {
                anchors.centerIn: parent
                height: 32
                width: navRow.implicitWidth + 6
                radius: 16
                color: theme.sidebarBackground
                border.width: 1
                border.color: theme.softBorder

                Row {
                    id: navRow
                    anchors.centerIn: parent
                    spacing: 2

                    Repeater {
                        model: ["常规", "外观", "数据"]
                        delegate: Rectangle {
                            readonly property bool selected: root.activeTab === index
                            width: 62
                            height: 26
                            radius: 13
                            color: selected
                                ? theme.withAlpha(theme.accent, theme.dark ? 0.22 : 0.14)
                                : (tabMouse.containsMouse ? theme.hover : "transparent")
                            border.width: selected ? 1 : 0
                            border.color: selected ? theme.withAlpha(theme.accent, 0.45) : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 12
                                font.weight: selected ? Font.DemiBold : Font.Normal
                                color: selected ? theme.accent : (tabMouse.containsMouse ? theme.text : theme.subtext)
                            }

                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.activeTab = index
                            }
                        }
                    }
                }
            }

            DialogCloseBtn {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.close()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.softBorder
            }
        }

        ScrollView {
            id: bodyScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }

            Item {
                width: bodyScroll.availableWidth
                implicitHeight: (root.activeTab === 0
                    ? tabGeneral.implicitHeight
                    : (root.activeTab === 1 ? tabAppearance.implicitHeight : tabData.implicitHeight)) + 28
                ColumnLayout {
                    id: tabGeneral
                    visible: root.activeTab === 0
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 14
                    spacing: 12

                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: hotkeyCol.implicitHeight + 24

                        ColumnLayout {
                            id: hotkeyCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: "全局快捷键"
                                    color: theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    height: 20
                                    width: hotkeyBadgeText.implicitWidth + 14
                                    radius: 10
                                    color: root.hotkeyEmpty
                                        ? theme.withAlpha(theme.faint, 0.14)
                                        : (root.hotkeyConflict
                                            ? theme.withAlpha(theme.danger, 0.15)
                                            : theme.withAlpha(theme.accent, 0.15))

                                    Text {
                                        id: hotkeyBadgeText
                                        anchors.centerIn: parent
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                        text: root.hotkeyEmpty
                                            ? "未启用"
                                            : (root.hotkeyConflict ? "冲突" : (root.hotkeyActive ? "已生效" : "待保存"))
                                        color: root.hotkeyEmpty
                                            ? theme.faint
                                            : (root.hotkeyConflict ? theme.danger : theme.accent)
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Rectangle {
                                    id: hotkeyCatcher
                                    Layout.fillWidth: true
                                    height: 30
                                    radius: Math.min(theme.controlRadius, 6)
                                    color: root.recordingHotkey
                                        ? theme.withAlpha(theme.accent, 0.14)
                                        : theme.sidebarBackground
                                    border.width: 1
                                    border.color: root.recordingHotkey ? theme.inputFocused : theme.controlBorder
                                    focus: root.recordingHotkey

                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        color: root.recordingHotkey
                                            ? theme.accent
                                            : (root.draftHotkey.length > 0 ? theme.text : theme.faint)
                                        text: root.recordingHotkey
                                            ? "请按下快捷键组合 (Esc 取消)..."
                                            : (root.draftHotkey.length > 0 ? root.draftHotkey : "未设置（已禁用全局呼出）")
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.recordingHotkey ? root.stopRecording() : root.startRecording()
                                    }

                                    Keys.onPressed: (event) => {
                                        if (!root.recordingHotkey) {
                                            return
                                        }
                                        event.accepted = true
                                        const formatted = root.formatCapturedKey(event)
                                        if (formatted === "") {
                                            return
                                        }
                                        if (formatted === "__CANCEL__") {
                                            root.stopRecording()
                                            return
                                        }
                                        if (formatted === "__UNSUPPORTED__") {
                                            root.recordHint = "暂不支持该按键，请改用字母、数字或 F1~F24"
                                            return
                                        }
                                        if (formatted === "__NEED_MOD__") {
                                            root.recordHint = "普通按键请搭配 Ctrl / Alt / Shift / Win 组合"
                                            return
                                        }
                                        root.draftHotkey = formatted
                                        root.recordHint = ""
                                        root.stopRecording()
                                    }
                                }

                                SettingBtn {
                                    text: root.recordingHotkey ? "停止" : "录制"
                                    onClicked: root.recordingHotkey ? root.stopRecording() : root.startRecording()
                                }
                                SettingBtn {
                                    text: "清空"
                                    onClicked: {
                                        root.stopRecording()
                                        root.draftHotkey = ""
                                        root.recordHint = ""
                                    }
                                }
                                SettingBtn {
                                    text: "恢复默认"
                                    onClicked: {
                                        root.stopRecording()
                                        root.draftHotkey = "Alt+1"
                                        root.recordHint = ""
                                    }
                                }
                            }

                            Text {
                                visible: root.recordHint.length > 0 || (root.hotkeyConflict && controller.hotkeyErrorText.length > 0)
                                text: root.recordHint.length > 0 ? root.recordHint : controller.hotkeyErrorText
                                color: theme.danger
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            Text {
                                text: "提示：在 Win顺「设置 → 插件 → 快速启动面板」也可直接修改此快捷键"
                                color: theme.faint
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: behaviorCol.implicitHeight

                        ColumnLayout {
                            id: behaviorCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 0

                            SettingSwitch {
                                id: hideOnBlurCheck
                                rightAligned: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: "失去焦点时自动隐藏"
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 14
                                Layout.rightMargin: 14
                                Layout.preferredHeight: 1
                                color: theme.softBorder
                            }
                            SettingSwitch {
                                id: hideAfterCheck
                                rightAligned: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: "启动程序后自动隐藏"
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 14
                                Layout.rightMargin: 14
                                Layout.preferredHeight: 1
                                color: theme.softBorder
                            }
                            SettingSwitch {
                                id: singleClickCheck
                                rightAligned: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: "单击直接启动（关闭则双击）"
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 14
                                Layout.rightMargin: 14
                                Layout.preferredHeight: 1
                                color: theme.softBorder
                            }
                            SettingSwitch {
                                id: showAtCursorCheck
                                rightAligned: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: "呼出时跟随鼠标指针"
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 14
                                Layout.rightMargin: 14
                                Layout.preferredHeight: 1
                                color: theme.softBorder
                            }
                            SettingSwitch {
                                id: topMostCheck
                                rightAligned: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                text: "窗口始终置顶显示"
                            }
                        }
                    }
                }
                ColumnLayout {
                    id: tabAppearance
                    visible: root.activeTab === 1
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 14
                    spacing: 12

                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: viewModeCol.implicitHeight + 28

                        ColumnLayout {
                            id: viewModeCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 10

                            Text {
                                text: "默认视图模式"
                                color: theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Repeater {
                                    model: [
                                        { mode: "grid", title: "图标网格", sub: "多列平铺图标，适合快速扫视" },
                                        { mode: "list", title: "紧凑列表", sub: "单行展示详情，路径更清晰" }
                                    ]
                                    delegate: Rectangle {
                                        id: viewCard
                                        readonly property bool selected: root.draftViewMode === modelData.mode
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 74
                                        radius: 8
                                        color: viewCard.selected
                                            ? theme.withAlpha(theme.accent, 0.12)
                                            : (vmMouse.containsMouse ? theme.hover : theme.sidebarBackground)
                                        border.width: 1
                                        border.color: viewCard.selected ? theme.accent : theme.softBorder
                                        Behavior on color { ColorAnimation { duration: 110 } }

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            spacing: 6

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8

                                                Grid {
                                                    visible: modelData.mode === "grid"
                                                    columns: 2
                                                    spacing: 2
                                                    Repeater {
                                                        model: 4
                                                        Rectangle {
                                                            width: 6
                                                            height: 6
                                                            radius: 2
                                                            color: viewCard.selected ? theme.accent : theme.subtext
                                                        }
                                                    }
                                                }
                                                Column {
                                                    visible: modelData.mode === "list"
                                                    spacing: 3
                                                    Repeater {
                                                        model: 3
                                                        Rectangle {
                                                            width: 14
                                                            height: 3
                                                            radius: 1
                                                            color: viewCard.selected ? theme.accent : theme.subtext
                                                        }
                                                    }
                                                }
                                                Text {
                                                    text: modelData.title
                                                    color: viewCard.selected ? theme.accent : theme.text
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    Layout.fillWidth: true
                                                }
                                                Rectangle {
                                                    width: 14
                                                    height: 14
                                                    radius: 7
                                                    color: viewCard.selected ? theme.accent : "transparent"
                                                    border.width: 1
                                                    border.color: viewCard.selected ? theme.accent : theme.controlBorder
                                                    Rectangle {
                                                        visible: viewCard.selected
                                                        anchors.centerIn: parent
                                                        width: 6
                                                        height: 6
                                                        radius: 3
                                                        color: theme.accentText
                                                    }
                                                }
                                            }
                                            Text {
                                                text: modelData.sub
                                                color: theme.subtext
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }
                                        MouseArea {
                                            id: vmMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.draftViewMode = modelData.mode
                                        }
                                    }
                                }
                            }
                        }
                    }
                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: tileSizeCol.implicitHeight + 28

                        ColumnLayout {
                            id: tileSizeCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "图标尺寸与间距"
                                    color: theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: root.draftTileSize === "small"
                                        ? "紧凑布局"
                                        : (root.draftTileSize === "large" ? "大图标视图" : "推荐默认")
                                    color: theme.faint
                                    font.pixelSize: 11
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: [
                                        { size: "small", label: "紧凑 (32px)" },
                                        { size: "medium", label: "标准 (40px)" },
                                        { size: "large", label: "宽松 (48px)" }
                                    ]
                                    delegate: Rectangle {
                                        readonly property bool selected: root.draftTileSize === modelData.size
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 38
                                        radius: 6
                                        color: selected
                                            ? theme.withAlpha(theme.accent, 0.12)
                                            : (tsMouse.containsMouse ? theme.hover : theme.sidebarBackground)
                                        border.width: 1
                                        border.color: selected ? theme.accent : theme.softBorder
                                        Behavior on color { ColorAnimation { duration: 110 } }

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            font.pixelSize: 12
                                            font.weight: selected ? Font.DemiBold : Font.Normal
                                            color: selected ? theme.accent : theme.text
                                        }

                                        MouseArea {
                                            id: tsMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.draftTileSize = modelData.size
                                        }
                                    }
                                }
                            }

                            Text {
                                text: "调节主面板网格单元格密度与图标大小，保存后立即应用"
                                color: theme.faint
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                ColumnLayout {
                    id: tabData
                    visible: root.activeTab === 2
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 14
                    spacing: 12

                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: mayeCol.implicitHeight + 26

                        ColumnLayout {
                            id: mayeCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 13
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 8

                            Text {
                                text: "MayeNano 导入"
                                color: theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: "从 MayeNano 的 MDB.json 迁移分组与启动项（自动跳过重复项）"
                                color: theme.subtext
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                spacing: 8

                                SettingBtn {
                                    visible: controller.hasDefaultMayeFile
                                    accentStyle: true
                                    text: "一键导入默认 MDB.json"
                                    onClicked: controller.importMayeDefault()
                                }
                                SettingBtn {
                                    text: "导入 MDB.json..."
                                    onClicked: controller.importMayeFromDialog()
                                }
                            }
                        }
                    }

                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: backupCol.implicitHeight + 26

                        ColumnLayout {
                            id: backupCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 13
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 8

                            Text {
                                text: "备份与恢复"
                                color: theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: "导出全部分组与启动项配置为 JSON 文件，或从本地备份恢复"
                                color: theme.subtext
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                spacing: 8

                                SettingBtn {
                                    text: "导出备份 JSON..."
                                    onClicked: controller.exportBackupDialog()
                                }
                                SettingBtn {
                                    text: "恢复备份 JSON..."
                                    onClicked: controller.importBackupDialog()
                                }
                            }
                        }
                    }

                    CardBox {
                        Layout.fillWidth: true
                        implicitHeight: dirRow.implicitHeight + 24

                        RowLayout {
                            id: dirRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Text {
                                    text: "数据目录"
                                    color: theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: "打开配置、数据库及快捷方式缓存所在文件夹"
                                    color: theme.subtext
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            SettingBtn {
                                text: "打开数据目录"
                                onClicked: controller.openDataDir()
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: theme.softBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 8

                Text {
                    text: "v0.3.1"
                    color: theme.faint
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }

                SettingBtn {
                    text: "取消"
                    implicitWidth: 76
                    implicitHeight: 32
                    onClicked: root.close()
                }

                Button {
                    id: saveSettingsBtn
                    text: "保存设置"
                    implicitWidth: 92
                    implicitHeight: 32
                    hoverEnabled: true
                    onClicked: root.saveAndClose()
                    contentItem: Text {
                        text: saveSettingsBtn.text
                        color: theme.accentText
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: saveSettingsBtn.down
                            ? theme.pressed
                            : (saveSettingsBtn.hovered ? theme.accent : theme.accentFill)
                        radius: Math.min(theme.controlRadius, 6)
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                }
            }
        }
    }
}
