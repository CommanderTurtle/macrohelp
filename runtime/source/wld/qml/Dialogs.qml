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
    property int lastCloseTime: 0

    property int openCount: 0
    function openDialog(kind) {
        if (dialogOpen) return
        if (Date.now() - lastCloseTime < 5000) return
        openCount++
        console.log("[macrohelp] openDialog #" + openCount + " kind=" + kind + " cursor=" + (service ? service.cursor.x + "," + service.cursor.y : "null"))
        dialogKind = kind
        dialogOpen = true
        if (service) {
            anchorX = service.cursor.x
            anchorY = service.cursor.y
        }
    }

    function closeDialog() {
        console.log("[macrohelp] closeDialog (was open=" + dialogOpen + ")")
        lastCloseTime = Date.now()
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
            onVisibleChanged: console.log("[macrohelp] window visible=" + visible + " dialogOpen=" + dlgRoot.dialogOpen)

            // Mouse-leave-to-close: 700ms grace, 3px tolerance, 50ms poll
            Timer {
                id: leaveTimer
                repeat: true
                running: dlgRoot.dialogOpen
                property int graceEnd: 0
                property int tickCount: 0
                onRunningChanged: {
                    tickCount = 0
                    if (running) {
                        graceEnd = Date.now() + 700
                        console.log("[macrohelp] timer START")
                    } else {
                        console.log("[macrohelp] timer STOP (ticks=" + tickCount + ")")
                    }
                }
                onTriggered: {
                    tickCount++
                    if (tickCount % 20 === 0) {
                        var cx = dlgRoot.service ? dlgRoot.service.cursor.x : -1
                        var cy = dlgRoot.service ? dlgRoot.service.cursor.y : -1
                        console.log("[macrohelp] tick#" + tickCount + " cur=" + cx + "," + cy + " anch=" + dlgRoot.anchorX + "," + dlgRoot.anchorY)
                    }
                    if (!dlgRoot.dialogOpen) return
                    if (Date.now() < graceEnd) return
                    if (!dlgRoot.service) return
                    var dx = Math.abs(dlgRoot.service.cursor.x - dlgRoot.anchorX)
                    var dy = Math.abs(dlgRoot.service.cursor.y - dlgRoot.anchorY)
                    if (dx > 60 || dy > 60) {
                        console.log("[macrohelp] MOUSE-LEAVE dx=" + dx + " dy=" + dy)
                        dlgRoot.closeDialog()
                    }
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
                width: Style.space(380)
                height: col.implicitHeight + Style.space(24)
                x: (window.width - width) / 2
                y: window.height - height - 14
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
