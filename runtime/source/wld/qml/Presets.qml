import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

Column {
  id:root
  required property var service
  property string name:""
  property string selected:""
  property var fields:["script"]
  property bool replace:false
  spacing:Style.space(8)
  Component.onCompleted:service.loadProfiles()
  Ui.TextField {width:parent.width;placeholderText:"Preset name (e.g. 1 - Writing)";text:root.name;onTextEdited:root.name=text}
  Flow {
    width:parent.width;spacing:Style.space(8)
    Repeater {
      model:[{id:"script",label:"Code"},{id:"registry",label:"Rgd path"},{id:"vars.A",label:"Var A"},{id:"vars.S",label:"Var S"},{id:"vars.D",label:"Var D"},{id:"vars.F",label:"Var F"},{id:"buffers.Z",label:"Buffer Z"},{id:"buffers.X",label:"Buffer X"},{id:"buffers.C",label:"Buffer C"},{id:"buffers.V",label:"Buffer V"},{id:"zones.Z",label:"Zone Z"},{id:"zones.X",label:"Zone X"},{id:"zones.C",label:"Zone C"},{id:"zones.V",label:"Zone V"},{id:"points",label:"Points"},{id:"shells.X",label:"Shell X"},{id:"shells.C",label:"Shell C"},{id:"shells.G",label:"Shell G"},{id:"view",label:"View"}]
      Ui.Toggle {required property var modelData;label:modelData.label;checked:root.fields.indexOf(modelData.id)>=0;onClicked:root.fields=checked?root.fields.filter(function(f){return f!==modelData.id}):root.fields.concat([modelData.id])}
    }
  }
  Ui.Toggle {label:"Replace existing preset";checked:root.replace;onClicked:root.replace=!checked}
  Ui.Button {text:"Export preset";enabled:root.name.trim()!==""&&root.fields.length>0;onClicked:root.service.saveProfile(root.name,root.fields,root.replace)}
  Text {width:parent.width;wrapMode:Text.Wrap;text:root.service.profileLibrary.directory||"";textFormat:Text.PlainText;color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
  Ui.Dropdown {width:parent.width;label:"Saved preset";options:Object.keys(root.service.profileLibrary.presets||{});value:root.selected;onChanged:function(v){root.selected=v}}
  Flow {
    width:parent.width;spacing:Style.space(8)
    Ui.Button {text:"Import";enabled:root.selected!=="";onClicked:root.service.importProfile(root.selected)}
    Ui.Button {text:"Run";enabled:root.selected!==""&&root.service.connected&&!root.service.busy;onClicked:root.service.play("{profile "+root.selected+"}")}
    Ui.Button {text:"Refresh";onClicked:root.service.loadProfiles()}
  }
  Text {visible:root.service.profileError!=="";width:parent.width;wrapMode:Text.Wrap;text:root.service.profileError;textFormat:Text.PlainText;color:Commons.Color.urgent;font.family:Style.font.family;font.pixelSize:Style.font.body}
}
