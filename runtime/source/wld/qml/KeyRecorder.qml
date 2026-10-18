import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

Ui.BorderSurface {
  id:root
  property bool recording:false
  property double epoch:0
  property var held:({})
  property var events:({})
  signal recorded(var action)
  implicitHeight:Style.space(75)
  activeFocusOnTab:true
  color:Style.controlFill(activeFocus,recordArea.containsMouse,Commons.Color.foreground,Commons.Color.accent)
  borderSpec:Border.controlSpec(activeFocus?"focus":recordArea.containsMouse?"hover-cursor":"normal",Commons.Color.foreground,Commons.Color.accent)
  radius:Style.cornerRadius
  function key(event){var names={16777216:"ESCAPE",16777217:"TAB",16777219:"BACKSPACE",16777220:"ENTER",16777221:"ENTER",16777222:"INSERT",16777223:"DELETE",16777225:"PRINT_SCREEN",16777232:"HOME",16777233:"END",16777234:"LEFT_ARROW",16777235:"UP_ARROW",16777236:"RIGHT_ARROW",16777237:"DOWN_ARROW",16777238:"PAGE_UP",16777239:"PAGE_DOWN",16777248:"SHIFT_LEFT",16777249:"CONTROL_LEFT",16777250:"WINDOWS",16777251:"ALT_LEFT",32:"SPACE"};if(names[event.key])return names[event.key];if(event.key>=16777264&&event.key<=16777298)return "F"+(event.key-16777263);return String(event.key>=33&&event.key<=126?String.fromCharCode(event.key):event.text||"").toUpperCase()}
  function start(){held=({});events=({});epoch=Date.now();recording=true;forceActiveFocus()}
  function release(name){var start=held[name];if(start===undefined)return;var map=Object.assign({},events),at=String(start);while(map[at])at=String(Number(at)+1);map[at]=[Math.max(1,Date.now()-epoch-start),[name]];events=map;var next=Object.assign({},held);delete next[name];held=next}
  function finish(){Object.keys(held).forEach(function(k){root.release(k)});recording=false;if(Object.keys(events).length)recorded({type:"keyssequence",keysmap:events,keysSeqId:"",loop:1})}
  onActiveFocusChanged:if(!activeFocus&&recording)finish()
  Text {anchors.centerIn:parent;width:Math.max(0,parent.width-Style.spacing.controlPaddingX*2);wrapMode:Text.Wrap;horizontalAlignment:Text.AlignHCenter;textFormat:Text.PlainText;text:root.recording?"Recording · click to finish":"Click to record keys (focused only)";color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.body}
  MouseArea {id:recordArea;anchors.fill:parent;hoverEnabled:true;onClicked:root.recording?root.finish():root.start()}
  Keys.onPressed:function(event){if(!recording){if(event.key===Qt.Key_Return||event.key===Qt.Key_Enter||event.key===Qt.Key_Space){event.accepted=true;start()}return}event.accepted=true;if(event.isAutoRepeat)return;var name=key(event);if(name&&held[name]===undefined){var next=Object.assign({},held);next[name]=Date.now()-epoch;held=next}}
  Keys.onReleased:function(event){if(!recording)return;event.accepted=true;if(!event.isAutoRepeat)release(key(event))}
}
