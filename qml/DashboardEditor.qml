import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: control
    property bool lcd5b: false

    property string editorProfile: "normal"
    property var widgetModel: []
    property string selectedId: ""
    property var selectedWidget: ({})
    property var backgroundSettings: ({})
    property int panelWidth: lcd5b ? 1024 : 648
    property int panelHeight: lcd5b ? 600 : 480
    property var typeModel: dashboard.widgetTypes()
    property var profileModel: [
        { name: "normal", label: "Normal dashboard" },
        { name: "inGame", label: "In-game layout" },
        { name: "idle", label: "Idle screensaver" }
    ]
    property var paletteModel: [
        { name: "system", label: "System" },
        { name: "ocean", label: "Ocean" },
        { name: "sunset", label: "Sunset" },
        { name: "forest", label: "Forest" },
        { name: "mono", label: "Monochrome" }
    ]
    property var backgroundModes: [
        { name: "color", label: "Solid color" },
        { name: "pattern", label: "Pattern" },
        { name: "image", label: "Image" }
    ]
    property var patternModel: [
        { name: "dots", label: "Dots" },
        { name: "grid", label: "Grid" },
        { name: "stripes", label: "Diagonal stripes" }
    ]

    function refresh() {
        widgetModel = dashboard.widgets(editorProfile, lcd5b);
        backgroundSettings = dashboard.background(editorProfile, lcd5b);
        selectedWidget = widgetModel.find(widget => widget.id === selectedId) || ({});
        if (!selectedWidget.id)
            selectedId = "";
    }

    function isMetric(widget) {
        return widget.id && (widget.type.toLowerCase().includes("metric")
                             || (widget.settings && "collector" in widget.settings));
    }

    function isBoxArt(widget) {
        return widget.id && (widget.type.toLowerCase().includes("art")
                             || (widget.settings && "fit" in widget.settings));
    }

    function isScreensaver(widget) {
        return widget.id && (widget.type.toLowerCase().includes("screen")
                             || (widget.settings && "text" in widget.settings));
    }

    function widgetTitle(widget, detailed) {
        if (isMetric(widget)) {
            var collector = widget.settings ? widget.settings.collector || "" : "";
            if (collector.length > 0)
                return detailed ? "System metric · " + collector : collector;
            return "System metric";
        }
        return widget.id ? (widget.label || widget.type) : "Select a widget in the preview";
    }

    function widgetPreviewColor(widget) {
        const palette = widget.settings ? widget.settings.palette || "system" : "system";
        if (palette === "ocean") return "#193747";
        if (palette === "sunset") return "#493043";
        if (palette === "forest") return "#1f382a";
        if (palette === "mono") return "#252525";
        return "#293139";
    }

    function modelIndex(items, name) {
        for (let i = 0; i < items.length; ++i)
            if (items[i].name === name) return i;
        return -1;
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    function saveBounds(widget, x, y, width, height) {
        x = clamp(x, 0, 1);
        y = clamp(y, 0, 1);
        width = clamp(width, 0, 1 - x);
        height = clamp(height, 0, 1 - y);
        dashboard.setWidgetBounds(editorProfile, lcd5b, widget.id, x, y, width, height);
    }

    function selectWidget(widget) {
        selectedId = widget.id;
        selectedWidget = widget;
    }

    function saveRuleEnabled(condition, enabled) {
        dashboard.setRuleEnabled(condition, enabled);
        refreshRules();
    }

    function saveRulePriority(condition, priority) {
        dashboard.setRulePriority(condition, priority);
        refreshRules();
    }
    function saveRuleProfile(condition, profile) {
        dashboard.setRuleProfile(condition, profile);
        refreshRules();
    }

    function refreshRules() {
        gameRule = dashboard.rule("gameRunning");
        idleRule = dashboard.rule("idle");
    }

    function layoutIndex(name) {
        return control.profileModel.findIndex(layout => layout.name === name);
    }

    property var gameRule: ({})
    property var idleRule: ({})

    Component.onCompleted: {
        editorProfile = dashboard.activeProfile;
        refresh();
        refreshRules();
    }

    Connections {
        target: dashboard
        function onRevisionChanged() { control.refresh(); control.refreshRules(); }
    }

    onLcd5bChanged: refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Label { text: "Edit layout" }
            ComboBox {
                id: profileBox
                textRole: "label"
                valueRole: "name"
                model: control.profileModel
                Layout.minimumWidth: 140
                currentIndex: Math.max(0, indexOfValue(control.editorProfile))
                onActivated: {
                    control.editorProfile = currentValue;
                    control.refresh();
                }
                function indexOfValue(value) {
                    for (let i = 0; i < count; ++i)
                        if (valueAt(i) === value) return i;
                    return -1;
                }
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: typeBox
                Layout.preferredWidth: 190
                Layout.minimumWidth: 130
                textRole: "label"
                valueRole: "type"
                model: control.typeModel
            }
            Button {
                text: "Add widget"
                enabled: typeBox.currentIndex >= 0
                onClicked: {
                    dashboard.addWidget(control.editorProfile, control.lcd5b, typeBox.currentValue);
                    control.refresh();
                }
            }
        }

        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 12

            Item {
                id: previewArea
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumWidth: 280

                Rectangle {
                    id: preview
                    width: Math.min(parent.width, parent.height * control.panelWidth / control.panelHeight)
                    height: width * control.panelHeight / control.panelWidth
                    anchors.centerIn: parent
                    color: control.backgroundSettings.color || (control.lcd5b ? "#101722" : "#ffffff")
                    border.color: "#7a838c"
                    border.width: 1
                    clip: true

                    Image {
                        anchors.fill: parent
                        visible: control.backgroundSettings.mode === "image"
                                 && !!control.backgroundSettings.imagePath
                        source: visible ? control.backgroundSettings.imageUrl : ""
                        fillMode: Image.PreserveAspectCrop
                    }

                    Canvas {
                        id: patternPreview
                        anchors.fill: parent
                        visible: control.backgroundSettings.mode === "pattern"
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onVisibleChanged: if (visible) requestPaint()
                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.strokeStyle = control.backgroundSettings.patternColor || "#aaaaaa";
                            ctx.fillStyle = ctx.strokeStyle;
                            ctx.lineWidth = 1;
                            const pattern = control.backgroundSettings.pattern || "dots";
                            if (pattern === "dots") {
                                for (let y = 12; y < height; y += 32) {
                                    for (let x = 12; x < width; x += 32) {
                                        ctx.beginPath();
                                        ctx.arc(x, y, 1.5, 0, 2 * Math.PI);
                                        ctx.fill();
                                    }
                                }
                            } else if (pattern === "grid") {
                                ctx.beginPath();
                                for (let x = 0; x < width; x += 32) {
                                    ctx.moveTo(x, 0);
                                    ctx.lineTo(x, height);
                                }
                                for (let y = 0; y < height; y += 32) {
                                    ctx.moveTo(0, y);
                                    ctx.lineTo(width, y);
                                }
                                ctx.stroke();
                            } else if (pattern === "stripes") {
                                ctx.beginPath();
                                for (let x = -height; x < width; x += 32) {
                                    ctx.moveTo(x, 0);
                                    ctx.lineTo(x + height, height);
                                }
                                ctx.stroke();
                            }
                        }
                    }
                    Connections {
                        target: control
                        function onBackgroundSettingsChanged() { patternPreview.requestPaint(); }
                    }

                    Repeater {
                        model: control.widgetModel
                        delegate: Rectangle {
                            id: widgetRect
                            required property var modelData
                            x: control.clamp(modelData.x, 0, 1) * preview.width
                            y: control.clamp(modelData.y, 0, 1) * preview.height
                            width: control.clamp(modelData.width, 0, 1 - modelData.x) * preview.width
                            height: control.clamp(modelData.height, 0, 1 - modelData.y) * preview.height
                            color: control.widgetPreviewColor(modelData)
                            border.color: modelData.id === control.selectedId ? "#70c4ff" : "#77818a"
                            border.width: 2
                            clip: true

                            RowLayout {
                                id: widgetHeader
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                height: 32
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: control.widgetTitle(modelData, false)
                                    color: "white"
                                    MouseArea {
                                        anchors.fill: parent
                                        property point originPoint
                                        property real originX
                                        property real originY
                                        onPressed: {
                                            control.selectWidget(modelData);
                                            originPoint = mapToItem(preview, mouse.x, mouse.y);
                                            originX = widgetRect.x;
                                            originY = widgetRect.y;
                                        }
                                        onPositionChanged: if (pressed) {
                                            var point = mapToItem(preview, mouse.x, mouse.y);
                                            widgetRect.x = control.clamp(originX + point.x - originPoint.x, 0,
                                                                         preview.width - widgetRect.width);
                                            widgetRect.y = control.clamp(originY + point.y - originPoint.y, 0,
                                                                         preview.height - widgetRect.height);
                                        }
                                        onReleased: control.saveBounds(modelData, widgetRect.x / preview.width,
                                                                       widgetRect.y / preview.height,
                                                                       widgetRect.width / preview.width,
                                                                       widgetRect.height / preview.height)
                                    }
                                }
                                ToolButton {
                                    text: "×"
                                    onClicked: {
                                        dashboard.removeWidget(control.editorProfile, control.lcd5b, modelData.id);
                                        if (control.selectedId === modelData.id)
                                            control.selectedId = "";
                                        control.refresh();
                                    }
                                }
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                color: "#70c4ff"
                                MouseArea {
                                    anchors.fill: parent
                                    property real originWidth
                                    property real originHeight
                                    property point originPoint
                                    onPressed: {
                                        control.selectWidget(modelData);
                                        originWidth = widgetRect.width;
                                        originHeight = widgetRect.height;
                                        originPoint = mapToItem(preview, mouse.x, mouse.y);
                                    }
                                    onPositionChanged: if (pressed) {
                                        var point = mapToItem(preview, mouse.x, mouse.y);
                                        widgetRect.width = control.clamp(originWidth + point.x - originPoint.x,
                                                                         preview.width * 0.08,
                                                                         preview.width - widgetRect.x);
                                        widgetRect.height = control.clamp(originHeight + point.y - originPoint.y,
                                                                          preview.height * 0.08,
                                                                          preview.height - widgetRect.y);
                                    }
                                    onReleased: control.saveBounds(modelData, widgetRect.x / preview.width,
                                                                   widgetRect.y / preview.height,
                                                                   widgetRect.width / preview.width,
                                                                   widgetRect.height / preview.height)
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                id: inspector
                Layout.fillHeight: true
                Layout.minimumWidth: 300
                Layout.preferredWidth: 360
                Layout.maximumWidth: 410
                Layout.fillWidth: false
                spacing: 8

                TabBar {
                    id: inspectorTabs
                    Layout.fillWidth: true

                    TabButton { text: "Widget"; font.pixelSize: 18 }
                    TabButton { text: "Automation"; font.pixelSize: 18 }
                    TabButton { text: "Appearance"; font.pixelSize: 18 }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: inspectorTabs.currentIndex

                    ScrollView {
                        id: widgetSettingsScroll
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: widgetSettingsScroll.availableWidth
                            spacing: 10

                            Label { text: "Widget settings"; font.bold: true }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: control.widgetTitle(control.selectedWidget, true)
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: control.selectedWidget.id
                                      ? "Settings apply to the selected widget."
                                      : "Select a widget in the preview to edit its settings."
                            }

                            Label { text: "Widget palette"; visible: !!control.selectedWidget.id }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: !!control.selectedWidget.id
                                textRole: "label"
                                valueRole: "name"
                                model: control.paletteModel
                                currentIndex: {
                                    const palette = control.selectedWidget.settings
                                            ? control.selectedWidget.settings.palette || "system" : "system";
                                    return Math.max(0, control.paletteModel.findIndex(
                                                        item => item.name === palette));
                                }
                                onActivated: dashboard.setWidgetSetting(control.editorProfile,
                                                                         control.lcd5b,
                                                                         control.selectedId,
                                                                         "palette", currentValue)
                            }

                            Label { text: "Show metric"; visible: control.isMetric(control.selectedWidget) }
                            ComboBox {
                                id: collectorBox
                                Layout.fillWidth: true
                                visible: control.isMetric(control.selectedWidget)
                                model: panelState.collectors
                                textRole: "displayName"
                                currentIndex: Math.max(0, indexOfValue(control.selectedWidget.settings ? control.selectedWidget.settings.collector : ""))
                                onActivated: dashboard.setWidgetSetting(control.editorProfile, control.lcd5b,
                                                                          control.selectedId, "collector", currentText)
                                function indexOfValue(value) {
                                    for (let i = 0; i < count; ++i)
                                        if (textAt(i) === value) return i;
                                    return -1;
                                }
                            }
                            Label { text: "Fit box art"; visible: control.isBoxArt(control.selectedWidget) }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: control.isBoxArt(control.selectedWidget)
                                model: ["contain", "cover", "stretch"]
                                currentIndex: Math.max(0, model.indexOf(control.selectedWidget.settings ? control.selectedWidget.settings.fit : "contain"))
                                onActivated: dashboard.setWidgetSetting(control.editorProfile, control.lcd5b,
                                                                          control.selectedId, "fit", currentText)
                            }
                            Label { text: "Screensaver text"; visible: control.isScreensaver(control.selectedWidget) }
                            TextField {
                                Layout.fillWidth: true
                                visible: control.isScreensaver(control.selectedWidget)
                                text: control.selectedWidget.settings ? control.selectedWidget.settings.text || "" : ""
                                onEditingFinished: dashboard.setWidgetSetting(control.editorProfile,
                                                                                control.lcd5b,
                                                                                control.selectedId,
                                                                                "text", text)
                            }
                        }
                    }

                    ScrollView {
                        id: automationScroll
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: automationScroll.availableWidth
                            spacing: 10

                            Label { text: "Panel automation"; font.bold: true }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: "These rules apply to the whole panel. They choose which layout is shown; widgets stay in the layouts where you placed them."
                            }

                            Label { text: "When a game is running"; font.bold: true; topPadding: 4 }
                            CheckBox {
                                text: "Switch layouts automatically"
                                checked: !!control.gameRule.enabled
                                onToggled: control.saveRuleEnabled("gameRunning", checked)
                            }
                            Label { text: "Show this layout" }
                            ComboBox {
                                Layout.fillWidth: true
                                textRole: "label"
                                valueRole: "name"
                                model: control.profileModel
                                currentIndex: control.layoutIndex(control.gameRule.profile)
                                onActivated: control.saveRuleProfile("gameRunning", currentValue)
                            }
                            Label { text: "Priority if rules overlap" }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: Number(control.gameRule.priority || 0)
                                onValueModified: control.saveRulePriority("gameRunning", value)
                            }

                            Label { text: "When idle"; font.bold: true; topPadding: 4 }
                            CheckBox {
                                text: "Switch layouts automatically"
                                checked: !!control.idleRule.enabled
                                onToggled: control.saveRuleEnabled("idle", checked)
                            }
                            Label { text: "Show this layout" }
                            ComboBox {
                                Layout.fillWidth: true
                                textRole: "label"
                                valueRole: "name"
                                model: control.profileModel
                                currentIndex: control.layoutIndex(control.idleRule.profile)
                                onActivated: control.saveRuleProfile("idle", currentValue)
                            }
                            Label { text: "Priority if rules overlap" }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: Number(control.idleRule.priority || 0)
                                onValueModified: control.saveRulePriority("idle", value)
                            }

                            Label { text: "Enter idle layout after (seconds)" }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 30
                                to: 86400
                                value: dashboard.idleTimeoutSeconds
                                onValueModified: dashboard.setIdleTimeoutSeconds(value)
                            }
                            CheckBox {
                                text: "Turn off LCD backlight while idle"
                                visible: control.lcd5b
                                checked: dashboard.backlightOffOnIdle
                                onToggled: dashboard.setBacklightOffOnIdle(checked)
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: "Activity: " + dashboard.activityBackend
                            }
                        }
                    }

                    ScrollView {
                        id: appearanceScroll
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: appearanceScroll.availableWidth
                            spacing: 10

                            Label { text: "Layout appearance"; font.bold: true }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: "Background settings apply to the selected layout and screen."
                            }
                            Label { text: "Background style" }
                            ComboBox {
                                Layout.fillWidth: true
                                textRole: "label"
                                valueRole: "name"
                                model: control.backgroundModes
                                currentIndex: Math.max(0, control.modelIndex(
                                                    control.backgroundModes,
                                                    control.backgroundSettings.mode || "color"))
                                onActivated: dashboard.setBackground(control.editorProfile,
                                                                     control.lcd5b,
                                                                     "mode", currentValue)
                            }
                            Label { text: "Background color"; visible: control.backgroundSettings.mode !== "image" }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: control.backgroundSettings.mode !== "image"
                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 30
                                    color: control.backgroundSettings.color || "#ffffff"
                                    border.color: "#77818a"
                                }
                                Button {
                                    Layout.fillWidth: true
                                    text: "Choose color"
                                    onClicked: {
                                        const selected = dashboard.chooseColor(control.backgroundSettings.color || "#ffffff");
                                        if (selected.length > 0)
                                            dashboard.setBackground(control.editorProfile, control.lcd5b,
                                                                     "color", selected);
                                    }
                                }
                            }
                            Label { text: "Pattern"; visible: control.backgroundSettings.mode === "pattern" }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: control.backgroundSettings.mode === "pattern"
                                model: control.patternModel
                                textRole: "label"
                                valueRole: "name"
                                currentIndex: Math.max(0, control.modelIndex(
                                                    control.patternModel,
                                                    control.backgroundSettings.pattern || "dots"))
                                onActivated: dashboard.setBackground(control.editorProfile,
                                                                     control.lcd5b,
                                                                     "pattern", currentValue)
                            }
                            Label { text: "Pattern color"; visible: control.backgroundSettings.mode === "pattern" }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: control.backgroundSettings.mode === "pattern"
                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 30
                                    color: control.backgroundSettings.patternColor || "#aaaaaa"
                                    border.color: "#77818a"
                                }
                                Button {
                                    Layout.fillWidth: true
                                    text: "Choose pattern color"
                                    onClicked: {
                                        const selected = dashboard.chooseColor(control.backgroundSettings.patternColor || "#aaaaaa");
                                        if (selected.length > 0)
                                            dashboard.setBackground(control.editorProfile, control.lcd5b,
                                                                     "patternColor", selected);
                                    }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: control.backgroundSettings.mode === "image"
                                elide: Text.ElideMiddle
                                text: {
                                    const path = control.backgroundSettings.imagePath || "";
                                    return path.length ? path.split(/[\\/]/).pop() : "No background image selected";
                                }
                            }
                            Button {
                                Layout.fillWidth: true
                                visible: control.backgroundSettings.mode === "image"
                                text: "Choose background image…"
                                onClicked: {
                                    const path = dashboard.chooseBackgroundImage(control.lcd5b);
                                    if (path.length > 0)
                                        dashboard.setBackgroundImage(control.editorProfile,
                                                                     control.lcd5b, path);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
