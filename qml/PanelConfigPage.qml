import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import vqt

Item {
    id: control

    property PanelInfo panel: panelFinder.panel

    signal error(var message)
    signal notification(var message)
    signal panelCleared

    Settings {
        id: settings

    }

    RowLayout {
        id: topRow

        anchors.left: parent.left
        anchors.margins: 10
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 10

        VLabel {
            id: nameLabel

            Layout.fillWidth: true
            elide: Label.ElideRight
            font.bold: true
            font.pixelSize: 36
            text: panel.name
        }

        VConfirmButton {
            normalText: "Clear Panel"

            onConfirmed: {
                settings.setValue("panelName", "");
                control.panelCleared();
            }
        }

        VConfirmButton {
            normalText: "Exit"

            onConfirmed: Qt.callLater(Qt.quit)
        }
    }


    ColumnLayout {
        id: lcdBacklightRow

        anchors.left: topRow.left
        anchors.right: topRow.right
        anchors.top: topRow.bottom
        anchors.topMargin: 10
        height: visible ? implicitHeight : 0
        spacing: 0
        visible: panel && panel.lcd5b

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            VLabel {
                text: "LCD Backlight"
            }

            Switch {
                id: backlightSwitch

                checked: panelState.lcdBacklightOn

                onToggled: panelState.lcdBacklightOn = checked
            }

            VLabel {
                text: backlightSwitch.checked ? "On" : "Off"
            }
        }
    }

    DashboardEditor {
        id: readoutGrid

        anchors.bottom: serviceRect.top
        anchors.bottomMargin: 10
        anchors.left: topRow.left
        anchors.right: topRow.right
        anchors.top: lcdBacklightRow.bottom
        anchors.topMargin: 10
        lcd5b: panel && panel.lcd5b
    }

    VBevelRect {
        id: serviceRect

        anchors.bottom: parent.bottom
        anchors.left: readoutGrid.left
        anchors.margins: 10
        anchors.right: parent.right
        anchors.rightMargin: -(lineWidth * 2)
        height: 72
        shape: 1
        style: svcMgr.isRunning ? 2 : 1

        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 15
            anchors.right: parent.right
            anchors.rightMargin: Math.abs(parent.anchors.rightMargin) + 10
            anchors.top: parent.top
            anchors.topMargin: 10
            spacing: 10

            VLabel {
                font.bold: true
                font.italic: true
                text: "Service:"
            }

            VLabel {
                Layout.fillWidth: true
                elide: Label.ElideRight
                text: {
                    if (svcMgr.isRunning) {
                        return "Running";
                    } else if (svcMgr.isInstalled) {
                        return "Installed, Not Running";
                    }
                    return "Not Installed";
                }
            }

            VButton {
                enabled: svcMgr.isInstalled
                text: svcMgr.isRunning ? "Restart" : "Start"

                onClicked: svcMgr.isRunning ? svcMgr.restartService() : svcMgr.startService()
            }

            VButton {
                text: svcMgr.isInstalled ? "Uninstall" : "Install"

                onClicked: svcMgr.isInstalled ? svcMgr.uninstallService() : svcMgr.installService()
            }

            VButton {
                text: "Box Art: " + (panelState.artworkEnabled ? "On" : "Off")

                onClicked: panelState.artworkEnabled = !panelState.artworkEnabled
            }
        }
    }

    Loader {
        id: overlayLoader

        anchors.fill: parent
    }

    Connections {
        function onFinished() {
            overlayLoader.sourceComponent = null;
        }

        target: overlayLoader.item
    }
}
