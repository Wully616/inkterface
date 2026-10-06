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
    property int panelWidth: lcd5b ? 1024 : 648
    property int panelHeight: lcd5b ? 600 : 480
    property var typeModel: dashboard.widgetTypes()
    property var profileModel: [
        { name: "normal", label: "Normal" },
        { name: "inGame", label: "In Game" },
        { name: "idle", label: "Idle" }
    ]

    function refresh() {
        widgetModel = dashboard.widgets(editorProfile, lcd5b);
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Label { text: "Profile" }
            ComboBox {
                id: profileBox
                textRole: "label"
                valueRole: "name"
                model: control.profileModel
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
                Layout.preferredWidth: 200
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

                Rectangle {
                    id: preview
                    width: Math.min(parent.width, parent.height * control.panelWidth / control.panelHeight)
                    height: width * control.panelHeight / control.panelWidth
                    anchors.centerIn: parent
                    color: "#171a1d"
                    border.color: "#7a838c"
                    border.width: 1
                    clip: true

                    Repeater {
                        model: control.widgetModel
                        delegate: Rectangle {
                            id: widgetRect
                            required property var modelData
                            x: control.clamp(modelData.x, 0, 1) * preview.width
                            y: control.clamp(modelData.y, 0, 1) * preview.height
                            width: control.clamp(modelData.width, 0, 1 - modelData.x) * preview.width
                            height: control.clamp(modelData.height, 0, 1 - modelData.y) * preview.height
                            color: modelData.id === control.selectedId ? "#344b60" : "#293139"
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
                                    text: modelData.label || modelData.type
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

            ScrollView {
                Layout.fillHeight: true
                Layout.preferredWidth: 310
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    Label { text: "Selected widget"; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: control.selectedWidget.id ? (control.selectedWidget.label || control.selectedWidget.type) : "Select a widget in preview"
                    }
                    Label { text: "Metric collector"; visible: control.isMetric(control.selectedWidget) }
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
                    Label { text: "Box-art fit"; visible: control.isBoxArt(control.selectedWidget) }
                    ComboBox {
                        id: fitBox
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
                        onEditingFinished: dashboard.setWidgetSetting(control.editorProfile, control.lcd5b,
                                                                        control.selectedId, "text", text)
                    }

                    Label { text: "Rules"; font.bold: true; topPadding: 8 }
                    Label { text: "Game running" }
                    RowLayout {
                        CheckBox {
                            text: "Enabled"
                            checked: !!control.gameRule.enabled
                            onToggled: control.saveRuleEnabled("gameRunning", checked)
                        }
                        Label { text: "Priority" }
                        SpinBox {
                            from: 0
                            to: 100
                            value: Number(control.gameRule.priority || 0)
                            onValueModified: control.saveRulePriority("gameRunning", value)
                        }
                    }
                    RowLayout {
                        Label { text: "Target layout" }
                        ComboBox {
                            Layout.fillWidth: true
                            textRole: "label"
                            valueRole: "name"
                            model: control.profileModel
                            currentIndex: Math.max(0, control.profileModel.findIndex(
                                                       profile => profile.name === control.gameRule.profile))
                            onActivated: control.saveRuleProfile("gameRunning", currentValue)
                        }
                    }
                    Label { text: "Idle" }
                    RowLayout {
                        CheckBox {
                            text: "Enabled"
                            checked: !!control.idleRule.enabled
                            onToggled: control.saveRuleEnabled("idle", checked)
                        }
                        Label { text: "Priority" }
                        SpinBox {
                            from: 0
                            to: 100
                            value: Number(control.idleRule.priority || 0)
                            onValueModified: control.saveRulePriority("idle", value)
                        }
                    }
                    RowLayout {
                        Label { text: "Target layout" }
                        ComboBox {
                            Layout.fillWidth: true
                            textRole: "label"
                            valueRole: "name"
                            model: control.profileModel
                            currentIndex: Math.max(0, control.profileModel.findIndex(
                                                       profile => profile.name === control.idleRule.profile))
                            onActivated: control.saveRuleProfile("idle", currentValue)
                        }
                    }
                    Label { text: "Idle timeout (seconds)" }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 30
                        to: 86400
                        value: dashboard.idleTimeoutSeconds
                        onValueModified: dashboard.setIdleTimeoutSeconds(value)
                    }
                    CheckBox {
                        text: "Turn off backlight on idle"
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
        }
    }
}
