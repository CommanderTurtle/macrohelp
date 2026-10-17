import QtQuick
import QtQuick.Controls as QQC
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

QQC.ScrollView {
  id: root
  property alias text: editor.text
  property string hint: ""
  property bool pasting: false
  signal edited(string value)
  function pasteClipboard(callback) {
    editor.forceActiveFocus()
    Qt.callLater(function() {
      if(!editor.canPaste){if(callback)callback(null,"Clipboard has no text");return}
      var before=editor.text
      root.pasting=true;editor.selectAll();editor.paste()
      var value=editor.text
      if(value!==before)editor.undo()
      root.pasting=false
      if(callback)callback({text:value},null)
    })
  }
  implicitHeight: Style.space(160)
  clip:true
  QQC.TextArea {
    id:editor
    placeholderText:root.hint
    wrapMode:TextEdit.WrapAnywhere
    textFormat:TextEdit.PlainText
    selectByMouse:true
    color:Commons.Color.foreground
    font.family:Style.font.family
    font.pixelSize:Style.font.body
    background:Ui.BorderSurface{color:Qt.rgba(Commons.Color.foreground.r,Commons.Color.foreground.g,Commons.Color.foreground.b,0.04);radius:Style.cornerRadius}
    onTextChanged:if(activeFocus&&!root.pasting)root.edited(text)
  }
}
