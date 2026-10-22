import QtQuick
import QtQuick.Controls as QQC
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

Item {
    id: dlgRoot
    property var service: null
    property bool dialogOpen: false
    property string dialogKind: ""
    property real anchorX: 0
    property real anchorY: 0

    function openDialog(kind) {
        dialogKind = kind
        dialogOpen = true
        if (service) {
            anchorX = service.cursor.x
            anchorY = service.cursor.y
        }
    }

    function closeDialog() {
        dialogOpen = false
        dialogKind = ""
    }

    // Layer shell overlay window (same pattern as Hud.qml)
    Variants {
        model: Quickshell.screens
        PanelWindow {
            id: window
            required property var modelData
            screen: modelData
            color: "transparent"
            anchors { top: true; bottom: true; left: true; right: true }
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.namespace: "macrohelp-dialog"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
            mask: Region {}
            visible: dlgRoot.dialogOpen && dlgRoot.service !== null

            // Mouse-leave-to-close: 700ms grace, 3px tolerance, 50ms poll
            Timer {
                id: leaveTimer
                interval: 50
                running: dlgRoot.dialogOpen
                repeat: true
                onRunningChanged: { if(running) graceEnd = Date.now() + 700 }
                onTriggered: {
                    if (!dlgRoot.dialogOpen) return
                    if (Date.now() < graceEnd) return
                    if (!dlgRoot.service) return
                    var dx = Math.abs(dlgRoot.service.cursor.x - dlgRoot.anchorX)
                    var dy = Math.abs(dlgRoot.service.cursor.y - dlgRoot.anchorY)
                    if (dx > 3 || dy > 3) dlgRoot.closeDialog()
                }
            }

            // Escape to close
            Shortcut {
                sequence: StandardKey.Cancel
                onActivated: dlgRoot.closeDialog()
            }

            // Dialog box - bottom center
            Rectangle {
                id: dialogBox
                width: Style.space(300)
                height: col.implicitHeight + Style.space(24)
                x: (window.width - width) / 2
                y: window.height - height - Style.space(60)
                radius: Style.cornerRadius
                color: Commons.Color.background
                opacity: 0.93

                Column {
                    id: col
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Style.space(12)
                    spacing: Style.space(8)
                    width: parent.width - Style.space(24)

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: dlgRoot.dialogKind
                        color: Commons.Color.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                        font.bold: true
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "X: " + (dlgRoot.service ? Math.round(dlgRoot.service.cursor.x) : 0)
                              + "  Y: " + (dlgRoot.service ? Math.round(dlgRoot.service.cursor.y) : 0)
                        color: Commons.Color.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                        opacity: 0.6
                    }

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Style.space(6)
                        Ui.Button {
                            text: "OK"
                            onClicked: dlgRoot.closeDialog()
                        }
                        Ui.Button {
                            text: "Cancel"
                            onClicked: dlgRoot.closeDialog()
                        }
                    }
                }
            }
        }
    }
}
