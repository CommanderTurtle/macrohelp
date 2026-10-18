import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "model/Workflow.js" as Workflow
import "model/KeyTools.js" as Model

Column {
  id:root
  required property var service
  property var flow:[]
  property string name:"key-session"
  property string chord:"CTRL+C"
  property int hold:100
  property int delay:200
  property int repeats:1
  property string pasteText:""
  property string error:""
  property int selected:0
  property var manual:[{category:"Modifiers",key:"CONTROL_LEFT"},{category:"Commons",key:"C"},{category:"Commons",key:""}]
  readonly property var categories:Model.categories()
  spacing:Style.space(8)
  function attempt(f){try{error="";f()}catch(e){error=e.message}}
  function add(action){flow=Model.append(flow,action,delay);selected=flow.length-1}
  function pick(index,field,value){var next=Workflow.copy(manual);next[index][field]=value;if(field==="category")next[index].key="";manual=next}
  function reorder(at,delta){var next=Workflow.copy(flow),to=at+delta;if(to<0||to>=next.length)return;var a=next.splice(at,1)[0];next.splice(to,0,a);flow=next;selected=to}
  Ui.TextField {width:parent.width;placeholderText:"Sequence / export name";text:root.name;onTextEdited:root.name=text}
  Ui.TextField {width:parent.width;placeholderText:"Key or chord";text:root.chord;onTextEdited:root.chord=text}
  Flow {width:parent.width;spacing:Style.space(8)
    Ui.NumberField {label:"Hold ms";from:1;to:600000;value:root.hold;onModified:function(v){root.hold=v}}
    Ui.NumberField {label:"Step delay ms";from:0;to:600000;value:root.delay;onModified:function(v){root.delay=v}}
    Ui.NumberField {label:"Repeat";from:1;to:1000;value:root.repeats;onModified:function(v){root.repeats=v}}
    Ui.Button {text:"Send chord";onClicked:root.attempt(function(){var a=Workflow.keys(root.chord,root.hold);a.loop=root.repeats;root.service.later([a])})}
    Ui.Button {text:"Add chord";onClicked:root.attempt(function(){var a=Workflow.keys(root.chord,root.hold);a.loop=root.repeats;root.add(a)})}
  }
  Flow {width:parent.width;spacing:Style.space(6)
    Repeater {model:["LEFT_MOUSE","RIGHT_MOUSE","MIDDLE_MOUSE"];Column {required property string modelData;spacing:Style.space(4)
      Ui.Button {text:modelData;onClicked:root.service.later([Workflow.keys(parent.modelData,root.hold)])}
      Ui.Button {text:"Append click JSON";onClicked:root.service.exportActions(root.name,[Workflow.keys(parent.modelData,root.hold)],true)}
    }}
  }
  Text {text:"Manual Keys · up to three keys";color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.subtitle}
  Repeater {model:3;Flow {required property int index;width:parent.width;spacing:Style.space(6)
    Ui.Dropdown {label:"Category "+(parent.index+1);options:Object.keys(root.categories);value:root.manual[parent.index].category;onChanged:function(v){root.pick(parent.index,"category",v)}}
    Ui.Dropdown {label:"Key "+(parent.index+1);options:[""].concat(root.categories[root.manual[parent.index].category]);value:root.manual[parent.index].key;onChanged:function(v){root.pick(parent.index,"key",v)}}
  }}
  Ui.Button {text:"Add manual step";onClicked:root.attempt(function(){root.add(Workflow.keys(Model.chord(root.manual.map(function(k){return k.key})),root.hold))})}
  KeyRecorder {width:parent.width;onRecorded:function(a){root.attempt(function(){root.add(a)})}}
  CodeArea {width:parent.width;hint:"Text / paste block";text:root.pasteText;onEdited:function(v){root.pasteText=v}}
  Flow {width:parent.width;spacing:Style.space(6)
    Ui.Button {text:"Add Text/Paste";onClicked:root.attempt(function(){root.add(Workflow.paste(root.pasteText))})}
    Ui.Button {text:"Add wait";onClicked:root.attempt(function(){root.flow=Model.append(root.flow,Workflow.wait(root.delay),0)})}
  }
  Repeater {model:root.flow;Row {required property var modelData;required property int index;width:parent.width;spacing:Style.space(5)
    Ui.Button {text:"↑";onClicked:root.reorder(parent.index,-1)}
    Ui.Button {text:"↓";onClicked:root.reorder(parent.index,1)}
    Ui.Button {text:(parent.index+1)+" · "+parent.modelData.type;onClicked:root.selected=parent.index}
    Ui.Button {text:"×";onClicked:{var a=root.flow.slice();a.splice(parent.index,1);root.flow=a;root.selected=Math.min(root.selected,a.length-1)}}
  }}
  CodeArea {width:parent.width;visible:root.flow.length>0;text:root.flow[root.selected]?JSON.stringify(root.flow[root.selected],null,2):"";hint:"Selected action JSON";onEdited:function(v){root.attempt(function(){var a=JSON.parse(v);if(!a||typeof a.type!=="string")throw Error("Expected an action");var list=root.flow.slice();list[root.selected]=a;root.flow=list})}}
  Flow {width:parent.width;spacing:Style.space(6)
    Ui.Button {text:"Replay flow";enabled:root.flow.length>0;onClicked:root.service.later(Workflow.copy(root.flow))}
    Ui.Button {text:"Export JSON…";enabled:root.flow.length>0;onClicked:root.service.exportActions(root.name,root.flow,false)}
    Ui.Button {text:"Append JSON…";enabled:root.flow.length>0;onClicked:root.service.exportActions(root.name,root.flow,true)}
    Ui.Button {text:"Clear flow";onClicked:root.flow=[]}
  }
  Text {visible:root.error!=="";width:parent.width;wrapMode:Text.Wrap;text:root.error;textFormat:Text.PlainText;color:Commons.Color.urgent;font.family:Style.font.family;font.pixelSize:Style.font.caption}
}
