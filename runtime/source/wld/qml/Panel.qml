import QtQuick
import QtQuick.Controls as QQC
import Quickshell
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "model/Workflow.js" as Workflow

Ui.Panel {
  id:root
  moduleName:"io.github.commanderturtle.macrohelp"
  readonly property var app:bar&&bar.shell?bar.shell.serviceFor(moduleName):null
  readonly property var env:app?app.environment:({})
  readonly property color fg:Commons.Color.foreground
  property string scheduleName:"Macrohelp workflow"
  property string slot:"Z"
  property string zoneMethod:"X"
  property string targetSlot:"V"
  property string varSlot:"A"
  property bool confirmClear:false
  property string exportFormat:"JSON"
  property string movementName:"cursor-session"
  property var movements:[]
  property var bufferSequence:[]
  property string coordinateText:"0,0"
  property string vectorText:"0,0"
  property string regionText:"0,0,100,100"
  property string referenceText:""
  property var captureAction:null
  property string captureMessage:""
  property string localError:""
  implicitWidth:button.implicitWidth
  implicitHeight:button.implicitHeight
  function attempt(f){try{localError="";f()}catch(e){localError=e.message}}
  function coords(text,count){var a=text.split(/[,\s]+/).filter(Boolean);if(a.length!==count)throw Error("Expected "+count+" coordinates");return a.map(function(n){return Math.round(Workflow.numeric(n,-100000,100000,"Coordinate"))})}
  function capture(f){if(!app)return;captureAction=f;captureMessage="Capture in 3 seconds";close();captureDelay.restart()}
  function append(actions){if(!app)return;app.later(actions)}
  function addMovement(point){if(movements.length>=10000)throw Error("Path limit reached");movements=movements.concat([[0,200,point[0],point[1]]])}
  function movementActions(){return [{type:"cursormovements",cursormovsmap:Workflow.copy(movements),cursorMovsId:"",optionalkeysstroke:[],loop:1}]}
  function reorderBuffer(index,delta){var a=bufferSequence.slice(),to=index+delta;if(to<0||to>=a.length)return;var slot=a.splice(index,1)[0];a.splice(to,0,slot);bufferSequence=a}
  function atCursor(){if(!app||!panel.screen)return false;var g=app.monitors.find(function(m){return m.name===panel.screen.name});return g?Workflow.containsPoint(app.cursor,g,panel.screen.width,panel.screen.height):Quickshell.screens.length>0&&Quickshell.screens[0].name===panel.screen.name}
  onOpenedChanged:if(opened&&app){app.activePanel=root;app.refresh()}
  Component.onDestruction:if(app&&app.activePanel===root)app.activePanel=null
  Timer {interval:1000;repeat:true;running:root.opened&&root.app&&root.app.page==="Tasks";onTriggered:root.app.refresh()}
  Connections {
    target:root.app
    function onShowPanel(){if(root.app.activePanel===root||(!root.app.activePanel&&root.atCursor()))root.open()}
    function onHidePanel(){root.close()}
    function onActivePanelChanged(){if(root.app.activePanel!==root)root.close()}
    function onGenerationChanged(){captureDelay.stop();root.captureAction=null;root.captureMessage=""}
    function onClipboardRequested(callback){if(root.app.activePanel===root)Qt.callLater(function(){bufferEditor.pasteClipboard(callback)})}
  }
  Timer {id:captureDelay;interval:3000;onTriggered:{root.captureMessage="";var f=root.captureAction;root.captureAction=null;if(f&&root.app)f([Math.round(root.app.cursor.x),Math.round(root.app.cursor.y)])}}
  Ui.WidgetButton {id:button;anchors.fill:parent;bar:root.bar;text:"󰍽";tooltipText:"Macrohelp · right-click to stop";onPressed:function(mouseButton){if(mouseButton===Qt.RightButton&&root.app)root.app.stop();else root.toggle()}}
  Ui.KeyboardPanel {
    id:panel;anchorItem:button;owner:root;bar:root.bar;open:root.opened
    focusTarget:root.app&&root.app.page==="Workflow"?workflowEditor.input:root.app&&root.app.page==="Buffers"?bufferEditor.input:body
    contentWidth:panel.fittedContentWidth(Style.space(650))
    contentHeight:panel.fittedContentHeight(Style.space(780),Style.space(780))
    Column {
      id:body;anchors.fill:parent;spacing:Style.space(8)
      Keys.onEscapePressed:root.close()
      Ui.PanelHero {title:"Macrohelp";meta:root.app&&root.app.connected?root.app.endpoint:"Controller disconnected";foreground:root.fg;fontFamily:Style.font.family}
      Flow {
        width:parent.width;spacing:Style.space(5)
        Repeater {model:["Workflow","Pointer","Keys","Buffers","Zones","Tasks","Presets","View","Help"];Ui.Button{required property string modelData;text:modelData;foreground:root.app&&root.app.page===modelData?Commons.Color.accent:root.fg;onClicked:if(root.app)root.app.page=modelData}}
        Ui.Button {id:menuButton;text:"Menu";enabled:root.app&&root.app.ready;onClicked:toolsMenu.popup()}
        Ui.Button {text:"Stop";onClicked:if(root.app)root.app.stop()}
        Ui.Button {text:"×";onClicked:root.close()}
      }
      Text {width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:Commons.Color.accent;font.family:Style.font.family;font.pixelSize:Style.font.caption;text:root.localError||(root.app?(root.app.error||root.app.message):"Enable the Macrohelp service");visible:text!==""}
      QQC.ScrollView {
        id:scroll;contentWidth:availableWidth
        width:parent.width;height:Math.max(0,parent.height-y);clip:true
        Column {
          width:scroll.availableWidth;spacing:Style.space(10)
          enabled:root.app&&root.app.ready
          Column {
            width:parent.width;spacing:Style.space(8);visible:root.app&&root.app.page==="Workflow"
            Ui.TextField {id:taskName;width:parent.width;text:root.scheduleName;placeholderText:"Schedule name";onTextEdited:root.scheduleName=text}
            CodeArea {id:workflowEditor;width:parent.width;implicitHeight:Style.space(240);text:String(root.env.script||"");hint:"{point 1}{click}{paste A}{wait 200}{CTRL+S}";onEdited:function(value){root.app.change("","script",value)}}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:root.app&&root.app.busy?"Running…":"Play";enabled:root.app&&!root.app.busy&&root.app.connected;onClicked:root.app.play(root.env.script)}
              Ui.Button{text:"Save schedule";enabled:root.app&&root.app.connected;onClicked:root.app.saveSchedule(root.scheduleName)}
              Ui.Button{text:"Clear…";onClicked:root.confirmClear=!root.confirmClear}
            }
            Flow {visible:root.confirmClear;width:parent.width;spacing:Style.space(6)
              Ui.Button {text:"Clear workflow, variables and points";onClicked:{root.app.clearWorkflow();root.confirmClear=false}}
              Ui.Button {text:"Cancel";onClicked:root.confirmClear=false}
            }
            Ui.TextField {width:parent.width;text:root.env.paths?root.env.paths.import:"";placeholderText:"Import path · blank = choose file";onTextEdited:root.app.change("paths","import",text)}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button {text:"Browse import…";onClicked:root.app.browsePath("import")}
              Ui.Button {text:"Import environment";onClicked:root.app.importState()}
            }
            Ui.TextField {width:parent.width;text:root.env.paths?root.env.paths.export:"";placeholderText:"Export path · blank = choose file";onTextEdited:root.app.change("paths","export",text)}
            Ui.Dropdown {width:parent.width;label:"Environment format";value:root.exportFormat;options:["JSON","Windows text"];onChanged:function(v){root.exportFormat=v}}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button {text:"Browse export…";onClicked:root.app.browsePath("export")}
              Ui.Button {text:"Export environment";onClicked:root.app.exportState(root.exportFormat==="Windows text"?"windows":"json")}
            }
            CodeArea {visible:root.app&&root.app.resolvedScript!=="";width:parent.width;implicitHeight:Style.space(125);readOnly:true;text:root.app?root.app.resolvedScript:"";hint:"Resolved workflow"}
            Text {width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;text:"{wait ms}  {type text}  {point 1…16}  {paste A,Z}  {buf Z}  {enter A}  {CTRL+S}\n{click on|off}  {left|right|middle}  {play name}  {wheelup 3}\n{if A contains text}…{else}…{endif}  {set A value}\n{zone Z,X}  {zonebuf Z,X,V}  {rgdclip ref}  {rgdappend ref}";color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption}
            Ui.TextField {width:parent.width;text:String(root.env.registry||"");placeholderText:"Regedited file path (optional; rgd on PATH)";onTextEdited:root.app.change("","registry",text)}
            Ui.TextField {width:parent.width;text:root.referenceText;placeholderText:"Regedited reference";onTextEdited:root.referenceText=text}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:"Copy reference";onClicked:root.app.call({method:"regedited",action:"clip",file:root.env.registry,reference:root.referenceText})}
              Ui.Button{text:"Append clipboard";onClicked:root.app.call({method:"regedited",action:"append",file:root.env.registry,reference:root.referenceText})}
            }
            Text {width:parent.width;wrapMode:Text.Wrap;text:"Shells open the terminal shortcut, type the command, then press Enter. Blank commands use the terminal's default shell.";color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption}
            Repeater {model:[{id:"T",label:"Terminal shortcut · blank = native binding"},{id:"X",label:"Shell X · {bash}"},{id:"C",label:"Shell C · {cmd}"},{id:"G",label:"Shell G · {python} / {regedited}"},{id:"P",label:"Shell P · {pwsh} / {powershell}"}];Column{required property var modelData;width:parent.width;spacing:Style.space(4);Text{text:modelData.label;color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption} Ui.TextField{width:parent.width;text:root.env.shells&&root.env.shells[modelData.id]!==undefined?String(root.env.shells[modelData.id]):modelData.id==="P"?"pwsh":"";onTextEdited:root.app.change("shells",modelData.id,text)}}}
          }
          Column {
            width:parent.width;spacing:Style.space(8);visible:root.app&&root.app.page==="Pointer"
            Text {textFormat:Text.PlainText;text:root.app?Math.round(root.app.cursor.x)+", "+Math.round(root.app.cursor.y):"";color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.subtitle}
            Ui.Dropdown {width:parent.width;label:"Point";value:String(root.app?root.app.pointIndex+1:1);options:Array.from({length:16},function(_,i){return String(i+1)});onChanged:function(value){root.app.pointIndex=Number(value)-1}}
            Ui.TextField {width:parent.width;text:root.coordinateText;placeholderText:"x,y";onTextEdited:root.coordinateText=text}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:"Save x,y / point";onClicked:root.attempt(function(){root.app.change("points",root.app.pointIndex,Workflow.pointValue(root.coordinateText,root.env))})}
              Ui.Button{text:"Capture pointer (3s)";onClicked:root.capture(function(p){root.app.change("points",root.app.pointIndex,p)})}
              Ui.Button{text:"Move to point";onClicked:root.attempt(function(){root.append([Workflow.move(root.env.points[root.app.pointIndex],100)])})}
              Ui.Button{text:"Clear point";onClicked:root.app.change("points",root.app.pointIndex,null)}
            }
            Text {textFormat:Text.PlainText;text:"Saved: "+(root.env.points&&root.env.points[root.app.pointIndex]?root.env.points[root.app.pointIndex].join(", "):"—");color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption}
            Ui.TextField {width:parent.width;text:root.movementName;placeholderText:"Cursor sequence / export name";onTextEdited:root.movementName=text}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button {text:"Add another (3s)";onClicked:root.capture(function(p){root.attempt(function(){root.addMovement(p)})})}
              Ui.Button {text:"Add saved point";onClicked:root.attempt(function(){var p=root.env.points[root.app.pointIndex];if(!p)throw Error("Save a point first");root.addMovement(p)})}
              Ui.Button {text:"Save + Click (3s)";onClicked:root.capture(function(p){root.attempt(function(){root.addMovement(p);root.app.exportActions(root.movementName,root.movementActions().concat([Workflow.keys("LEFT_MOUSE"),Workflow.wait(200)]),false)})})}
              Ui.Button {text:"Export movements…";enabled:root.movements.length>0;onClicked:root.app.exportActions(root.movementName,root.movementActions(),false)}
              Ui.Button {text:"Append movements…";enabled:root.movements.length>0;onClicked:root.app.exportActions(root.movementName,root.movementActions(),true)}
              Ui.Button {text:"Clear movements";onClicked:root.movements=[]}
            }
            Text {text:root.movements.length+" saved movements";color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption}
            Ui.NumberField {label:"Circle radius";value:root.app?root.app.radius:100;from:0;to:10000;onModified:function(value){root.app.radius=value}}
            Ui.TextField {width:parent.width;text:root.app?String(root.app.angle):"0";placeholderText:"Angle · pi/2, 90, 2*(30+15)";onEditingFinished:root.attempt(function(){root.app.angle=Workflow.expression(text)})}
            Ui.Dropdown {width:parent.width;label:"Angle unit";value:root.app?root.app.unit:"degrees";options:["degrees","radians"];onChanged:function(value){root.app.unit=value}}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:"Set circle origin (3s)";onClicked:root.capture(function(p){root.app.circleOrigin=p})}
              Ui.Button{text:"Move to circle target";onClicked:root.append([Workflow.move(root.app.circleTarget,150)])}
              Ui.Button{text:"Target → point";onClicked:root.app.change("points",root.app.pointIndex,root.app.circleTarget)}
              Ui.Button{text:"Append target…";onClicked:root.app.exportActions(root.movementName,[Workflow.move(root.app.circleTarget,150),Workflow.wait(200)],true)}
              Ui.Button{text:"Next circle";onClicked:root.app.circleOrigin=root.app.circleTarget.slice()}
              Ui.Button{text:"Circle → zone";onClicked:{var p=root.app.circleOrigin,r=root.app.radius;root.app.change("zones",root.slot,[p[0]-r,p[1]-r,p[0]+r,p[1]+r])}}
            }
            Ui.TextField {width:parent.width;text:root.vectorText;placeholderText:"Vector · x,y / -20i+20j / 316 @ -2i+2j";onTextEdited:root.vectorText=text}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button {text:"Apply vector";onClicked:root.attempt(function(){root.app.vector(root.vectorText)})}
              Ui.Button {text:"Move origin + vector";onClicked:root.attempt(function(){root.app.vector(root.vectorText);root.append([Workflow.move(root.app.circleTarget,150)])})}
            }
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:root.app&&root.app.recordingPath?"Stop pointer recording":"Record pointer path";onClicked:{root.close();root.app.togglePath()}}
              Ui.Button{text:"Play path";onClicked:root.append([{type:"cursormovements",cursormovsmap:root.app.path,cursorMovsId:"",optionalkeysstroke:[],loop:1}])}
              Ui.Button{text:"Export path…";enabled:root.app&&root.app.path.length>0;onClicked:root.app.exportActions(root.movementName,[{type:"cursormovements",cursormovsmap:Workflow.copy(root.app.path),cursorMovsId:"",optionalkeysstroke:[],loop:1}],false)}
              Ui.Button{text:"Clear path";onClicked:root.app.path=[]}
            }
            Text {width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;text:root.app?root.app.path.length+" path points":"";color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption}
          }
          KeyTools {width:parent.width;service:root.app;visible:root.app&&root.app.page==="Keys"}
          Column {
            width:parent.width;spacing:Style.space(8);visible:root.app&&root.app.page==="Buffers"
            Ui.Dropdown {width:parent.width;label:"Paste buffer";value:root.slot;options:["Z","X","C","V"];onChanged:function(value){root.slot=value}}
            CodeArea {id:bufferEditor;width:parent.width;text:root.env.buffers?root.env.buffers[root.slot]:"";onEdited:function(value){root.app.change("buffers",root.slot,value)}}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:"Clipboard → buffer";onClicked:{var slot=root.slot;root.app.readClipboard(function(v,e){if(!e)root.app.change("buffers",slot,v.text)})}}
              Ui.Button{text:"Paste";onClicked:root.append([Workflow.paste(root.env.buffers[root.slot])])}
              Ui.Button{text:"Paste + Enter";onClicked:root.append([Workflow.paste(root.env.buffers[root.slot]),Workflow.keys("ENTER")])}
              Ui.Button{text:"Add to sequence";enabled:root.bufferSequence.length<1000;onClicked:root.bufferSequence=root.bufferSequence.concat([root.slot])}
            }
            Repeater {model:root.bufferSequence;Row {required property string modelData;required property int index;spacing:Style.space(5)
              Ui.Button {text:"↑";onClicked:root.reorderBuffer(parent.index,-1)}
              Ui.Button {text:"↓";onClicked:root.reorderBuffer(parent.index,1)}
              Text {text:parent.modelData;color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.body}
              Ui.Button {text:"×";onClicked:{var a=root.bufferSequence.slice();a.splice(parent.index,1);root.bufferSequence=a}}
            }}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button {text:"Paste sequence";enabled:root.bufferSequence.length>0;onClicked:root.attempt(function(){root.append(Workflow.pasteSequence(root.bufferSequence,root.env))})}
              Ui.Button {text:"Clear sequence";onClicked:root.bufferSequence=[]}
            }
            Ui.Dropdown {width:parent.width;label:"Variable";value:root.varSlot;options:["A","S","D","F"];onChanged:function(value){root.varSlot=value}}
            CodeArea {width:parent.width;text:root.env.vars?root.env.vars[root.varSlot]:"";onEdited:function(value){root.app.change("vars",root.varSlot,value)}}
          }
          Column {
            width:parent.width;spacing:Style.space(8);visible:root.app&&root.app.page==="Zones"
            Ui.Dropdown {width:parent.width;label:"Zone";value:root.slot;options:["Z","X","C","V"];onChanged:function(value){root.slot=value;root.app.zoneSlot=value}}
            Ui.Dropdown {width:parent.width;label:"Method · Z screenshot / X OCR / C custom OCR / V copy";value:root.zoneMethod;options:["Z","X","C","V"];onChanged:function(value){root.zoneMethod=value}}
            Ui.TextField {width:parent.width;text:root.regionText;placeholderText:"x1,y1,x2,y2";onTextEdited:root.regionText=text}
            Flow {width:parent.width;spacing:Style.space(6)
              Ui.Button{text:"Save rectangle / points / zone";onClicked:root.attempt(function(){root.app.change("zones",root.slot,Workflow.zoneValue(root.regionText,root.env))})}
              Ui.Button{text:root.app&&root.app.zoneStart?"Capture second corner (3s)":"Capture first corner (3s)";onClicked:root.capture(function(p){if(!root.app.zoneStart)root.app.zoneStart=p;else{root.app.change("zones",root.slot,root.app.zoneStart.concat(p));root.app.zoneStart=null}})}
              Ui.Button{text:"Capture / copy";onClicked:root.app.useZone(root.slot,root.zoneMethod)}
              Ui.Button{text:"Clear";onClicked:{root.app.change("zones",root.slot,null);root.app.zoneStart=null}}
            }
            Ui.Dropdown {width:parent.width;label:"Target paste buffer";value:root.targetSlot;options:["Z","X","C","V"];onChanged:function(value){root.targetSlot=value}}
            Ui.Button {text:"Capture → buffer";enabled:root.zoneMethod!=="Z";onClicked:root.app.useZone(root.slot,root.zoneMethod,root.targetSlot)}
            Text {width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;text:"Desktop capture shortcuts · Saved: "+(root.env.zones&&root.env.zones[root.slot]?root.env.zones[root.slot].join(", "):"—");color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.caption}
          }
          Column {
            width:parent.width;spacing:Style.space(8);visible:root.app&&root.app.page==="Tasks"
            Ui.Button{text:"Refresh";onClicked:root.app.refresh()}
            Repeater {model:root.app?root.app.tasks:[];Row{required property var modelData;width:parent.width;spacing:Style.space(6);Text{width:Math.max(0,parent.width-runButton.width-parent.spacing);elide:Text.ElideRight;textFormat:Text.PlainText;text:modelData.name;color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.body} Ui.Button{id:runButton;text:"Run";onClicked:root.app.runSaved(parent.modelData.name)}}}
            Repeater {
              model:root.app?(root.app.status.scheduled||[]).concat(root.app.status.running||[],root.app.status.finished||[]).slice(-30):[]
              Column {required property var modelData;width:parent.width
                Text {width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;text:"#"+modelData.task_number+" "+modelData.name+" · "+modelData.state; color:root.fg;font.family:Style.font.family;font.pixelSize:Style.font.body}
                Ui.Button {visible:parent.modelData.state==="running"||parent.modelData.state==="scheduled";text:"Stop task";onClicked:root.app.http("POST","/stop?id="+parent.modelData.task_number,{},function(){root.app.refresh()})}
              }
            }
          }
          Loader {width:parent.width;active:!!root.app&&root.app.page==="Presets";sourceComponent:Component {Presets {width:parent.width;service:root.app}}}
          Column {
            width:parent.width;spacing:Style.space(8);visible:root.app&&root.app.page==="View"
            Repeater {model:[{key:"crosshair",label:"Crosshair"},{key:"grid",label:"Grid"},{key:"coordinates",label:"Cursor coordinates"},{key:"panel",label:"Top-right coordinate panel"},{key:"keys",label:"Key display"},{key:"coordinates_only",label:"Coordinates only"}];Ui.Toggle{required property var modelData;width:parent.width;label:modelData.label;checked:root.env.view?root.env.view[modelData.key]:false;onClicked:root.app.change("view",modelData.key,!checked)}}
            Text {visible:root.app&&root.app.keysEnabled&&!root.app.keyFeedAvailable;text:"Native key events unavailable";color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
            Ui.Button{text:"Reconnect";onClicked:root.app.reconnect()}
          }
          Help {width:parent.width;visible:root.app&&root.app.page==="Help"}
        }
      }
    }
  }
  ToolsMenu {
    id:toolsMenu
    parent:menuButton
    QQC.MenuItem {text:"Workflow";onTriggered:root.app.page="Workflow"}
    QQC.MenuItem {text:"Save Cursor Position";onTriggered:{root.app.pointer();root.app.page="Pointer"}}
    QQC.MenuItem {text:"Paste Buffers";onTriggered:root.app.page="Buffers"}
    QQC.MenuItem {text:"Record / Manual Keys";onTriggered:root.app.page="Keys"}
    QQC.MenuItem {text:"Circle Placer";onTriggered:root.app.page="Pointer"}
    QQC.MenuItem {text:"View";onTriggered:root.app.page="View"}
    QQC.MenuSeparator {}
    Repeater {model:[{key:"crosshair",label:"Crosshair"},{key:"grid",label:"Grid"},{key:"coordinates",label:"Cursor coordinates"},{key:"panel",label:"Coordinate panel"},{key:"keys",label:"Key display"}];QQC.MenuItem {required property var modelData;text:modelData.label;checkable:true;checked:!!root.env.view&&!!root.env.view[modelData.key];onTriggered:root.app.change("view",modelData.key,!root.env.view[modelData.key])}}
    QQC.MenuSeparator {}
    QQC.MenuItem {text:"Left click";onTriggered:root.app.click("LEFT_MOUSE")}
    QQC.MenuItem {text:"Right click";onTriggered:root.app.click("RIGHT_MOUSE")}
    QQC.MenuItem {text:"Middle click";onTriggered:root.app.click("MIDDLE_MOUSE")}
    QQC.MenuItem {text:"Stop all";onTriggered:root.app.stop()}
    QQC.MenuItem {text:"Help";onTriggered:root.app.page="Help"}
    QQC.MenuSeparator {}
    QQC.MenuItem {text:"Exit overlay";onTriggered:root.app.exitOverlay()}
  }
}
