import QtQuick
import QtQuick.Controls as QQC
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "model/Workflow.js" as Workflow
import "model/Dialogs.js" as DialogState
import "model/KeyDisplay.js" as KeyDisplay

Item {
    id: dlgRoot
    property var service: null
    property bool dialogOpen: false
    property var dialogState: ({kind:"",stage:"",text:"",steps:[],capturing:false})
    readonly property string dialogKind: dialogState.kind
    readonly property bool capturing: dialogOpen && dialogState.capturing
    readonly property var view: DialogState.description(dialogState, service ? service.environment : {})
    readonly property var circlePreview: dialogOpen && dialogKind === "circle_placer" ? dialogState : null
    property real anchorX: 0
    property real anchorY: 0
    property double graceEnd: 0
    property int revision: 0
    property bool working: false
    property string screenName: ""
    property var clipboardCallback: null
    property var manualCategories: DialogState.categories()
    property string category: "Commons"
    property var manualKeys: ["", "", ""]
    readonly property var keyOptions: [""].concat(manualKeys.filter(Boolean),(manualCategories.find(function(c){return c.name===dlgRoot.category}) || manualCategories[0]).keys).filter(function(k,i,a){return a.indexOf(k)===i})
    signal focusInput()

    function reanchor() {
        if (!service) return
        anchorX = service.cursor.x
        anchorY = service.cursor.y
        graceEnd = Date.now() + 700
    }
    function openDialog(kind, zone, keepBlink) {
        closeDialog(false, !!keepBlink)
        if (!service || !service.ready) return
        dialogState = DialogState.open(kind, service.cursor, zone)
        if (zone) { var next=DialogState.clone(dialogState);next.stage="ZoneCommand";dialogState=next }
        // Only one output takes exclusive focus. Keep native placement and scaling.
        var output=service.monitors.find(function(m){return Workflow.containsPoint(service.cursor,m,m.width/(m.scale||1),m.height/(m.scale||1))})
        screenName=output?output.name:((Quickshell.screens[0]||{}).name||"")
        working=false
        dialogOpen=true
        reanchor()
        Qt.callLater(function(){dlgRoot.focusInput()})
    }
    function closeDialog(restorePointer, keepBlink) {
        var cancelledRead=clipboardCallback
        revision++
        dialogOpen=false
        working=false
        clipboardCallback=null
        clipboardWait.stop()
        if(service)service.dialogClosed(!!restorePointer,!!keepBlink)
        if(dialogState.capturing){var next=DialogState.clone(dialogState);next.capturing=false;dialogState=next}
        if(cancelledRead)cancelledRead(null,"Clipboard read cancelled")
    }
    function fail(message) { var next=DialogState.clone(dialogState);next.error=String(message);dialogState=next;working=false }
    function edit(value) {
        if(working || capturing)return
        var next=DialogState.clone(dialogState);next.text=value;next.error=""
        dialogState=DialogState.preview(next,Workflow)
        if(dialogKind==="circle_placer")reanchor()
        if(dialogKind==="registry_hub" && dialogState.stage==="Script")service.change("","script",value)
        // Single-letter commands never apply to editable field stages.
        if(view.command && /^[a-z?]$/i.test(value) && !dialogState.error) submit(value)
    }
    function submit(value, fromButton) {
        if(!dialogOpen || working || capturing)return
        try {
            var result=DialogState.submit(dialogState,value===undefined?dialogState.text:value,service.environment,service.cursor,Workflow)
            dialogState=result.state
            if(dialogState.capturing&&fromButton){var armed=DialogState.clone(dialogState);armed.captureReady=true;dialogState=armed}
            if(result.swap){openDialog(result.swap,undefined,true);return}
            if(result.close)closeDialog(!result.effects.some(function(e){return ["queue","play","zone","stop"].indexOf(e.kind)>=0}))
            result.effects.forEach(function(effect){dlgRoot.perform(effect)})
            if(dialogOpen)Qt.callLater(function(){dlgRoot.focusInput()})
        } catch(e) { fail(e.message) }
    }
    function perform(effect) {
        if(effect.kind==="change")service.change(effect.section,effect.key,effect.value)
        else if(effect.kind==="queue")service.later(effect.actions)
        else if(effect.kind==="export") {
            working=true
            var token=revision
            service.appendActions(effect.name,effect.actions,function(v,e){if(token!==dlgRoot.revision)return;if(e)dlgRoot.fail(e);else dlgRoot.closeDialog(true)})
        } else if(effect.kind==="play")service.play(service.environment.script,effect.origin)
        else if(effect.kind==="clear")service.clearWorkflow()
        else if(effect.kind==="stop")service.stop()
        else if(effect.kind==="zone")service.captureZone(effect.rect,effect.method,effect.target)
        else if(effect.kind==="zone-router")openDialog("paste_buffers",effect.rect,true)
    }
    function receiveKeys(sample) {
        if(!capturing)return
        try {dialogState=DialogState.recorded(dialogState,sample,KeyDisplay.name)}catch(e){fail(e.message)}
        if(!capturing)Qt.callLater(function(){dlgRoot.focusInput()})
    }
    function addManual(done) {
        try {
            var chosen=manualKeys.filter(Boolean),next=DialogState.clone(dialogState)
            if(chosen.length)DialogState.addStep(next,chosen)
            else if(!done)throw Error("Choose a key")
            if(done)DialogState.field(next,"Command")
            dialogState=next;manualKeys=["","",""]
            Qt.callLater(function(){dlgRoot.focusInput()})
        }catch(e){fail(e.message)}
    }
    function cancel() {
        if(view.manual){var next=DialogState.clone(dialogState);next.steps=next.steps.slice(0,next.manualStart);DialogState.field(next,"Command");dialogState=next;focusInput()}
        else closeDialog(true)
    }
    function dismissIfMoved() {
        if(!dialogOpen || !service || working || Date.now()<graceEnd || !DialogState.mouseDismiss(dialogState))return
        if(Math.abs(service.cursor.x-anchorX)>60 || Math.abs(service.cursor.y-anchorY)>60)closeDialog(true)
    }
    function nameAction(command) {
        if(working)return
        if(dialogKind==="save_cursor" && command==="x"){var next=DialogState.clone(dialogState);next.points.push(DialogState.point(service.cursor));dialogState=next;focusInput();return}
        submit()
        if(dialogState.stage==="Command"&&!dialogState.error)submit(command,true)
    }
    function replayKeys() {
        try {
            var result=DialogState.submit(dialogState,"",service.environment,service.cursor,Workflow)
            var actions=[Workflow.move(dialogState.origin,0)].concat(result.effects[0].actions)
            closeDialog();service.later(actions)
        }catch(e){fail(e.message)}
    }
    function scriptAction(action) {
        if(working)return
        service.change("","script",dialogState.text)
        if(action==="play"){var origin=dialogState.origin.slice();closeDialog();service.play(service.environment.script,origin)}
        else if(action==="clear"){service.clearWorkflow();var next=DialogState.clone(dialogState);next.text=service.environment.script;dialogState=next}
        else if(action==="export")service.exportState()
        else if(action==="import"){closeDialog(true);service.importState()}
        else if(action==="schedule"){
            var next=DialogState.clone(dialogState);next.stage="ScheduleName";next.text="";dialogState=next
        }
    }
    function accept() {
        if(dialogState.stage==="Manual"){addManual(true);return}
        if(dialogState.stage==="ScheduleName") {
            if(!dialogState.text.trim()){fail("Enter a schedule name");return}
            var name=dialogState.text.trim();closeDialog(true);service.saveSchedule(name);return
        }
        submit()
    }
    function pasteBuffer(enter) {
        try {
            service.change("buffers",dialogState.slot,dialogState.text)
            var actions=[Workflow.move(dialogState.origin,0)].concat(Workflow.pasteSequence([dialogState.slot],service.environment))
            if(enter)actions.push(Workflow.keys("ENTER"))
            closeDialog();service.later(actions)
        }catch(e){fail(e.message)}
    }
    function readClipboard(callback) {
        openDialog("clipboard_read")
        clipboardCallback=callback
        clipboardWait.restart()
    }
    TextEdit {id:clipboard;visible:false;textFormat:TextEdit.PlainText}
    Timer {
        id:clipboardWait;interval:100
        onTriggered:{
            var f=dlgRoot.clipboardCallback
            if(!f)return
            var value=null,failure=null
            if(!clipboard.canPaste)failure="Clipboard has no text"
            else {clipboard.clear();clipboard.paste();value={text:clipboard.text};clipboard.clear()}
            dlgRoot.clipboardCallback=null;dlgRoot.closeDialog()
            f(value,failure)
        }
    }

    // Keep the supplied layer-shell surface and bottom-center appearance.
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
            mask: Region { item: dialogBox }
            visible: dlgRoot.dialogOpen && dlgRoot.service !== null && modelData.name === dlgRoot.screenName

            Timer {
                interval: 50
                repeat: true
                running: window.visible && dlgRoot.dialogOpen
                onTriggered:dlgRoot.dismissIfMoved()
            }
            Connections {
                target: dlgRoot
                function onFocusInput(){if(window.visible)editor.input.forceActiveFocus()}
            }
            onVisibleChanged: if(visible)Qt.callLater(function(){editor.input.forceActiveFocus()})
            Shortcut {sequence:StandardKey.Cancel;enabled:window.visible&&!dlgRoot.capturing;onActivated:dlgRoot.cancel()}

            Rectangle {
                id: dialogBox
                width: Style.space(380)
                height: Math.min(window.height-Style.space(28),col.implicitHeight+Style.space(24))
                x: (window.width-width)/2
                y: window.height-height-14
                radius: Style.cornerRadius
                color: Commons.Color.background
                opacity: 0.93
                Column {
                    id: col
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Style.space(12)
                    spacing: Style.space(8)
                    width: parent.width-Style.space(24)
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: dlgRoot.view.title
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Commons.Color.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                        font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "X: "+(dlgRoot.service?Math.round(dlgRoot.service.cursor.x):0)+"  Y: "+(dlgRoot.service?Math.round(dlgRoot.service.cursor.y):0)
                        textFormat: Text.PlainText
                        color: Commons.Color.foreground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                        opacity: 0.6
                    }
                    Text {
                        width:parent.width;text:dlgRoot.view.hint;textFormat:Text.PlainText;wrapMode:Text.Wrap
                        color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption
                    }
                    CodeArea {
                        id: editor
                        width:parent.width
                        visible:!dlgRoot.view.manual
                        minimumHeight:dlgRoot.view.multiline?Style.space(90):Style.space(48)
                        maximumHeight:dlgRoot.view.multiline?Math.min(Style.space(640),Math.max(Style.space(90),window.height*0.65)):Style.space(48)
                        text:dlgRoot.dialogState.text||""
                        readOnly:dlgRoot.view.readonly || dlgRoot.working
                        submitOnReturn:true
                        captureKeys:dlgRoot.capturing
                        onAccepted:dlgRoot.accept()
                        onCancelled:dlgRoot.cancel()
                        onEdited:function(value){dlgRoot.edit(value)}
                    }
                    Column {
                        visible:dlgRoot.view.manual;width:parent.width;spacing:Style.space(6)
                        Ui.Dropdown {width:parent.width;label:"Category";value:dlgRoot.category;options:dlgRoot.manualCategories.map(function(c){return c.name});onChanged:function(value){dlgRoot.category=value}}
                        Repeater {
                            model:3
                            Ui.Dropdown {
                                required property int index
                                width:parent.width;label:"Key "+(index+1);value:dlgRoot.manualKeys[index];options:dlgRoot.keyOptions
                                onChanged:function(value){var next=dlgRoot.manualKeys.slice();next[index]=value;dlgRoot.manualKeys=next}
                            }
                        }
                        Ui.Button {text:"Add step";onClicked:dlgRoot.addManual(false)}
                    }
                    Ui.NumberField {
                        visible:dlgRoot.dialogKind==="record_keys"
                        label:"Step delay (ms)";value:dlgRoot.dialogState.delay||200;from:10;to:600000
                        onModified:function(value){var next=DialogState.clone(dlgRoot.dialogState);next.delay=value;dlgRoot.dialogState=next}
                    }
                    Text {
                        visible:dlgRoot.dialogKind==="record_keys";width:parent.width
                        text:(dlgRoot.dialogState.steps||[]).slice(-5).map(function(step){return step.kind==="paste"?"Paste · "+step.text.slice(0,35):step.keys.join(" + ")}).join("\n")
                        textFormat:Text.PlainText;wrapMode:Text.Wrap;color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption
                    }
                    Text {
                        visible:!!dlgRoot.dialogState.error;width:parent.width;text:dlgRoot.dialogState.error||""
                        textFormat:Text.PlainText;wrapMode:Text.Wrap;color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption
                    }
                    Flow {
                        width:parent.width;spacing:Style.space(6)
                        Repeater {
                            model:dlgRoot.view.buttons
                            Ui.Button {required property var modelData;text:modelData.label;enabled:!dlgRoot.working&&!dlgRoot.capturing;onClicked:dlgRoot.submit(modelData.key,true)}
                        }
                    }
                    Flow {
                        visible:dlgRoot.dialogState.stage==="Name";width:parent.width;spacing:Style.space(6)
                        Repeater {
                            model:dlgRoot.dialogKind==="save_cursor"?[{key:"x",label:"Add point"},{key:"c",label:"Save + click"}]:[{key:"z",label:"Capture"},{key:"x",label:"Manual"},{key:"c",label:"Paste"}]
                            Ui.Button {required property var modelData;text:modelData.label;enabled:!dlgRoot.working;onClicked:dlgRoot.nameAction(modelData.key)}
                        }
                    }
                    Flow {
                        visible:dlgRoot.dialogState.stage==="Script";width:parent.width;spacing:Style.space(6)
                        Repeater {
                            model:["play","clear","export","import","schedule"]
                            Ui.Button {required property string modelData;text:modelData;onClicked:dlgRoot.scriptAction(modelData)}
                        }
                    }
                    Ui.Button {visible:dlgRoot.dialogKind==="record_keys"&&dlgRoot.dialogState.stage==="Command"&&(dlgRoot.dialogState.steps||[]).length>0;text:"Replay";onClicked:dlgRoot.replayKeys()}
                    Row {
                        visible:dlgRoot.dialogKind==="paste_buffers"&&dlgRoot.dialogState.stage==="Edit"
                        spacing:Style.space(6)
                        Ui.Button {text:"Paste";onClicked:dlgRoot.pasteBuffer(false)}
                        Ui.Button {text:"Paste + Enter";onClicked:dlgRoot.pasteBuffer(true)}
                    }
                    Row {
                        anchors.horizontalCenter:parent.horizontalCenter;spacing:Style.space(6)
                        Ui.Button {text:"OK";enabled:!dlgRoot.working&&!dlgRoot.capturing;onClicked:dlgRoot.accept()}
                        Ui.Button {
                            text:"Cancel"
                            onClicked:dlgRoot.cancel()
                        }
                    }
                }
            }
        }
    }
}
