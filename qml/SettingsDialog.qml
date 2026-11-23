import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    width: Math.min(490, Overlay.overlay ? Overlay.overlay.width - 32 : 490)
    height: Math.min(540, Overlay.overlay ? Overlay.overlay.height - 24 : 540)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string draftHotkey: "Alt+1"
    property bool recordingHotkey: false
    property string recordHint: ""

    function openSettings() {
        recordingHotkey = false
        recordHint = ""
        draftHotkey = controller.hotkeyKey
        hideOnBlurCheck.checked = controller.hideOnBlur
        hideAfterCheck.checked = controller.hideAfterLaunch
        singleClickCheck.checked = controller.singleClickLaunch
        showAtCursorCheck.checked = controller.showAtCursor
        topMostCheck.checked = controller.topMost
        viewModeCombo.currentIndex = (controller.viewMode === "list") ? 1 : 0
        tileSizeCombo.currentIndex = (controller.tileSize === "small") ? 0 : (controller.tileSize === "large" ? 2 : 1)
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
        const viewModes = ["grid", "list"]
        const tileSizes = ["small", "medium", "large"]
        controller.applySettings({
            popupHotkey: draftHotkey.trim(),
            hideOnBlur: hideOnBlurCheck.checked,
            hideAfterLaunch: hideAfterCheck.checked,
            singleClickLaunch: singleClickCheck.checked,
            showAtCursor: showAtCursorCheck.checked,
            topMost: topMostCheck.checked,
            viewMode: viewModes[viewModeCombo.currentIndex] || "grid",
            tileSize: tileSizes[tileSizeCombo.currentIndex] || "medium"
        })
        close()
    }

    onOpened: controller.setDialogOpen(true)
    onClosed: {
        stopRecording()
        controller.setDialogOpen(false)
    }

    Overlay.modal: Rectangle {
        color: theme.withAlpha("#000000", theme.dark ? 0.52 : 0.32)
    }

    background: Rectangle {
        color: theme.layer
        radius: theme.cardRadius
        border.width: 1
        border.color: theme.windowEdge
    }

    component SettingBtn: Button {
        id: sbtn
        implicitHeight: 32
        leftPadding: 12
        rightPadding: 12
        contentItem: Text {
            text: sbtn.text
            color: theme.text
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: sbtn.down ? theme.pressed : (sbtn.hovered ? theme.controlHover : theme.control)
            radius: Math.min(theme.controlRadius, 6)
            border.width: 1
            border.color: theme.controlBorder
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

    component SettingCheck: CheckBox {
        id: schk
        spacing: 8
        indicator: Rectangle {
            implicitWidth: 16
            implicitHeight: 16
            x: schk.leftPadding
            y: parent.height / 2 - height / 2
            radius: Math.min(theme.controlRadius, 4)
            color: schk.checked ? theme.accentFill : theme.control
            border.width: 1
            border.color: schk.checked ? theme.accentFill : theme.controlBorder
            Canvas {
                anchors.fill: parent
                visible: schk.checked
                property color strokeColor: theme.accentText
                onStrokeColorChanged: requestPaint()
                onVisibleChanged: if (visible) requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const c = strokeColor
                    ctx.strokeStyle = "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
                    ctx.lineWidth = 1.6
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(3.5, 8.2)
                    ctx.lineTo(6.6, 11.2)
                    ctx.lineTo(12.5, 4.8)
                    ctx.stroke()
                }
            }
        }
        contentItem: Text {
            text: schk.text
            color: theme.text
            font.pixelSize: 13
            leftPadding: schk.indicator.width + schk.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }
    component SettingCombo: ComboBox {
        id: scmb
        implicitHeight: 30
        contentItem: Text {
            leftPadding: 10
            rightPadding: 26
            text: scmb.displayText
            color: theme.text
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: theme.control
            radius: theme.controlRadius
            border.width: 1
            border.color: scmb.activeFocus ? theme.inputFocused : theme.controlBorder
        }
        popup: Popup {
            y: scmb.height + 2
            width: scmb.width
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 200)
            padding: 4
            background: Rectangle {
                color: theme.menuBackground
                radius: theme.controlRadius
                border.width: 1
                border.color: theme.controlBorder
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: scmb.popup.visible ? scmb.delegateModel : null
            }
        }
        delegate: ItemDelegate {
            width: scmb.width - 8
            height: 28
            highlighted: scmb.highlightedIndex === index
            contentItem: Text {
                text: String(modelData)
                color: theme.text
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: highlighted ? theme.menuHover : "transparent"
                radius: theme.controlRadius
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 46
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14

                Text {
                    text: "快速启动面板设置"
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                DialogCloseBtn {
                    onClicked: root.close()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.withAlpha(theme.divider, 0.7)
            }
        }
        ScrollView {
            id: scrollArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scrollArea.availableWidth
                spacing: 16

                Item { height: 2 }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 8

                    Text { text: "全局快捷键"; color: theme.subtext; font.pixelSize: 12; font.weight: Font.DemiBold }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            id: hotkeyCatcher
                            Layout.fillWidth: true
                            height: 32
                            radius: theme.controlRadius
                            color: root.recordingHotkey ? theme.withAlpha(theme.accent, 0.14) : theme.control
                            border.width: 1
                            border.color: root.recordingHotkey ? theme.inputFocused : theme.controlBorder
                            focus: root.recordingHotkey

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                font.pixelSize: 13
                                color: root.recordingHotkey ? theme.accent : (root.draftHotkey.length > 0 ? theme.text : theme.faint)
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
                            text: "Alt+1"
                            onClicked: {
                                root.stopRecording()
                                root.draftHotkey = "Alt+1"
                                root.recordHint = ""
                            }
                        }
                    }

                    Text {
                        visible: root.recordHint.length > 0 || (!controller.hotkeyRegistered && controller.hotkeyErrorText.length > 0)
                        text: root.recordHint.length > 0 ? root.recordHint : controller.hotkeyErrorText
                        color: theme.danger
                        font.pixelSize: 12
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
                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18; height: 1; color: theme.withAlpha(theme.divider, 0.7) }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 8

                    Text { text: "行为与外观"; color: theme.subtext; font.pixelSize: 12; font.weight: Font.DemiBold }

                    SettingCheck { id: hideOnBlurCheck; text: "失去焦点时自动隐藏面板" }
                    SettingCheck { id: hideAfterCheck; text: "启动程序后自动隐藏面板" }
                    SettingCheck { id: singleClickCheck; text: "单击卡片直接启动（取消勾选则双击启动）" }
                    SettingCheck { id: showAtCursorCheck; text: "呼出时跟随鼠标指针位置" }
                    SettingCheck { id: topMostCheck; text: "窗口置顶显示" }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8

                        Text { text: "默认视图模式"; color: theme.subtext; font.pixelSize: 12 }
                        SettingCombo {
                            id: viewModeCombo
                            Layout.fillWidth: true
                            model: ["图标网格", "紧凑列表"]
                        }

                        Text { text: "图标卡片尺寸"; color: theme.subtext; font.pixelSize: 12 }
                        SettingCombo {
                            id: tileSizeCombo
                            Layout.fillWidth: true
                            model: ["小图标", "中等", "大图标"]
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18; height: 1; color: theme.withAlpha(theme.divider, 0.7) }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 8

                    Text { text: "数据导入与备份"; color: theme.subtext; font.pixelSize: 12; font.weight: Font.DemiBold }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        SettingBtn {
                            visible: controller.hasDefaultMayeFile
                            text: "一键导入默认 MayeNano"
                            onClicked: controller.importMayeDefault()
                        }
                        SettingBtn {
                            text: "导入 MDB.json..."
                            onClicked: controller.importMayeFromDialog()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        SettingBtn {
                            text: "导出备份 JSON..."
                            onClicked: controller.exportBackupDialog()
                        }
                        SettingBtn {
                            text: "恢复备份 JSON..."
                            onClicked: controller.importBackupDialog()
                        }
                        SettingBtn {
                            text: "打开数据目录"
                            onClicked: controller.openDataDir()
                        }
                    }
                }

                Item { height: 6 }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 54
            color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: theme.withAlpha(theme.divider, 0.7)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 8

                Item { Layout.fillWidth: true }

                SettingBtn {
                    text: "取消"
                    implicitWidth: 78
                    onClicked: root.close()
                }

                Button {
                    id: saveSettingsBtn
                    text: "保存设置"
                    implicitWidth: 92
                    implicitHeight: 32
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
                        color: saveSettingsBtn.down ? theme.pressed : (saveSettingsBtn.hovered ? theme.accent : theme.accentFill)
                        radius: Math.min(theme.controlRadius, 6)
                    }
                }
            }
        }
    }
}
