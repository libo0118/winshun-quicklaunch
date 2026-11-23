import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: false
    width: 760
    height: 520
    minimumWidth: 560
    minimumHeight: 380
    color: "transparent"
    title: "快速启动面板"
    flags: Qt.Window | Qt.FramelessWindowHint | (controller.topMost ? Qt.WindowStaysOnTopHint : 0)
    palette.window: theme.layer
    palette.windowText: theme.text
    palette.base: theme.control
    palette.text: theme.text
    palette.button: theme.control
    palette.buttonText: theme.text
    palette.highlight: theme.menuHover
    palette.highlightedText: theme.text
    palette.toolTipBase: theme.menuBackground
    palette.toolTipText: theme.text

    property string draggingItemId: ""
    property string draggingItemName: ""
    property string menuItemId: ""
    property string menuItemCatId: ""
    property string menuCatId: ""
    property string menuCatName: ""
    property int currentRow: 0

    readonly property int gridCols: Math.max(1, Math.floor((gridView.width - 8) / gridCellWidth))
    readonly property int gridCellWidth: controller.tileSize === "small" ? 84 : (controller.tileSize === "large" ? 124 : 102)
    readonly property int gridCellHeight: controller.tileSize === "small" ? 88 : (controller.tileSize === "large" ? 120 : 102)
    readonly property int gridIconSize: controller.tileSize === "small" ? 28 : (controller.tileSize === "large" ? 46 : 36)

    onActiveChanged: {
        if (!active) {
            controller.handleWindowDeactivated()
        }
    }
    onWidthChanged: {
        if (visible) {
            controller.onWindowResized(width, height)
        }
    }
    onHeightChanged: {
        if (visible) {
            controller.onWindowResized(width, height)
        }
    }
    onClosing: (close) => {
        if (!controller.standaloneMode) {
            close.accepted = false
            controller.hidePopup()
        }
    }

    function activeViewCount() {
        return controller.viewMode === "grid" ? gridView.count : listView.count
    }

    function selectRow(idx) {
        const cnt = activeViewCount()
        if (cnt <= 0) {
            currentRow = -1
            return
        }
        const clamped = Math.max(0, Math.min(idx, cnt - 1))
        currentRow = clamped
        if (controller.viewMode === "grid") {
            gridView.currentIndex = clamped
            gridView.positionViewAtIndex(clamped, GridView.Contain)
        } else {
            listView.currentIndex = clamped
            listView.positionViewAtIndex(clamped, ListView.Contain)
        }
    }

    function currentCategoryTitle() {
        const idx = controller.categoryModel.indexOfId(controller.activeCategoryId)
        return idx >= 0 ? controller.categoryModel.nameAt(idx) : "默认"
    }
    function openItemContextMenu(itemId, catId, anchorItem, mx, my) {
        menuItemId = itemId
        menuItemCatId = catId
        while (moveSubMenu.count > 0) {
            const old = moveSubMenu.takeItem(0)
            if (old) old.destroy()
        }
        const choices = controller.categoryChoices
        for (let i = 0; i < choices.length; ++i) {
            const obj = moveSubItemComp.createObject(moveSubMenu, {
                targetCatId: choices[i].id,
                targetCatName: choices[i].name
            })
            if (obj) moveSubMenu.addItem(obj)
        }
        const pt = anchorItem.mapToItem(keyRoot, mx, my)
        itemMenu.popup(keyRoot, pt.x, pt.y)
    }

    Connections {
        target: controller
        function onFocusSearchRequested() {
            searchField.forceActiveFocus()
            searchField.selectAll()
            root.selectRow(0)
        }
        function onOpenSettingsRequested() {
            settingsDialog.openSettings()
        }
        function onDatabaseChanged() {
            root.selectRow(root.currentRow < 0 ? 0 : root.currentRow)
        }
    }

    Connections {
        target: controller.itemModel
        function onModelReset() {
            root.selectRow(0)
        }
    }

    component HeaderIconBtn: Button {
        id: hbtn
        property string tip: ""
        implicitWidth: 30
        implicitHeight: 30
        hoverEnabled: true
        ToolTip.visible: hbtn.hovered && hbtn.tip.length > 0
        ToolTip.delay: 450
        ToolTip.text: hbtn.tip
        contentItem: Text {
            text: hbtn.text
            color: theme.text
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: hbtn.down ? theme.pressed : (hbtn.hovered ? theme.controlHover : theme.control)
            radius: theme.controlRadius
            border.width: 1
            border.color: theme.controlBorder
        }
    }

    component StyledMenuItem: MenuItem {
        id: smi
        implicitHeight: 30
        contentItem: Text {
            text: smi.text
            color: smi.enabled ? theme.text : theme.faint
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
            rightPadding: 14
        }
        background: Rectangle {
            color: smi.highlighted ? theme.menuHover : "transparent"
            radius: Math.max(0, theme.controlRadius - 2)
        }
    }
    Item {
        id: keyRoot
        anchors.fill: parent

        Keys.onPressed: (event) => {
            if (controller.dialogOpen) {
                return
            }
            if (event.key === Qt.Key_Escape) {
                event.accepted = true
                if (searchField.text.length > 0) {
                    searchField.text = ""
                } else if (controller.standaloneMode) {
                    root.close()
                } else {
                    controller.hidePopup()
                }
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
                event.accepted = true
                const back = (event.modifiers & Qt.ShiftModifier) || event.key === Qt.Key_Backtab
                controller.cycleCategory(back ? -1 : 1)
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                event.accepted = true
                controller.launchItemAt(event.key - Qt.Key_1, (event.modifiers & Qt.ShiftModifier) !== 0)
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_F) {
                event.accepted = true
                searchField.forceActiveFocus()
                searchField.selectAll()
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_N) {
                event.accepted = true
                itemEditDialog.openNew(controller.activeCategoryId)
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Comma) {
                event.accepted = true
                settingsDialog.openSettings()
                return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                const idx = root.currentRow >= 0 ? root.currentRow : 0
                if (event.modifiers & Qt.ControlModifier) {
                    const id = controller.itemModel.idAt(idx)
                    if (id.length > 0) controller.revealItem(id)
                } else {
                    controller.launchItemAt(idx, (event.modifiers & Qt.ShiftModifier) !== 0)
                }
                return
            }
            if (event.key === Qt.Key_Down) {
                event.accepted = true
                root.selectRow(root.currentRow + (controller.viewMode === "grid" ? root.gridCols : 1))
                return
            }
            if (event.key === Qt.Key_Up) {
                event.accepted = true
                root.selectRow(root.currentRow - (controller.viewMode === "grid" ? root.gridCols : 1))
                return
            }
            if (event.key === Qt.Key_Right && controller.viewMode === "grid"
                && (!searchField.activeFocus || searchField.text.length === 0)) {
                event.accepted = true
                root.selectRow(root.currentRow + 1)
                return
            }
            if (event.key === Qt.Key_Left && controller.viewMode === "grid"
                && (!searchField.activeFocus || searchField.text.length === 0)) {
                event.accepted = true
                root.selectRow(root.currentRow - 1)
            }
        }
        Rectangle {
            id: windowBase
            anchors.fill: parent
            radius: theme.cardRadius
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 1.0)
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: theme.cardRadius
                color: theme.background
                border.width: 1
                border.color: theme.windowEdge

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    Rectangle {
                        id: headerBar
                        Layout.fillWidth: true
                        height: 50
                        color: "transparent"

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            onPressed: root.startSystemMove()
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8

                            Rectangle {
                                width: 26
                                height: 26
                                radius: theme.controlRadius
                                color: theme.accentFill

                                Text {
                                    anchors.centerIn: parent
                                    text: "Q"
                                    color: theme.accentText
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32
                                radius: theme.controlRadius
                                color: theme.control
                                border.width: 1
                                border.color: searchField.activeFocus ? theme.inputFocused : theme.controlBorder

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 6
                                    spacing: 6

                                    Text {
                                        text: "⌕"
                                        color: theme.subtext
                                        font.pixelSize: 15
                                    }

                                    TextField {
                                        id: searchField
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        focus: true
                                        color: theme.text
                                        placeholderText: "搜索名称、拼音首字母或路径..."
                                        placeholderTextColor: theme.faint
                                        selectedTextColor: theme.accentText
                                        selectionColor: theme.accentFill
                                        font.pixelSize: 13
                                        verticalAlignment: TextInput.AlignVCenter
                                        background: Item {}
                                        Keys.forwardTo: [keyRoot]
                                        onTextChanged: controller.searchText = text
                                        Connections {
                                            target: controller
                                            function onSearchTextChanged() {
                                                if (searchField.text !== controller.searchText) {
                                                    searchField.text = controller.searchText
                                                }
                                            }
                                        }
                                    }
                                    Button {
                                        visible: searchField.text.length > 0
                                        implicitWidth: 22
                                        implicitHeight: 22
                                        onClicked: {
                                            searchField.text = ""
                                            searchField.forceActiveFocus()
                                        }
                                        contentItem: Text {
                                            text: "✕"
                                            color: theme.subtext
                                            font.pixelSize: 11
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: parent.hovered ? theme.hover : "transparent"
                                            radius: theme.controlRadius
                                        }
                                    }
                                }
                            }

                            HeaderIconBtn {
                                text: controller.viewMode === "grid" ? "☰" : "⊞"
                                tip: controller.viewMode === "grid" ? "切换为紧凑列表" : "切换为图标网格"
                                onClicked: controller.viewMode = (controller.viewMode === "grid" ? "list" : "grid")
                            }

                            HeaderIconBtn {
                                visible: controller.viewMode === "grid"
                                text: controller.tileSize === "small" ? "小" : (controller.tileSize === "large" ? "大" : "中")
                                tip: "切换图标尺寸 (小/中/大)"
                                onClicked: {
                                    const next = controller.tileSize === "small" ? "medium"
                                        : (controller.tileSize === "medium" ? "large" : "small")
                                    controller.tileSize = next
                                }
                            }

                            HeaderIconBtn {
                                text: "+"
                                tip: "添加启动项 (Ctrl+N)"
                                onClicked: itemEditDialog.openNew(controller.activeCategoryId)
                            }

                            HeaderIconBtn {
                                text: "导"
                                tip: controller.hasDefaultMayeFile ? "一键导入 MayeNano (MDB.json)" : "选择 MDB.json 导入"
                                onClicked: controller.hasDefaultMayeFile ? controller.importMayeDefault() : controller.importMayeFromDialog()
                            }

                            HeaderIconBtn {
                                text: "⚙"
                                tip: "设置 (Ctrl+,)"
                                onClicked: settingsDialog.openSettings()
                            }

                            HeaderIconBtn {
                                text: "✕"
                                tip: controller.standaloneMode ? "退出" : "隐藏面板 (Esc)"
                                onClicked: controller.standaloneMode ? root.close() : controller.hidePopup()
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: theme.divider
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 0

                        Rectangle {
                            id: sidebar
                            Layout.preferredWidth: 164
                            Layout.fillHeight: true
                            color: theme.layer

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                ListView {
                                    id: categoryList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.margins: 6
                                    spacing: 3
                                    clip: true
                                    model: controller.categoryModel

                                    delegate: Item {
                                        width: categoryList.width
                                        height: 34

                                        readonly property bool isSelected: categoryId === controller.activeCategoryId

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: theme.controlRadius
                                            color: catDrop.containsDrag
                                                ? theme.withAlpha(theme.accent, 0.22)
                                                : (isSelected ? theme.withAlpha(theme.accent, 0.14)
                                                    : (catMouse.containsMouse ? theme.hover : "transparent"))
                                            border.width: catDrop.containsDrag ? 1 : 0
                                            border.color: theme.accent

                                            Rectangle {
                                                visible: isSelected
                                                width: 3
                                                height: 16
                                                radius: 1.5
                                                color: theme.accent
                                                anchors.left: parent.left
                                                anchors.leftMargin: 3
                                                anchors.verticalCenter: parent.verticalCenter
                                            }

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 12
                                                anchors.rightMargin: 8
                                                spacing: 6

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: name
                                                    color: isSelected ? theme.text : theme.subtext
                                                    font.pixelSize: 13
                                                    font.weight: isSelected ? Font.DemiBold : Font.Normal
                                                    elide: Text.ElideRight
                                                    verticalAlignment: Text.AlignVCenter
                                                }

                                                Rectangle {
                                                    implicitWidth: Math.max(20, countText.implicitWidth + 8)
                                                    height: 18
                                                    radius: theme.rounded ? 9 : 0
                                                    color: isSelected ? theme.withAlpha(theme.accent, 0.22) : theme.control

                                                    Text {
                                                        id: countText
                                                        anchors.centerIn: parent
                                                        text: String(count)
                                                        color: isSelected ? theme.accent : theme.faint
                                                        font.pixelSize: 11
                                                    }
                                                }
                                            }
                                        }

                                        DropArea {
                                            anchors.fill: parent
                                            id: catDrop
                                            onEntered: (drag) => {
                                                drag.accepted = !isAll && (drag.hasUrls || root.draggingItemId.length > 0)
                                            }
                                            onDropped: (drop) => {
                                                if (isAll) return
                                                if (drop.hasUrls) {
                                                    controller.addDroppedUrls(drop.urls, categoryId)
                                                } else if (root.draggingItemId.length > 0) {
                                                    controller.moveItemToCategory(root.draggingItemId, categoryId)
                                                }
                                            }
                                        }

                                        MouseArea {
                                            id: catMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: (mouse) => {
                                                if (mouse.button === Qt.LeftButton) {
                                                    controller.selectCategory(categoryId)
                                                } else if (mouse.button === Qt.RightButton && !isAll) {
                                                    root.menuCatId = categoryId
                                                    root.menuCatName = name
                                                    const pt = mapToItem(keyRoot, mouse.x, mouse.y)
                                                    categoryMenu.popup(keyRoot, pt.x, pt.y)
                                                }
                                            }
                                        }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: theme.divider }

                                Button {
                                    id: addCatBtn
                                    Layout.fillWidth: true
                                    Layout.margins: 6
                                    height: 30
                                    text: "+ 新建分组"
                                    onClicked: categoryPrompt.openCreate()
                                    contentItem: Text {
                                        text: addCatBtn.text
                                        color: theme.subtext
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        color: addCatBtn.down ? theme.pressed : (addCatBtn.hovered ? theme.hover : "transparent")
                                        radius: theme.controlRadius
                                        border.width: 1
                                        border.color: addCatBtn.hovered ? theme.controlBorder : "transparent"
                                    }
                                }
                            }

                            Rectangle {
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.right: parent.right
                                width: 1
                                color: theme.divider
                            }
                        }

                        Rectangle {
                            id: contentPane
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: theme.page
                            clip: true

                            GridView {
                                id: gridView
                                visible: controller.viewMode === "grid" && count > 0
                                anchors.fill: parent
                                anchors.margins: 8
                                cellWidth: Math.floor(width / root.gridCols)
                                cellHeight: root.gridCellHeight
                                clip: true
                                model: controller.itemModel
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Item {
                                    id: gridCardWrap
                                    width: gridView.cellWidth
                                    height: gridView.cellHeight

                                    readonly property bool isCurrent: index === root.currentRow

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        radius: theme.cardRadius
                                        color: cardMouse.pressed
                                            ? theme.pressed
                                            : (isCurrent ? theme.withAlpha(theme.accent, 0.14)
                                                : (cardMouse.containsMouse ? theme.hover : "transparent"))
                                        border.width: isCurrent ? 1 : 0
                                        border.color: theme.accent
                                        Rectangle {
                                            visible: runAsAdmin
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.margins: 4
                                            width: 16
                                            height: 15
                                            radius: theme.rounded ? 4 : 0
                                            color: theme.withAlpha(theme.danger, 0.18)
                                            border.width: 1
                                            border.color: theme.withAlpha(theme.danger, 0.55)
                                            Text {
                                                anchors.centerIn: parent
                                                text: "管"
                                                color: theme.danger
                                                font.pixelSize: 9
                                                font.weight: Font.Bold
                                            }
                                        }

                                        Rectangle {
                                            visible: shortcutHint.length > 0
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: 4
                                            implicitWidth: hintTxt.implicitWidth + 6
                                            height: 15
                                            radius: theme.rounded ? 4 : 0
                                            color: theme.withAlpha(theme.control, 0.9)
                                            border.width: 1
                                            border.color: theme.divider
                                            Text {
                                                id: hintTxt
                                                anchors.centerIn: parent
                                                text: shortcutHint
                                                color: theme.faint
                                                font.pixelSize: 9
                                            }
                                        }

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.topMargin: 14
                                            anchors.bottomMargin: 6
                                            anchors.leftMargin: 6
                                            anchors.rightMargin: 6
                                            spacing: 5

                                            Item {
                                                Layout.alignment: Qt.AlignHCenter
                                                width: root.gridIconSize
                                                height: root.gridIconSize

                                                Image {
                                                    id: gridIconImg
                                                    anchors.fill: parent
                                                    source: iconUrl
                                                    asynchronous: true
                                                    fillMode: Image.PreserveAspectFit
                                                    sourceSize: Qt.size(root.gridIconSize * 2, root.gridIconSize * 2)
                                                    visible: status === Image.Ready
                                                }

                                                Rectangle {
                                                    anchors.fill: parent
                                                    visible: gridIconImg.status !== Image.Ready
                                                    radius: theme.controlRadius
                                                    color: theme.withAlpha(theme.accentFill, 0.18)
                                                    border.width: 1
                                                    border.color: theme.withAlpha(theme.accent, 0.35)
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: name.length > 0 ? name.slice(0, 1).toUpperCase() : "?"
                                                        color: theme.accent
                                                        font.pixelSize: Math.round(root.gridIconSize * 0.48)
                                                        font.weight: Font.Bold
                                                    }
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                text: name
                                                color: theme.text
                                                font.pixelSize: 11
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignTop
                                                wrapMode: Text.WrapAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    MouseArea {
                                        id: cardMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        property bool draggingNow: false
                                        property real pressX: 0
                                        property real pressY: 0

                                        ToolTip.visible: containsMouse && root.draggingItemId === ""
                                        ToolTip.delay: 550
                                        ToolTip.text: name + "\n" + target + (remark.length > 0 ? "\n" + remark : "")

                                        onPressed: (mouse) => {
                                            root.selectRow(index)
                                            pressX = mouse.x
                                            pressY = mouse.y
                                            draggingNow = false
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(pressedButtons & Qt.LeftButton)) return
                                            if (!draggingNow && Math.hypot(mouse.x - pressX, mouse.y - pressY) > 10) {
                                                draggingNow = true
                                                root.draggingItemId = itemId
                                                root.draggingItemName = name
                                                dragProxy.Drag.active = true
                                            }
                                            if (draggingNow) {
                                                const pt = mapToItem(keyRoot, mouse.x, mouse.y)
                                                dragProxy.x = pt.x - dragProxy.width / 2
                                                dragProxy.y = pt.y - dragProxy.height / 2
                                            }
                                        }
                                        onReleased: {
                                            if (draggingNow) {
                                                dragProxy.Drag.drop()
                                                dragProxy.Drag.active = false
                                                root.draggingItemId = ""
                                                root.draggingItemName = ""
                                                draggingNow = false
                                            }
                                        }
                                        onClicked: (mouse) => {
                                            if (draggingNow) return
                                            root.selectRow(index)
                                            if (mouse.button === Qt.LeftButton && controller.singleClickLaunch) {
                                                controller.launchItem(itemId, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                            } else if (mouse.button === Qt.RightButton) {
                                                root.openItemContextMenu(itemId, categoryId, gridCardWrap, mouse.x, mouse.y)
                                            }
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button === Qt.LeftButton && !controller.singleClickLaunch) {
                                                controller.launchItem(itemId, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                            }
                                        }
                                    }
                                }
                            }

                            ListView {
                                id: listView
                                visible: controller.viewMode === "list" && count > 0
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4
                                clip: true
                                model: controller.itemModel
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Item {
                                    id: listRowWrap
                                    width: listView.width
                                    height: 44

                                    readonly property bool isCurrent: index === root.currentRow

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: theme.controlRadius
                                        color: rowMouse.pressed
                                            ? theme.pressed
                                            : (isCurrent ? theme.withAlpha(theme.accent, 0.14)
                                                : (rowMouse.containsMouse ? theme.hover : "transparent"))
                                        border.width: isCurrent ? 1 : 0
                                        border.color: theme.accent
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            spacing: 10

                                            Item {
                                                width: 24
                                                height: 24

                                                Image {
                                                    id: listIconImg
                                                    anchors.fill: parent
                                                    source: iconUrl
                                                    asynchronous: true
                                                    fillMode: Image.PreserveAspectFit
                                                    sourceSize: Qt.size(48, 48)
                                                    visible: status === Image.Ready
                                                }

                                                Rectangle {
                                                    anchors.fill: parent
                                                    visible: listIconImg.status !== Image.Ready
                                                    radius: theme.controlRadius
                                                    color: theme.withAlpha(theme.accentFill, 0.18)
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: name.length > 0 ? name.slice(0, 1).toUpperCase() : "?"
                                                        color: theme.accent
                                                        font.pixelSize: 12
                                                        font.weight: Font.Bold
                                                    }
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 6
                                                    Text {
                                                        text: name
                                                        color: theme.text
                                                        font.pixelSize: 13
                                                        font.weight: Font.Medium
                                                        elide: Text.ElideRight
                                                        Layout.maximumWidth: 260
                                                    }
                                                    Rectangle {
                                                        visible: runAsAdmin
                                                        implicitWidth: 16
                                                        height: 15
                                                        radius: theme.rounded ? 4 : 0
                                                        color: theme.withAlpha(theme.danger, 0.18)
                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "管"
                                                            color: theme.danger
                                                            font.pixelSize: 9
                                                            font.weight: Font.Bold
                                                        }
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: target + (remark.length > 0 ? " · " + remark : "")
                                                    color: theme.subtext
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Rectangle {
                                                visible: controller.activeCategoryId === "__all__" && categoryName.length > 0
                                                implicitWidth: catTagTxt.implicitWidth + 10
                                                height: 18
                                                radius: theme.controlRadius
                                                color: theme.control
                                                Text {
                                                    id: catTagTxt
                                                    anchors.centerIn: parent
                                                    text: categoryName
                                                    color: theme.subtext
                                                    font.pixelSize: 10
                                                }
                                            }

                                            Rectangle {
                                                visible: shortcutHint.length > 0
                                                implicitWidth: listHintTxt.implicitWidth + 8
                                                height: 18
                                                radius: theme.controlRadius
                                                color: theme.control
                                                border.width: 1
                                                border.color: theme.divider
                                                Text {
                                                    id: listHintTxt
                                                    anchors.centerIn: parent
                                                    text: shortcutHint
                                                    color: theme.faint
                                                    font.pixelSize: 10
                                                }
                                            }
                                        }
                                    }
                                    MouseArea {
                                        id: rowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        property bool draggingNow: false
                                        property real pressX: 0
                                        property real pressY: 0

                                        ToolTip.visible: containsMouse && root.draggingItemId === ""
                                        ToolTip.delay: 550
                                        ToolTip.text: name + "\n" + target + (remark.length > 0 ? "\n" + remark : "")

                                        onPressed: (mouse) => {
                                            root.selectRow(index)
                                            pressX = mouse.x
                                            pressY = mouse.y
                                            draggingNow = false
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(pressedButtons & Qt.LeftButton)) return
                                            if (!draggingNow && Math.hypot(mouse.x - pressX, mouse.y - pressY) > 10) {
                                                draggingNow = true
                                                root.draggingItemId = itemId
                                                root.draggingItemName = name
                                                dragProxy.Drag.active = true
                                            }
                                            if (draggingNow) {
                                                const pt = mapToItem(keyRoot, mouse.x, mouse.y)
                                                dragProxy.x = pt.x - dragProxy.width / 2
                                                dragProxy.y = pt.y - dragProxy.height / 2
                                            }
                                        }
                                        onReleased: {
                                            if (draggingNow) {
                                                dragProxy.Drag.drop()
                                                dragProxy.Drag.active = false
                                                root.draggingItemId = ""
                                                root.draggingItemName = ""
                                                draggingNow = false
                                            }
                                        }
                                        onClicked: (mouse) => {
                                            if (draggingNow) return
                                            root.selectRow(index)
                                            if (mouse.button === Qt.LeftButton && controller.singleClickLaunch) {
                                                controller.launchItem(itemId, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                            } else if (mouse.button === Qt.RightButton) {
                                                root.openItemContextMenu(itemId, categoryId, listRowWrap, mouse.x, mouse.y)
                                            }
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button === Qt.LeftButton && !controller.singleClickLaunch) {
                                                controller.launchItem(itemId, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                            }
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 48, 340)
                                visible: root.activeViewCount() === 0 && !rightDrop.containsDrag
                                spacing: 10

                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: controller.searchText.length > 0 ? "🔍" : "📂"
                                    font.pixelSize: 28
                                    color: theme.faint
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: controller.searchText.length > 0
                                        ? "没有匹配的启动项，可换个关键词或拼音首字母"
                                        : "当前分组暂无启动项\n直接拖入程序、快捷方式或文件夹即可添加"
                                    color: theme.subtext
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: 8
                                    visible: controller.searchText.length === 0

                                    Button {
                                        id: emptyAddBtn
                                        text: "+ 添加启动项"
                                        implicitHeight: 30
                                        leftPadding: 12
                                        rightPadding: 12
                                        onClicked: itemEditDialog.openNew(controller.activeCategoryId)
                                        contentItem: Text {
                                            text: emptyAddBtn.text
                                            color: theme.accentText
                                            font.pixelSize: 12
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: emptyAddBtn.hovered ? theme.accent : theme.accentFill
                                            radius: theme.controlRadius
                                        }
                                    }

                                    Button {
                                        id: emptyImportBtn
                                        visible: controller.hasDefaultMayeFile
                                        text: "导入 MayeNano"
                                        implicitHeight: 30
                                        leftPadding: 12
                                        rightPadding: 12
                                        onClicked: controller.importMayeDefault()
                                        contentItem: Text {
                                            text: emptyImportBtn.text
                                            color: theme.text
                                            font.pixelSize: 12
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            color: emptyImportBtn.hovered ? theme.controlHover : theme.control
                                            radius: theme.controlRadius
                                            border.width: 1
                                            border.color: theme.controlBorder
                                        }
                                    }
                                }
                            }

                            DropArea {
                                id: rightDrop
                                anchors.fill: parent
                                onEntered: (drag) => {
                                    drag.accepted = drag.hasUrls
                                }
                                onDropped: (drop) => {
                                    if (drop.hasUrls) {
                                        controller.addDroppedUrls(drop.urls, controller.activeCategoryId)
                                    }
                                }
                            }

                            Rectangle {
                                visible: rightDrop.containsDrag
                                anchors.fill: parent
                                anchors.margins: 10
                                radius: theme.cardRadius
                                color: theme.withAlpha(theme.accent, 0.14)
                                border.width: 2
                                border.color: theme.accent

                                Text {
                                    anchors.centerIn: parent
                                    text: "松开鼠标，添加到「" + root.currentCategoryTitle() + "」"
                                    color: theme.accent
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 28
                        color: theme.layer

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 1
                            color: theme.divider
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Text {
                                readonly property bool warn: !controller.hotkeyRegistered && controller.hotkeyKey.length > 0
                                text: controller.hotkeyKey.length === 0
                                    ? "热键已关闭"
                                    : (controller.hotkeyRegistered ? "热键: " + controller.hotkeyKey : "⚠ 热键冲突: " + controller.hotkeyKey)
                                color: warn ? theme.danger : theme.subtext
                                font.pixelSize: 11
                            }

                            Text {
                                Layout.fillWidth: true
                                text: controller.statusMessage
                                color: controller.statusError ? theme.danger : theme.subtext
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }

                            Text {
                                text: gridView.count + " 项 · Enter 启动 · Ctrl+Enter 定位"
                                color: theme.faint
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        // 5px edge & corner resize handles
        MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.leftMargin: 6; anchors.rightMargin: 6; height: 5; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.TopEdge) }
        MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.leftMargin: 6; anchors.rightMargin: 6; height: 5; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.BottomEdge) }
        MouseArea { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 6; anchors.bottomMargin: 6; width: 5; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.LeftEdge) }
        MouseArea { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 6; anchors.bottomMargin: 6; width: 5; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.RightEdge) }
        MouseArea { anchors.left: parent.left; anchors.top: parent.top; width: 7; height: 7; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.LeftEdge | Qt.TopEdge) }
        MouseArea { anchors.right: parent.right; anchors.top: parent.top; width: 7; height: 7; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.RightEdge | Qt.TopEdge) }
        MouseArea { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 7; height: 7; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.LeftEdge | Qt.BottomEdge) }
        MouseArea { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 7; height: 7; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.RightEdge | Qt.BottomEdge) }

        Rectangle {
            id: dragProxy
            visible: root.draggingItemId.length > 0
            width: Math.min(180, dragLabel.implicitWidth + 20)
            height: 28
            radius: theme.controlRadius
            color: theme.accentFill
            z: 999
            Drag.keys: ["ql-item"]
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

            Text {
                id: dragLabel
                anchors.centerIn: parent
                text: "移动: " + root.draggingItemName
                color: theme.accentText
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width - 14
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
    Component {
        id: moveSubItemComp
        StyledMenuItem {
            property string targetCatId: ""
            property string targetCatName: ""
            text: targetCatName
            enabled: targetCatId !== root.menuItemCatId
            onTriggered: controller.moveItemToCategory(root.menuItemId, targetCatId)
        }
    }

    Menu {
        id: itemMenu
        padding: 4
        background: Rectangle {
            implicitWidth: 168
            color: theme.menuBackground
            radius: theme.controlRadius
            border.width: 1
            border.color: theme.controlBorder
        }

        StyledMenuItem { text: "启动"; onTriggered: controller.launchItem(root.menuItemId, false) }
        StyledMenuItem { text: "以管理员身份运行"; onTriggered: controller.launchItem(root.menuItemId, true) }
        StyledMenuItem { text: "打开所在文件夹"; onTriggered: controller.revealItem(root.menuItemId) }
        StyledMenuItem { text: "复制目标路径"; onTriggered: controller.copyItemTarget(root.menuItemId) }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: theme.divider }
        }
        StyledMenuItem { text: "编辑..."; onTriggered: itemEditDialog.openEdit(root.menuItemId) }
        Menu {
            id: moveSubMenu
            title: "移动到分组"
            padding: 4
            background: Rectangle {
                implicitWidth: 148
                color: theme.menuBackground
                radius: theme.controlRadius
                border.width: 1
                border.color: theme.controlBorder
            }
        }
        StyledMenuItem { text: "前移"; onTriggered: controller.moveItemOrder(root.menuItemId, -1) }
        StyledMenuItem { text: "后移"; onTriggered: controller.moveItemOrder(root.menuItemId, 1) }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: theme.divider }
        }
        StyledMenuItem { text: "删除"; onTriggered: controller.deleteItem(root.menuItemId) }
    }

    Menu {
        id: categoryMenu
        padding: 4
        background: Rectangle {
            implicitWidth: 148
            color: theme.menuBackground
            radius: theme.controlRadius
            border.width: 1
            border.color: theme.controlBorder
        }

        StyledMenuItem { text: "重命名分组..."; onTriggered: categoryPrompt.openRename(root.menuCatId, root.menuCatName) }
        StyledMenuItem { text: "上移分组"; onTriggered: controller.moveCategoryOrder(root.menuCatId, -1) }
        StyledMenuItem { text: "下移分组"; onTriggered: controller.moveCategoryOrder(root.menuCatId, 1) }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: theme.divider }
        }
        StyledMenuItem { text: "删除分组"; onTriggered: controller.deleteCategory(root.menuCatId) }
    }
    Popup {
        id: categoryPrompt
        modal: true
        focus: true
        padding: 0
        width: 320
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property bool isRename: false
        property string targetCatId: ""

        function openCreate() {
            isRename = false
            targetCatId = ""
            catInput.text = ""
            open()
        }

        function openRename(catId, currentName) {
            isRename = true
            targetCatId = catId
            catInput.text = currentName
            open()
        }

        function confirm() {
            const name = catInput.text.trim()
            if (name.length === 0) {
                return
            }
            const ok = isRename
                ? controller.renameCategory(targetCatId, name)
                : controller.addCategory(name)
            if (ok) close()
        }

        onOpened: {
            controller.setDialogOpen(true)
            catInput.forceActiveFocus()
            catInput.selectAll()
        }
        onClosed: controller.setDialogOpen(false)

        Overlay.modal: Rectangle { color: theme.withAlpha("#000000", theme.dark ? 0.52 : 0.32) }
        background: Rectangle {
            color: theme.layer
            radius: theme.cardRadius
            border.width: 1
            border.color: theme.windowEdge
        }

        contentItem: ColumnLayout {
            spacing: 12
            Item { height: 2 }
            Text {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: categoryPrompt.isRename ? "重命名分组" : "新建分组"
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            TextField {
                id: catInput
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                implicitHeight: 32
                leftPadding: 10
                rightPadding: 10
                color: theme.text
                placeholderText: "请输入分组名称"
                placeholderTextColor: theme.faint
                selectedTextColor: theme.accentText
                selectionColor: theme.accentFill
                font.pixelSize: 13
                Keys.onReturnPressed: categoryPrompt.confirm()
                background: Rectangle {
                    color: theme.control
                    radius: theme.controlRadius
                    border.width: 1
                    border.color: catInput.activeFocus ? theme.inputFocused : theme.controlBorder
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                spacing: 8
                Item { Layout.fillWidth: true }
                HeaderIconBtn { implicitWidth: 68; text: "取消"; onClicked: categoryPrompt.close() }
                Button {
                    id: catOkBtn
                    text: "确定"
                    implicitWidth: 68
                    implicitHeight: 30
                    onClicked: categoryPrompt.confirm()
                    contentItem: Text {
                        text: catOkBtn.text; color: theme.accentText; font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: catOkBtn.hovered ? theme.accent : theme.accentFill
                        radius: theme.controlRadius
                    }
                }
            }
        }
    }

    ItemEditDialog { id: itemEditDialog }
    SettingsDialog { id: settingsDialog }
}
