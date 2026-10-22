import QtQuick
import QtQuick.Controls as QQC
import Quickshell
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
    readonly property real cursorDist: (service ? Math.sqrt(Math.pow(service.cursor.x - anchorX, 2) + Math.pow(service.cursor.y - anchorY, 2)) : 0)
    onCursorDistChanged: { if(dialogOpen && cursorDist > 10) closeDialog() }

    function openDialog(kind) {
        dialogKind = kind
        dialogOpen = true
        if(service){anchorX=service.cursor.x;anchorY=service.cursor.y}
    }

    function closeDialog() {
        dialogOpen = false
        dialogKind = ""
    }
 
    // Save Cursor dialog - bottom center
    Rectangle {
        id: saveBox
        visible: dlgRoot.dialogOpen && dlgRoot.dialogKind === "save_cursor"
        width: Style.space(300)
        height: saveCol.implicitHeight + Style.space(24)
        x: ((dlgRoot.parent ? dlgRoot.parent.width : 0) - width) / 2
        y: (dlgRoot.parent ? dlgRoot.parent.height : 0) - height - Style.space(60)
        radius: Style.cornerRadius
        color: Commons.Color.background
        opacity: 0.95

        Column {
            id: saveCol
            anchors.horizontalCenter: parent.horizontalCenter
            y: Style.space(12)
            spacing: Style.space(8)
            width: parent.width - Style.space(24)

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "X: " + (dlgRoot.service ? Math.round(dlgRoot.service.cursor.x) : 0)
                      + "  Y: " + (dlgRoot.service ? Math.round(dlgRoot.service.cursor.y) : 0)
                color: Commons.Color.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.body
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Point slot (1-16)"
                color: Commons.Color.foreground
                font.family: Style.font.family
                font.pixelSize: Style.font.caption
                opacity: 0.6
            }

            QQC.SpinBox {
                id: slotBox
                anchors.horizontalCenter: parent.horizontalCenter
                width: Style.space(120)
                from: 1
                to: 16
                value: dlgRoot.service ? dlgRoot.service.pointIndex + 1 : 1
                editable: true
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.space(6)

                Ui.Button {
                    text: "Save"
                    onClicked: {
                        if (dlgRoot.service) {
                            dlgRoot.service.pointIndex = slotBox.value - 1
                            dlgRoot.service.pointer()
                        }
                        dlgRoot.closeDialog()
                    }
                }

                Ui.Button {
                    text: "Cancel"
                    onClicked: dlgRoot.closeDialog()
                }
            }
        }
    }
}
