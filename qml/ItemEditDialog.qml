import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    width: Math.min(500, Overlay.overlay ? Overlay.overlay.width - 32 : 500)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string editItemId: ""
    property string errorText: ""

    function selectCategoryId(catId) {
        const list = controller.categoryChoices
        let idx = 0
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === catId) {
                idx = i
                break
            }
        }
        categoryCombo.currentIndex = list.length > 0 ? idx : -1
    }

    function openNew(defaultCategoryId) {
        editItemId = ""
        errorText = ""
        nameField.text = ""
        targetField.text = ""
        argsField.text = ""
        workDirField.text = ""
        iconField.text = ""
        keywordsField.text = ""
        remarkField.text = ""
        adminCheck.checked = false
        selectCategoryId(defaultCategoryId)
        open()
    }

    function openEdit(itemId) {
        const info = controller.getItemDetails(itemId)
        if (!info || !info.itemId) {
            return
        }
        editItemId = info.itemId
        errorText = ""
        nameField.text = info.name || ""
        targetField.text = info.target || ""
        argsField.text = info.arguments || ""
        workDirField.text = info.workingDir || ""
        iconField.text = info.iconPath || ""
        keywordsField.text = info.keywords || ""
        remarkField.text = info.remark || ""
        adminCheck.checked = !!info.runAsAdmin
        selectCategoryId(info.categoryId || "")
        open()
    }

    function submit() {
        const tgt = targetField.text.trim()
        if (tgt.length === 0) {
            errorText = "请填写目标程序路径、文件夹或网址"
            targetField.forceActiveFocus()
            return
        }
        const choices = controller.categoryChoices
        const chosenCat = (categoryCombo.currentIndex >= 0 && categoryCombo.currentIndex < choices.length)
            ? choices[categoryCombo.currentIndex].id : ""
        const ok = controller.saveItem({
            itemId: editItemId,
            categoryId: chosenCat,
            name: nameField.text.trim(),
            target: tgt,
            arguments: argsField.text.trim(),
            workingDir: workDirField.text.trim(),
            iconPath: iconField.text.trim(),
            keywords: keywordsField.text.trim(),
            remark: remarkField.text.trim(),
            runAsAdmin: adminCheck.checked
        })
        if (ok) {
            close()
        } else {
            errorText = controller.statusError ? controller.statusMessage : "保存失败"
        }
    }
    onOpened: {
        controller.setDialogOpen(true)
        nameField.forceActiveFocus()
    }
    onClosed: controller.setDialogOpen(false)

    Overlay.modal: Rectangle {
        color: theme.withAlpha("#000000", theme.dark ? 0.52 : 0.32)
    }

    background: Rectangle {
        color: theme.layer
        radius: theme.cardRadius
        border.width: 1
        border.color: theme.windowEdge
    }

    component StyledInput: TextField {
        id: styledField
        color: theme.text
        placeholderTextColor: theme.faint
        selectedTextColor: theme.accentText
        selectionColor: theme.accentFill
        font.pixelSize: 13
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 10
        background: Rectangle {
            color: theme.control
            radius: Math.min(theme.controlRadius, 6)
            border.width: 1
            border.color: styledField.activeFocus ? theme.inputFocused : theme.controlBorder
        }
    }

    component SmallActionBtn: Button {
        id: btn
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        contentItem: Text {
            text: btn.text
            color: theme.text
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: btn.down ? theme.pressed : (btn.hovered ? theme.controlHover : theme.control)
            radius: Math.min(theme.controlRadius, 6)
            border.width: 1
            border.color: theme.controlBorder
        }
    }

    component DialogCloseBtn: Button {
        id: cbtn
        implicitWidth: 30
        implicitHeight: 30
        padding: 0
        hoverEnabled: true
        contentItem: Canvas {
            id: closeCanvas
            property color strokeColor: cbtn.hovered ? "#FFFFFF" : theme.subtext
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
            color: cbtn.hovered ? theme.danger : "transparent"
            radius: Math.min(theme.controlRadius, 6)
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
                    text: root.editItemId === "" ? "添加启动项" : "编辑启动项"
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
        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            columns: 2
            columnSpacing: 14
            rowSpacing: 11

            Text { text: "所属分组"; color: theme.subtext; font.pixelSize: 12 }
            ComboBox {
                id: categoryCombo
                Layout.fillWidth: true
                implicitHeight: 34
                model: controller.categoryChoices
                textRole: "name"
                valueRole: "id"
                contentItem: Text {
                    leftPadding: 10
                    rightPadding: 26
                    text: categoryCombo.displayText
                    color: theme.text
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    color: theme.control
                    radius: theme.controlRadius
                    border.width: 1
                    border.color: categoryCombo.activeFocus ? theme.inputFocused : theme.controlBorder
                }
                popup: Popup {
                    y: categoryCombo.height + 2
                    width: categoryCombo.width
                    implicitHeight: Math.min(contentItem.implicitHeight + 8, 220)
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
                        model: categoryCombo.popup.visible ? categoryCombo.delegateModel : null
                    }
                }
                delegate: ItemDelegate {
                    width: categoryCombo.width - 8
                    height: 30
                    highlighted: categoryCombo.highlightedIndex === index
                    contentItem: Text {
                        text: (modelData && modelData.name !== undefined) ? modelData.name : String(modelData)
                        color: theme.text
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: highlighted ? theme.menuHover : "transparent"
                        radius: theme.controlRadius
                    }
                }
            }

            Text { text: "显示名称"; color: theme.subtext; font.pixelSize: 12 }
            StyledInput {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "留空将自动从文件名提取"
                Keys.onReturnPressed: root.submit()
            }
            Text { text: "目标路径"; color: theme.subtext; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                StyledInput {
                    id: targetField
                    Layout.fillWidth: true
                    placeholderText: "程序路径、目录、https:// 网址、$computer"
                    onTextChanged: root.errorText = ""
                    Keys.onReturnPressed: root.submit()
                }
                SmallActionBtn {
                    text: "文件"
                    onClicked: {
                        const p = controller.pickFile("选择程序或文件", "可执行文件与快捷方式 (*.exe;*.lnk;*.bat;*.cmd)", "*.exe;*.lnk;*.bat;*.cmd")
                        if (p && p.length > 0) {
                            targetField.text = p
                            if (nameField.text.trim().length === 0) {
                                const parts = p.replace(/\\/g, "/").split("/")
                                const fname = parts[parts.length - 1] || ""
                                const dot = fname.lastIndexOf(".")
                                nameField.text = dot > 0 ? fname.slice(0, dot) : fname
                            }
                        }
                    }
                }
                SmallActionBtn {
                    text: "目录"
                    onClicked: {
                        const d = controller.pickFolder("选择目标文件夹")
                        if (d && d.length > 0) {
                            targetField.text = d
                            if (nameField.text.trim().length === 0) {
                                const parts = d.replace(/\\/g, "/").split("/")
                                nameField.text = parts[parts.length - 1] || d
                            }
                        }
                    }
                }
            }

            Text { text: "启动参数"; color: theme.subtext; font.pixelSize: 12 }
            StyledInput {
                id: argsField
                Layout.fillWidth: true
                placeholderText: "可选命令行参数"
                Keys.onReturnPressed: root.submit()
            }

            Text { text: "工作目录"; color: theme.subtext; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                StyledInput {
                    id: workDirField
                    Layout.fillWidth: true
                    placeholderText: "可选工作目录"
                    Keys.onReturnPressed: root.submit()
                }
                SmallActionBtn {
                    text: "浏览"
                    onClicked: {
                        const d = controller.pickFolder("选择工作目录")
                        if (d && d.length > 0) workDirField.text = d
                    }
                }
            }

            Text { text: "自定义图标"; color: theme.subtext; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                StyledInput {
                    id: iconField
                    Layout.fillWidth: true
                    placeholderText: "留空使用目标默认图标，支持 path,index"
                    Keys.onReturnPressed: root.submit()
                }
                SmallActionBtn {
                    text: "浏览"
                    onClicked: {
                        const p = controller.pickFile("选择图标", "图标与程序 (*.ico;*.exe;*.dll;*.png)", "*.ico;*.exe;*.dll;*.png")
                        if (p && p.length > 0) iconField.text = p
                    }
                }
            }
            Text { text: "别名关键词"; color: theme.subtext; font.pixelSize: 12 }
            StyledInput {
                id: keywordsField
                Layout.fillWidth: true
                placeholderText: "空格分隔，如 jsb note 记事本"
                Keys.onReturnPressed: root.submit()
            }

            Text { text: "备注说明"; color: theme.subtext; font.pixelSize: 12 }
            StyledInput {
                id: remarkField
                Layout.fillWidth: true
                placeholderText: "可选备注信息"
                Keys.onReturnPressed: root.submit()
            }

            Item { width: 1; height: 1 }
            CheckBox {
                id: adminCheck
                text: "以管理员身份运行"
                indicator: Rectangle {
                    implicitWidth: 16
                    implicitHeight: 16
                    x: adminCheck.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: Math.min(theme.controlRadius, 4)
                    color: adminCheck.checked ? theme.accentFill : theme.control
                    border.width: 1
                    border.color: adminCheck.checked ? theme.accentFill : theme.controlBorder
                    Canvas {
                        anchors.fill: parent
                        visible: adminCheck.checked
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
                    text: adminCheck.text
                    color: theme.text
                    font.pixelSize: 13
                    leftPadding: adminCheck.indicator.width + adminCheck.spacing
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Text {
            visible: root.errorText.length > 0
            text: root.errorText
            color: theme.danger
            font.pixelSize: 12
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            wrapMode: Text.WordWrap
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

                SmallActionBtn {
                    text: "取消"
                    implicitWidth: 78
                    implicitHeight: 32
                    onClicked: root.close()
                }

                Button {
                    id: saveBtn
                    text: "保存"
                    implicitWidth: 86
                    implicitHeight: 32
                    onClicked: root.submit()
                    contentItem: Text {
                        text: saveBtn.text
                        color: theme.accentText
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: saveBtn.down ? theme.pressed : (saveBtn.hovered ? theme.accent : theme.accentFill)
                        radius: Math.min(theme.controlRadius, 6)
                    }
                }
            }
        }
    }
}
