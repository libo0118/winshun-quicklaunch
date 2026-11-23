import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: false
    width: 720
    height: 490
    minimumWidth: 500
    minimumHeight: 340
    color: theme.page
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

    property bool searchActive: false
    property bool keyboardNavActive: false
    property bool ctrlHeld: false
    property string draggingItemId: ""
    property string draggingItemName: ""
    property string menuItemId: ""
    property string menuItemCatId: ""
    property string menuCatId: ""
    property string menuCatName: ""
    property int currentRow: 0
    property Item hoverTipTarget: null
    property string hoverTipText: ""

    readonly property int gridIconSize: controller.tileSize === "small" ? 32 : (controller.tileSize === "large" ? 50 : 42)
    readonly property int gridCellWidth: controller.tileSize === "small" ? 82 : (controller.tileSize === "large" ? 118 : 98)
    readonly property int gridCellHeight: controller.tileSize === "small" ? 80 : (controller.tileSize === "large" ? 110 : 94)
    readonly property int gridCols: Math.max(1, Math.floor(gridView.width / gridCellWidth))

    onActiveChanged: {
        if (!active) {
            ctrlHeld = false
            hideHoverTip(null)
            controller.handleWindowDeactivated()
        }
    }
    onVisibleChanged: if (!visible) hideHoverTip(null)
    onDraggingItemIdChanged: if (draggingItemId.length > 0) hideHoverTip(null)
    onWidthChanged: if (visible) controller.onWindowResized(width, height)
    onHeightChanged: if (visible) controller.onWindowResized(width, height)
    onClosing: (close) => {
        if (!controller.standaloneMode && !controller.shuttingDown) {
            close.accepted = false
            controller.hidePopup()
        }
    }

    function formatTipText(raw) {
        if (!raw || raw.length === 0) return ""
        const lines = raw.split("\n")
        for (let i = 0; i < lines.length; ++i) {
            if (lines[i].length > 58) {
                lines[i] = lines[i].slice(0, 57) + "…"
            }
        }
        return lines.join("\n")
    }

    function scheduleHoverTip(targetItem, text, delayMs) {
        const formatted = formatTipText(text)
        if (!targetItem || formatted.length === 0 || root.draggingItemId.length > 0) {
            hideHoverTip(null)
            return
        }
        hoverTipTarget = targetItem
        hoverTipText = formatted
        hoverTipTimer.interval = delayMs > 0 ? delayMs : 500
        hoverTipTimer.restart()
    }

    function hideHoverTip(targetItem) {
        if (targetItem !== null && targetItem !== undefined && hoverTipTarget !== targetItem) {
            return
        }
        hoverTipTimer.stop()
        hoverTipTarget = null
        hoverTipText = ""
        sharedToolTip.visible = false
    }

    function activeViewCount() {
        if (root.searchActive) return searchListView.count
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
        if (root.searchActive) {
            searchListView.currentIndex = clamped
            searchListView.positionViewAtIndex(clamped, ListView.Contain)
        } else if (controller.viewMode === "grid") {
            gridView.currentIndex = clamped
            gridView.positionViewAtIndex(clamped, GridView.Contain)
        } else {
            listView.currentIndex = clamped
            listView.positionViewAtIndex(clamped, ListView.Contain)
        }
    }

    function enterSearch(initialText) {
        if (!root.searchActive) {
            root.searchActive = true
            root.keyboardNavActive = false
        }
        if (initialText && initialText.length > 0) {
            searchField.text = searchField.text + initialText
        }
        searchField.forceActiveFocus()
        searchField.cursorPosition = searchField.text.length
        root.selectRow(0)
    }

    function exitSearch() {
        searchField.text = ""
        controller.searchText = ""
        root.searchActive = false
        root.keyboardNavActive = false
        keyRoot.forceActiveFocus()
        root.selectRow(0)
    }

    function currentCategoryTitle() {
        const idx = controller.categoryModel.indexOfId(controller.activeCategoryId)
        return idx >= 0 ? controller.categoryModel.nameAt(idx) : "常用"
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
            const obj = moveSubItemComp.createObject(null, {
                targetCatId: choices[i].id,
                targetCatName: choices[i].name
            })
            if (obj) moveSubMenu.addItem(obj)
        }
        const pt = anchorItem.mapToItem(keyRoot, mx, my)
        itemMenu.popup(keyRoot, pt.x, pt.y)
    }

    Timer {
        id: statusAutoClearTimer
        interval: 2600
        repeat: false
        onTriggered: controller.clearStatus()
    }

    Connections {
        target: controller
        function onPopupShown() {
            root.searchActive = false
            root.keyboardNavActive = false
            root.ctrlHeld = false
            searchField.text = ""
            keyRoot.forceActiveFocus()
            root.selectRow(0)
        }
        function onOpenSettingsRequested() { settingsDialog.openSettings() }
        function onDatabaseChanged() { root.selectRow(root.currentRow < 0 ? 0 : root.currentRow) }
        function onStatusChanged() {
            if (controller.statusMessage.length > 0 && !controller.statusError) {
                statusAutoClearTimer.restart()
            } else {
                statusAutoClearTimer.stop()
            }
        }
    }

    Connections {
        target: controller.itemModel
        function onModelReset() { root.selectRow(0) }
    }

    component VectorGlyph: Canvas {
        id: glyph
        property string kind: ""
        property color color: theme.text
        property real strokeWidth: 1.35

        onKindChanged: requestPaint()
        onColorChanged: requestPaint()
        onStrokeWidthChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const s = Math.min(width, height)
            if (s <= 0 || kind.length === 0) return
            const c = glyph.color
            ctx.strokeStyle = "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + c.a + ")"
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.translate((width - s) / 2, (height - s) / 2)
            ctx.scale(s / 16.0, s / 16.0)
            ctx.lineWidth = glyph.strokeWidth * (16.0 / s)
            ctx.beginPath()

            if (kind === "search") {
                ctx.arc(6.8, 6.8, 4.2, 0, Math.PI * 2)
                ctx.moveTo(9.9, 9.9)
                ctx.lineTo(13.6, 13.6)
            } else if (kind === "menu") {
                ctx.moveTo(2.6, 4.2)
                ctx.lineTo(13.4, 4.2)
                ctx.moveTo(2.6, 8.0)
                ctx.lineTo(13.4, 8.0)
                ctx.moveTo(2.6, 11.8)
                ctx.lineTo(13.4, 11.8)
            } else if (kind === "close") {
                ctx.moveTo(3.8, 3.8)
                ctx.lineTo(12.2, 12.2)
                ctx.moveTo(12.2, 3.8)
                ctx.lineTo(3.8, 12.2)
            } else if (kind === "clear") {
                ctx.moveTo(4.6, 4.6)
                ctx.lineTo(11.4, 11.4)
                ctx.moveTo(11.4, 4.6)
                ctx.lineTo(4.6, 11.4)
            } else if (kind === "plus") {
                ctx.moveTo(8.0, 3.0)
                ctx.lineTo(8.0, 13.0)
                ctx.moveTo(3.0, 8.0)
                ctx.lineTo(13.0, 8.0)
            } else if (kind === "back") {
                ctx.moveTo(13.2, 8.0)
                ctx.lineTo(3.2, 8.0)
                ctx.moveTo(7.2, 4.0)
                ctx.lineTo(3.0, 8.0)
                ctx.lineTo(7.2, 12.0)
            } else if (kind === "folder") {
                ctx.moveTo(2.0, 4.4)
                ctx.lineTo(6.0, 4.4)
                ctx.lineTo(7.5, 6.0)
                ctx.lineTo(14.0, 6.0)
                ctx.lineTo(14.0, 12.6)
                ctx.lineTo(2.0, 12.6)
                ctx.closePath()
            }
            ctx.stroke()
        }
    }

    component TopIconBtn: Button {
        id: hbtn
        property string kind: ""
        property string tip: ""
        property bool isClose: false
        implicitWidth: 38
        implicitHeight: 36
        padding: 0
        hoverEnabled: true
        onHoveredChanged: {
            if (hovered && tip.length > 0) root.scheduleHoverTip(hbtn, tip, 450)
            else root.hideHoverTip(hbtn)
        }
        onPressed: root.hideHoverTip(hbtn)
        Component.onDestruction: root.hideHoverTip(hbtn)

        contentItem: Item {
            VectorGlyph {
                anchors.centerIn: parent
                width: 15
                height: 15
                kind: hbtn.kind
                color: (hbtn.isClose && (hbtn.hovered || hbtn.down))
                    ? "#FFFFFF"
                    : (hbtn.hovered ? theme.text : theme.subtext)
            }
        }
        background: Rectangle {
            radius: Math.min(theme.controlRadius, 6)
            color: hbtn.isClose
                ? (hbtn.down ? Qt.darker(theme.danger, 1.15) : (hbtn.hovered ? theme.danger : "transparent"))
                : (hbtn.down ? theme.pressed : (hbtn.hovered ? theme.hover : "transparent"))
            Behavior on color { ColorAnimation { duration: 90 } }
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
            radius: Math.max(0, Math.min(theme.controlRadius, 6) - 2)
        }
    }

    Item {
        id: keyRoot
        anchors.fill: parent
        focus: true

        Keys.onReleased: (event) => {
            if (event.key === Qt.Key_Control) {
                root.ctrlHeld = false
            }
        }

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Control) {
                root.ctrlHeld = true
            } else {
                root.ctrlHeld = (event.modifiers & Qt.ControlModifier) !== 0
            }
            if (controller.dialogOpen) {
                return
            }
            if (event.key === Qt.Key_Escape) {
                event.accepted = true
                if (root.searchActive) {
                    root.exitSearch()
                } else if (controller.standaloneMode) {
                    root.close()
                } else {
                    controller.hidePopup()
                }
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
                event.accepted = true
                if (!root.searchActive) {
                    const back = (event.modifiers & Qt.ShiftModifier) || event.key === Qt.Key_Backtab
                    controller.cycleCategory(back ? -1 : 1)
                }
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                event.accepted = true
                controller.launchItemAt(event.key - Qt.Key_1, (event.modifiers & Qt.ShiftModifier) !== 0)
                return
            }
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_F) {
                event.accepted = true
                root.enterSearch("")
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
                if (!root.searchActive) root.keyboardNavActive = true
                const step = (!root.searchActive && controller.viewMode === "grid") ? root.gridCols : 1
                root.selectRow(root.currentRow + step)
                return
            }
            if (event.key === Qt.Key_Up) {
                event.accepted = true
                if (!root.searchActive) root.keyboardNavActive = true
                const step = (!root.searchActive && controller.viewMode === "grid") ? root.gridCols : 1
                root.selectRow(root.currentRow - step)
                return
            }
            if (!root.searchActive && controller.viewMode === "grid" && event.key === Qt.Key_Right) {
                event.accepted = true
                root.keyboardNavActive = true
                root.selectRow(root.currentRow + 1)
                return
            }
            if (!root.searchActive && controller.viewMode === "grid" && event.key === Qt.Key_Left) {
                event.accepted = true
                root.keyboardNavActive = true
                root.selectRow(root.currentRow - 1)
                return
            }
            if ((!root.searchActive || !searchField.activeFocus)
                && event.text.length > 0
                && event.text.trim().length > 0
                && event.text.charCodeAt(0) >= 32
                && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                && event.key !== Qt.Key_Return
                && event.key !== Qt.Key_Enter
                && event.key !== Qt.Key_Escape
                && event.key !== Qt.Key_Tab
                && event.key !== Qt.Key_Backtab
                && event.key !== Qt.Key_Backspace
                && event.key !== Qt.Key_Delete) {
                event.accepted = true
                root.enterSearch(event.text)
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: headerBar
                Layout.fillWidth: true
                height: 38
                color: theme.layer

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: root.startSystemMove()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 2
                    spacing: 4

                    Text {
                        visible: !root.searchActive
                        text: "快速启动"
                        color: theme.subtext
                        font.pixelSize: 13
                        leftPadding: 2
                    }

                    Rectangle {
                        visible: root.searchActive
                        implicitWidth: backRow.implicitWidth + 14
                        height: 28
                        radius: Math.min(theme.controlRadius, 6)
                        color: backMouse.pressed ? theme.pressed : (backMouse.containsMouse ? theme.hover : "transparent")

                        Row {
                            id: backRow
                            anchors.centerIn: parent
                            spacing: 5
                            VectorGlyph {
                                width: 13
                                height: 13
                                anchors.verticalCenter: parent.verticalCenter
                                kind: "back"
                                color: backMouse.containsMouse ? theme.text : theme.subtext
                            }
                            Text {
                                text: "返回面板"
                                color: backMouse.containsMouse ? theme.text : theme.subtext
                                font.pixelSize: 13
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: backMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.exitSearch()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    TopIconBtn {
                        kind: "search"
                        tip: root.searchActive ? "退出搜索 (Esc)" : "搜索 (Ctrl+F 或直接打字)"
                        onClicked: root.searchActive ? root.exitSearch() : root.enterSearch("")
                    }

                    TopIconBtn {
                        id: menuBtn
                        kind: "menu"
                        tip: "更多功能"
                        onClicked: {
                            const pt = menuBtn.mapToItem(keyRoot, menuBtn.width - 186, menuBtn.height + 2)
                            mainMenu.popup(keyRoot, pt.x, pt.y)
                        }
                    }

                    TopIconBtn {
                        kind: "close"
                        isClose: true
                        tip: controller.standaloneMode ? "退出" : "隐藏面板 (Esc)"
                        onClicked: controller.standaloneMode ? root.close() : controller.hidePopup()
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
            Rectangle {
                id: searchBar
                visible: root.searchActive
                Layout.fillWidth: true
                height: 44
                color: theme.layer

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 14
                    spacing: 10

                    VectorGlyph {
                        width: 15
                        height: 15
                        kind: "search"
                        color: theme.subtext
                    }

                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.text
                        placeholderText: "搜索程序名称、拼音首字母、路径或备注..."
                        placeholderTextColor: theme.faint
                        selectedTextColor: theme.accentText
                        selectionColor: theme.accentFill
                        font.pixelSize: 14
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
                        id: clearSearchBtn
                        visible: searchField.text.length > 0
                        implicitWidth: 26
                        implicitHeight: 26
                        padding: 0
                        hoverEnabled: true
                        onClicked: {
                            searchField.text = ""
                            searchField.forceActiveFocus()
                        }
                        contentItem: Item {
                            VectorGlyph {
                                anchors.centerIn: parent
                                width: 13
                                height: 13
                                kind: "clear"
                                color: clearSearchBtn.hovered ? theme.text : theme.subtext
                            }
                        }
                        background: Rectangle {
                            color: clearSearchBtn.hovered ? theme.hover : "transparent"
                            radius: Math.min(theme.controlRadius, 6)
                        }
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

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    id: sidebar
                    visible: !root.searchActive
                    Layout.preferredWidth: 138
                    Layout.fillHeight: true
                    color: theme.layer

                    HoverHandler {
                        id: sidebarHover
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        ListView {
                            id: categoryList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 8
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
                            Layout.bottomMargin: 4
                            spacing: 2
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: controller.categoryModel

                            delegate: Item {
                                width: categoryList.width
                                height: 36

                                readonly property bool isSelected: categoryId === controller.activeCategoryId

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Math.min(theme.controlRadius, 6)
                                    color: catDrop.containsDrag
                                        ? theme.withAlpha(theme.accent, 0.22)
                                        : (isSelected ? theme.withAlpha(theme.accent, 0.13)
                                            : (catMouse.containsMouse ? theme.hover : "transparent"))
                                    border.width: catDrop.containsDrag ? 1 : 0
                                    border.color: theme.accent
                                    Behavior on color { ColorAnimation { duration: 90 } }

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

                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        text: name
                                        color: isSelected ? theme.text : theme.subtext
                                        font.pixelSize: 13
                                        font.weight: isSelected ? Font.DemiBold : Font.Normal
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                }

                                DropArea {
                                    id: catDrop
                                    anchors.fill: parent
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
                                    onContainsMouseChanged: {
                                        if (containsMouse && root.draggingItemId === "") root.scheduleHoverTip(catMouse, name + " (" + count + ")", 480)
                                        else root.hideHoverTip(catMouse)
                                    }
                                    onPressed: root.hideHoverTip(catMouse)
                                    Component.onDestruction: root.hideHoverTip(catMouse)

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
                        Button {
                            id: addCatBtn
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
                            Layout.bottomMargin: 8
                            height: 30
                            padding: 0
                            hoverEnabled: true
                            opacity: (sidebarHover.hovered || addCatBtn.hovered) ? 1.0 : 0.0
                            enabled: opacity > 0.1
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            onClicked: categoryPrompt.openCreate()

                            contentItem: Item {
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 5
                                    VectorGlyph {
                                        width: 11
                                        height: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                        kind: "plus"
                                        color: addCatBtn.hovered ? theme.text : theme.subtext
                                    }
                                    Text {
                                        text: "新建分组"
                                        color: addCatBtn.hovered ? theme.text : theme.subtext
                                        font.pixelSize: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                            background: Rectangle {
                                color: addCatBtn.down ? theme.pressed : (addCatBtn.hovered ? theme.hover : "transparent")
                                radius: Math.min(theme.controlRadius, 6)
                            }
                        }
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        width: 1
                        color: theme.withAlpha(theme.divider, 0.7)
                    }
                }

                Rectangle {
                    id: contentPane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: theme.page
                    clip: true

                    MouseArea {
                        id: blankMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: (mouse) => {
                            if (!root.searchActive && mouse.button === Qt.RightButton) {
                                const pt = blankMouse.mapToItem(keyRoot, mouse.x, mouse.y)
                                mainMenu.popup(keyRoot, pt.x, pt.y)
                            }
                        }
                    }

                    ListView {
                        id: searchListView
                        visible: root.searchActive && count > 0
                        anchors.fill: parent
                        anchors.topMargin: 8
                        anchors.bottomMargin: 8
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 2
                        clip: true
                        cacheBuffer: 480
                        boundsBehavior: Flickable.StopAtBounds
                        flickDeceleration: 3500
                        maximumFlickVelocity: 2400
                        model: controller.itemModel
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }
                        delegate: Item {
                            id: sRowWrap
                            width: searchListView.width
                            height: 54

                            readonly property bool isCurrent: index === root.currentRow

                            Rectangle {
                                anchors.fill: parent
                                radius: Math.min(theme.controlRadius, 8)
                                color: sRowMouse.pressed
                                    ? theme.pressed
                                    : (isCurrent ? theme.withAlpha(theme.accent, 0.14)
                                        : (sRowMouse.containsMouse ? theme.hover : "transparent"))
                                Behavior on color { ColorAnimation { duration: 90 } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12

                                    Item {
                                        width: 32
                                        height: 32
                                        Image {
                                            id: sIconImg
                                            anchors.fill: parent
                                            source: iconUrl
                                            asynchronous: true
                                            mipmap: true
                                            smooth: true
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize: Qt.size(32, 32)
                                            visible: status === Image.Ready
                                        }
                                        Rectangle {
                                            anchors.fill: parent
                                            visible: sIconImg.status !== Image.Ready
                                            radius: Math.min(theme.controlRadius, 6)
                                            color: theme.withAlpha(theme.accentFill, 0.18)
                                            Text {
                                                anchors.centerIn: parent
                                                text: name.length > 0 ? name.slice(0, 1).toUpperCase() : "?"
                                                color: theme.accent
                                                font.pixelSize: 14
                                                font.weight: Font.Bold
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        RowLayout {
                                            id: sNameRow
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text {
                                                text: name
                                                color: theme.text
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                Layout.maximumWidth: Math.max(80, sNameRow.width - (runAsAdmin ? 24 : 0))
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
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Rectangle {
                                        visible: categoryName.length > 0
                                        implicitWidth: sCatTxt.implicitWidth + 12
                                        height: 20
                                        radius: Math.min(theme.controlRadius, 6)
                                        color: theme.withAlpha(theme.control, 0.85)
                                        Text {
                                            id: sCatTxt
                                            anchors.centerIn: parent
                                            text: categoryName
                                            color: theme.subtext
                                            font.pixelSize: 11
                                        }
                                    }

                                    Rectangle {
                                        visible: shortcutHint.length > 0
                                        implicitWidth: sHintTxt.implicitWidth + 10
                                        height: 20
                                        radius: Math.min(theme.controlRadius, 6)
                                        color: theme.control
                                        border.width: 1
                                        border.color: theme.withAlpha(theme.divider, 0.8)
                                        Text {
                                            id: sHintTxt
                                            anchors.centerIn: parent
                                            text: shortcutHint
                                            color: theme.faint
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                id: sRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onContainsMouseChanged: {
                                    if (containsMouse) root.scheduleHoverTip(sRowMouse, name + "\n" + target + (remark.length > 0 ? "\n" + remark : ""), 550)
                                    else root.hideHoverTip(sRowMouse)
                                }
                                onPressed: root.hideHoverTip(sRowMouse)
                                Component.onDestruction: root.hideHoverTip(sRowMouse)

                                onClicked: (mouse) => {
                                    root.selectRow(index)
                                    if (mouse.button === Qt.LeftButton && controller.singleClickLaunch) {
                                        controller.launchItem(itemId, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                    } else if (mouse.button === Qt.RightButton) {
                                        root.openItemContextMenu(itemId, categoryId, sRowWrap, mouse.x, mouse.y)
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

                    GridView {
                        id: gridView
                        visible: !root.searchActive && controller.viewMode === "grid" && count > 0
                        anchors.fill: parent
                        anchors.margins: 16
                        cellWidth: Math.floor(width / root.gridCols)
                        cellHeight: root.gridCellHeight
                        cacheBuffer: 480
                        boundsBehavior: Flickable.StopAtBounds
                        flickDeceleration: 3500
                        maximumFlickVelocity: 2400
                        clip: true
                        model: controller.itemModel
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }
                        delegate: Item {
                            id: gridCardWrap
                            width: gridView.cellWidth
                            height: gridView.cellHeight

                            readonly property bool isCurrent: index === root.currentRow
                            readonly property bool showFocus: root.keyboardNavActive && isCurrent

                            Rectangle {
                                id: cardBg
                                anchors.fill: parent
                                anchors.margins: 2
                                radius: Math.min(theme.cardRadius, 10)
                                color: cardMouse.pressed
                                    ? theme.pressed
                                    : (cardMouse.containsMouse ? theme.hover
                                        : (showFocus ? theme.withAlpha(theme.accent, 0.14) : "transparent"))
                                border.width: showFocus ? 1 : 0
                                border.color: theme.accent
                                scale: cardMouse.pressed ? 0.96 : 1.0
                                Behavior on color { ColorAnimation { duration: 90 } }
                                Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }

                                Rectangle {
                                    visible: runAsAdmin
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.margins: 4
                                    width: 15
                                    height: 14
                                    radius: theme.rounded ? 4 : 0
                                    color: theme.withAlpha(theme.danger, 0.16)
                                    border.width: 1
                                    border.color: theme.withAlpha(theme.danger, 0.5)
                                    Text {
                                        anchors.centerIn: parent
                                        text: "管"
                                        color: theme.danger
                                        font.pixelSize: 9
                                        font.weight: Font.Bold
                                    }
                                }

                                Rectangle {
                                    id: gridHintBadge
                                    visible: opacity > 0.01
                                    opacity: (root.ctrlHeld && shortcutHint.length > 0) ? 1.0 : 0.0
                                    Behavior on opacity { NumberAnimation { duration: 110 } }
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: 4
                                    implicitWidth: hintTxt.implicitWidth + 6
                                    height: 15
                                    radius: theme.rounded ? 4 : 0
                                    color: theme.withAlpha(theme.control, 0.92)
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

                                Column {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width - 10
                                    y: controller.tileSize === "small" ? 5 : (controller.tileSize === "large" ? 10 : 7)
                                    spacing: 7

                                    Item {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: root.gridIconSize
                                        height: root.gridIconSize

                                        Image {
                                            id: gridIconImg
                                            anchors.fill: parent
                                            source: iconUrl
                                            asynchronous: true
                                            mipmap: true
                                            smooth: true
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize: Qt.size(root.gridIconSize, root.gridIconSize)
                                            visible: status === Image.Ready
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            visible: gridIconImg.status !== Image.Ready
                                            radius: Math.min(theme.controlRadius, 8)
                                            color: theme.withAlpha(theme.accentFill, 0.18)
                                            border.width: 1
                                            border.color: theme.withAlpha(theme.accent, 0.3)
                                            Text {
                                                anchors.centerIn: parent
                                                text: name.length > 0 ? name.slice(0, 1).toUpperCase() : "?"
                                                color: theme.accent
                                                font.pixelSize: Math.round(root.gridIconSize * 0.46)
                                                font.weight: Font.Bold
                                            }
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: name
                                        color: theme.text
                                        font.pixelSize: 12
                                        lineHeight: 1.08
                                        horizontalAlignment: Text.AlignHCenter
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

                                onContainsMouseChanged: {
                                    if (containsMouse && root.draggingItemId === "") root.scheduleHoverTip(cardMouse, name + "\n" + target + (remark.length > 0 ? "\n" + remark : ""), 550)
                                    else root.hideHoverTip(cardMouse)
                                }
                                Component.onDestruction: root.hideHoverTip(cardMouse)

                                onPressed: (mouse) => {
                                    root.hideHoverTip(cardMouse)
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
                                    root.keyboardNavActive = !controller.singleClickLaunch && mouse.button === Qt.LeftButton
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
                        visible: !root.searchActive && controller.viewMode === "list" && count > 0
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 3
                        clip: true
                        cacheBuffer: 480
                        boundsBehavior: Flickable.StopAtBounds
                        flickDeceleration: 3500
                        maximumFlickVelocity: 2400
                        model: controller.itemModel
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }
                        delegate: Item {
                            id: listRowWrap
                            width: listView.width
                            height: 46

                            readonly property bool isCurrent: index === root.currentRow
                            readonly property bool showFocus: root.keyboardNavActive && isCurrent

                            Rectangle {
                                anchors.fill: parent
                                radius: Math.min(theme.controlRadius, 6)
                                color: rowMouse.pressed
                                    ? theme.pressed
                                    : (rowMouse.containsMouse ? theme.hover
                                        : (showFocus ? theme.withAlpha(theme.accent, 0.14) : "transparent"))
                                border.width: showFocus ? 1 : 0
                                border.color: theme.accent
                                Behavior on color { ColorAnimation { duration: 90 } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 12

                                    Item {
                                        width: 28
                                        height: 28
                                        Image {
                                            id: listIconImg
                                            anchors.fill: parent
                                            source: iconUrl
                                            asynchronous: true
                                            mipmap: true
                                            smooth: true
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize: Qt.size(28, 28)
                                            visible: status === Image.Ready
                                        }
                                        Rectangle {
                                            anchors.fill: parent
                                            visible: listIconImg.status !== Image.Ready
                                            radius: Math.min(theme.controlRadius, 6)
                                            color: theme.withAlpha(theme.accentFill, 0.18)
                                            Text {
                                                anchors.centerIn: parent
                                                text: name.length > 0 ? name.slice(0, 1).toUpperCase() : "?"
                                                color: theme.accent
                                                font.pixelSize: 13
                                                font.weight: Font.Bold
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        RowLayout {
                                            id: listNameRow
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text {
                                                text: name
                                                color: theme.text
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                Layout.maximumWidth: Math.max(80, listNameRow.width - (runAsAdmin ? 24 : 0))
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
                                        radius: Math.min(theme.controlRadius, 6)
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
                                        visible: opacity > 0.01
                                        opacity: (root.ctrlHeld && shortcutHint.length > 0) ? 1.0 : 0.0
                                        Behavior on opacity { NumberAnimation { duration: 110 } }
                                        implicitWidth: listHintTxt.implicitWidth + 8
                                        height: 18
                                        radius: Math.min(theme.controlRadius, 6)
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

                                onContainsMouseChanged: {
                                    if (containsMouse && root.draggingItemId === "") root.scheduleHoverTip(rowMouse, name + "\n" + target + (remark.length > 0 ? "\n" + remark : ""), 550)
                                    else root.hideHoverTip(rowMouse)
                                }
                                Component.onDestruction: root.hideHoverTip(rowMouse)

                                onPressed: (mouse) => {
                                    root.hideHoverTip(rowMouse)
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
                                    root.keyboardNavActive = !controller.singleClickLaunch && mouse.button === Qt.LeftButton
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
                        spacing: 12

                        VectorGlyph {
                            Layout.alignment: Qt.AlignHCenter
                            width: 32
                            height: 32
                            strokeWidth: 1.5
                            kind: root.searchActive ? "search" : "folder"
                            color: theme.faint
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.searchActive
                                ? (controller.searchText.length > 0
                                    ? "没有匹配的启动项，可换个关键词或拼音首字母"
                                    : "输入程序名称、拼音首字母、路径或备注以搜索")
                                : "当前分组暂无启动项\n直接拖入程序、快捷方式或文件夹即可添加"
                            color: theme.subtext
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            visible: !root.searchActive && controller.searchText.length === 0

                            Button {
                                id: emptyAddBtn
                                text: "+ 添加启动项"
                                implicitHeight: 30
                                leftPadding: 14
                                rightPadding: 14
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
                                    radius: Math.min(theme.controlRadius, 6)
                                }
                            }

                            Button {
                                id: emptyImportBtn
                                visible: controller.hasDefaultMayeFile
                                text: "导入 MayeNano"
                                implicitHeight: 30
                                leftPadding: 14
                                rightPadding: 14
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
                                    radius: Math.min(theme.controlRadius, 6)
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
                            drag.accepted = !root.searchActive && drag.hasUrls
                        }
                        onDropped: (drop) => {
                            if (!root.searchActive && drop.hasUrls) {
                                controller.addDroppedUrls(drop.urls, controller.activeCategoryId)
                            }
                        }
                    }

                    Rectangle {
                        visible: rightDrop.containsDrag
                        anchors.fill: parent
                        anchors.margins: 12
                        radius: Math.min(theme.cardRadius, 10)
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
        }

        Rectangle {
            id: hotkeyConflictBanner
            visible: !controller.hotkeyRegistered && controller.hotkeyKey.length > 0
            z: 40
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            width: Math.min(parent.width - 40, conflictRow.implicitWidth + 20)
            height: 30
            radius: 15
            color: theme.layer
            border.width: 1
            border.color: theme.withAlpha(theme.danger, 0.65)

            RowLayout {
                id: conflictRow
                anchors.centerIn: parent
                spacing: 8

                Text {
                    text: "热键冲突: " + controller.hotkeyKey
                    color: theme.danger
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }

                Rectangle {
                    implicitWidth: goSetTxt.implicitWidth + 12
                    height: 20
                    radius: 10
                    color: goSetMouse.containsMouse ? theme.withAlpha(theme.danger, 0.22) : theme.withAlpha(theme.danger, 0.12)
                    Text {
                        id: goSetTxt
                        anchors.centerIn: parent
                        text: "去设置"
                        color: theme.danger
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        id: goSetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsDialog.openSettings()
                    }
                }
            }
        }

        Rectangle {
            id: statusToast
            readonly property bool hasToast: controller.statusMessage.length > 0
                && !(hotkeyConflictBanner.visible && controller.statusMessage === controller.hotkeyErrorText)
            visible: opacity > 0.01
            opacity: hasToast ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            z: 41
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: hotkeyConflictBanner.visible ? hotkeyConflictBanner.top : parent.bottom
            anchors.bottomMargin: hotkeyConflictBanner.visible ? 8 : 14
            width: Math.min(parent.width - 48, toastRow.implicitWidth + 24)
            height: 32
            radius: 16
            color: controller.statusError ? theme.layer : theme.menuBackground
            border.width: 1
            border.color: controller.statusError ? theme.withAlpha(theme.danger, 0.7) : theme.controlBorder

            RowLayout {
                id: toastRow
                anchors.centerIn: parent
                spacing: 8
                width: Math.min(implicitWidth, keyRoot.width - 72)

                Text {
                    Layout.fillWidth: true
                    text: controller.statusMessage
                    color: controller.statusError ? theme.danger : theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }

                VectorGlyph {
                    visible: controller.statusError
                    width: 10
                    height: 10
                    kind: "close"
                    color: theme.danger
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: controller.statusError ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: controller.clearStatus()
            }
        }

        MouseArea { z: 50; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.leftMargin: 6; anchors.rightMargin: 6; height: 5; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.TopEdge) }
        MouseArea { z: 50; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.leftMargin: 6; anchors.rightMargin: 6; height: 5; cursorShape: Qt.SizeVerCursor; onPressed: root.startSystemResize(Qt.BottomEdge) }
        MouseArea { z: 50; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 6; anchors.bottomMargin: 6; width: 5; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.LeftEdge) }
        MouseArea { z: 50; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 6; anchors.bottomMargin: 6; width: 5; cursorShape: Qt.SizeHorCursor; onPressed: root.startSystemResize(Qt.RightEdge) }
        MouseArea { z: 50; anchors.left: parent.left; anchors.top: parent.top; width: 7; height: 7; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.LeftEdge | Qt.TopEdge) }
        MouseArea { z: 50; anchors.right: parent.right; anchors.top: parent.top; width: 7; height: 7; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.RightEdge | Qt.TopEdge) }
        MouseArea { z: 50; anchors.left: parent.left; anchors.bottom: parent.bottom; width: 7; height: 7; cursorShape: Qt.SizeBDiagCursor; onPressed: root.startSystemResize(Qt.LeftEdge | Qt.BottomEdge) }
        MouseArea { z: 50; anchors.right: parent.right; anchors.bottom: parent.bottom; width: 7; height: 7; cursorShape: Qt.SizeFDiagCursor; onPressed: root.startSystemResize(Qt.RightEdge | Qt.BottomEdge) }

        Rectangle {
            id: dragProxy
            visible: root.draggingItemId.length > 0
            width: Math.min(180, dragLabel.implicitWidth + 20)
            height: 28
            radius: Math.min(theme.controlRadius, 6)
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
        id: mainMenu
        padding: 4
        background: Rectangle {
            implicitWidth: 192
            color: theme.menuBackground
            radius: Math.min(theme.controlRadius, 8)
            border.width: 1
            border.color: theme.controlBorder
        }

        StyledMenuItem { text: "添加启动项 (Ctrl+N)"; onTriggered: itemEditDialog.openNew(controller.activeCategoryId) }
        StyledMenuItem { text: "新建分组"; onTriggered: categoryPrompt.openCreate() }
        StyledMenuItem {
            text: controller.hasDefaultMayeFile ? "一键导入 MayeNano (MDB.json)" : "导入 MayeNano (选择文件)..."
            onTriggered: controller.hasDefaultMayeFile ? controller.importMayeDefault() : controller.importMayeFromDialog()
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: theme.divider } }
        StyledMenuItem {
            text: controller.viewMode === "grid" ? "切换为列表视图" : "切换为图标网格"
            onTriggered: controller.viewMode = (controller.viewMode === "grid" ? "list" : "grid")
        }
        Menu {
            title: "图标尺寸"
            padding: 4
            background: Rectangle {
                implicitWidth: 148
                color: theme.menuBackground
                radius: Math.min(theme.controlRadius, 8)
                border.width: 1
                border.color: theme.controlBorder
            }
            StyledMenuItem { text: (controller.tileSize === "small" ? "● " : "   ") + "小图标 (32px)"; onTriggered: controller.tileSize = "small" }
            StyledMenuItem { text: (controller.tileSize === "medium" ? "● " : "   ") + "中等图标 (42px)"; onTriggered: controller.tileSize = "medium" }
            StyledMenuItem { text: (controller.tileSize === "large" ? "● " : "   ") + "大图标 (50px)"; onTriggered: controller.tileSize = "large" }
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: theme.divider } }
        StyledMenuItem { text: "设置 (Ctrl+,)"; onTriggered: settingsDialog.openSettings() }
    }

    Menu {
        id: itemMenu
        padding: 4
        background: Rectangle {
            implicitWidth: 168
            color: theme.menuBackground
            radius: Math.min(theme.controlRadius, 8)
            border.width: 1
            border.color: theme.controlBorder
        }

        StyledMenuItem { text: "启动"; onTriggered: controller.launchItem(root.menuItemId, false) }
        StyledMenuItem { text: "以管理员身份运行"; onTriggered: controller.launchItem(root.menuItemId, true) }
        StyledMenuItem { text: "打开所在文件夹"; onTriggered: controller.revealItem(root.menuItemId) }
        StyledMenuItem { text: "复制目标路径"; onTriggered: controller.copyItemTarget(root.menuItemId) }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: theme.divider } }
        StyledMenuItem { text: "编辑..."; onTriggered: itemEditDialog.openEdit(root.menuItemId) }
        Menu {
            id: moveSubMenu
            title: "移动到分组"
            padding: 4
            background: Rectangle {
                implicitWidth: 148
                color: theme.menuBackground
                radius: Math.min(theme.controlRadius, 8)
                border.width: 1
                border.color: theme.controlBorder
            }
        }
        StyledMenuItem { text: "前移"; onTriggered: controller.moveItemOrder(root.menuItemId, -1) }
        StyledMenuItem { text: "后移"; onTriggered: controller.moveItemOrder(root.menuItemId, 1) }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: theme.divider } }
        StyledMenuItem { text: "删除"; onTriggered: controller.deleteItem(root.menuItemId) }
    }

    Menu {
        id: categoryMenu
        padding: 4
        background: Rectangle {
            implicitWidth: 148
            color: theme.menuBackground
            radius: Math.min(theme.controlRadius, 8)
            border.width: 1
            border.color: theme.controlBorder
        }

        StyledMenuItem { text: "重命名分组..."; onTriggered: categoryPrompt.openRename(root.menuCatId, root.menuCatName) }
        StyledMenuItem { text: "上移分组"; onTriggered: controller.moveCategoryOrder(root.menuCatId, -1) }
        StyledMenuItem { text: "下移分组"; onTriggered: controller.moveCategoryOrder(root.menuCatId, 1) }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: theme.divider } }
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
            if (name.length === 0) return
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
            radius: Math.min(theme.cardRadius, 10)
            border.width: 1
            border.color: theme.windowEdge
        }

        contentItem: ColumnLayout {
            spacing: 12
            Item { height: 2 }
            Text {
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                text: categoryPrompt.isRename ? "重命名分组" : "新建分组"
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            TextField {
                id: catInput
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                implicitHeight: 34
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
                    radius: Math.min(theme.controlRadius, 6)
                    border.width: 1
                    border.color: catInput.activeFocus ? theme.inputFocused : theme.controlBorder
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 16
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    id: catCancelBtn
                    text: "取消"
                    implicitWidth: 68
                    implicitHeight: 30
                    onClicked: categoryPrompt.close()
                    contentItem: Text {
                        text: catCancelBtn.text; color: theme.text; font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: catCancelBtn.down ? theme.pressed : (catCancelBtn.hovered ? theme.controlHover : theme.control)
                        radius: Math.min(theme.controlRadius, 6)
                        border.width: 1
                        border.color: theme.controlBorder
                    }
                }
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
                        radius: Math.min(theme.controlRadius, 6)
                    }
                }
            }
        }
    }

    Timer {
        id: hoverTipTimer
        repeat: false
        onTriggered: {
            if (!root.visible || !root.hoverTipTarget || root.hoverTipText.length === 0 || root.draggingItemId.length > 0) {
                sharedToolTip.visible = false
                return
            }
            const target = root.hoverTipTarget
            const pos = target.mapToItem(root.contentItem, target.width / 2, target.height + 6)
            sharedToolTip.text = root.hoverTipText
            const tipW = Math.max(40, sharedToolTip.implicitWidth)
            const tipH = Math.max(24, sharedToolTip.implicitHeight)
            const clampedX = Math.max(8, Math.min(pos.x - tipW / 2, root.width - tipW - 8))
            let clampedY = pos.y
            if (clampedY + tipH > root.height - 8) {
                const above = target.mapToItem(root.contentItem, 0, -tipH - 6).y
                clampedY = Math.max(8, above)
            }
            sharedToolTip.x = Math.round(clampedX)
            sharedToolTip.y = Math.round(clampedY)
            sharedToolTip.visible = true
        }
    }

    ToolTip {
        id: sharedToolTip
        parent: root.contentItem
        visible: false
        padding: 8
        font.pixelSize: 12
        contentItem: Text {
            text: sharedToolTip.text
            font: sharedToolTip.font
            color: theme.text
            maximumLineCount: 4
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Math.min(theme.controlRadius, 6)
            color: theme.menuBackground
            border.width: 1
            border.color: theme.windowEdge
        }
    }

    ItemEditDialog { id: itemEditDialog }
    SettingsDialog { id: settingsDialog }
}
