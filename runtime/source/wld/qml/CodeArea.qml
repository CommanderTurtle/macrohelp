import QtQuick
import QtQuick.Controls as QQC
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

QQC.ScrollView {
  id: root
  property alias text: editor.text
  property alias input: editor
  property alias readOnly: editor.readOnly
  property string hint: ""
  property bool submitOnReturn: false
  property bool captureKeys: false
  property real minimumHeight: Style.space(160)
  property real maximumHeight: Style.space(640)
  readonly property real naturalHeight: editor.contentHeight+editor.topPadding+editor.bottomPadding
  signal accepted()
  signal cancelled()
  signal edited(string value)
  function pasteClipboard(callback) {
    editor.forceActiveFocus()
    Qt.callLater(function() {
      if(!clipboard.canPaste){if(callback)callback(null,"Clipboard has no text");return}
      clipboard.clear();clipboard.paste()
      var value=clipboard.text
      clipboard.clear()
      if(callback)callback({text:value},null)
    })
  }
  implicitHeight: Math.min(maximumHeight,Math.max(minimumHeight,naturalHeight))
  contentWidth: availableWidth
  clip:true
  QQC.TextArea {
    id:editor
    TextEdit {id:clipboard;visible:false;textFormat:TextEdit.PlainText}
    placeholderText:root.hint
    wrapMode:TextEdit.WrapAnywhere
    textFormat:TextEdit.PlainText
    selectByMouse:true
    color:Commons.Color.foreground
    font.family:Style.font.family
    font.pixelSize:Style.font.body
    selectionColor:Style.selectionFillFor(Commons.Color.foreground,Commons.Color.accent)
    selectedTextColor:Commons.Color.foreground
    placeholderTextColor:Qt.darker(Commons.Color.foreground,1.6)
    leftPadding:Style.spacing.controlPaddingX+Border.left(fieldBorder)
    rightPadding:Style.spacing.controlPaddingX+Border.right(fieldBorder)
    topPadding:Style.spacing.inputPaddingY+Border.top(fieldBorder)
    bottomPadding:Style.spacing.inputPaddingY+Border.bottom(fieldBorder)
    readonly property var fieldBorder:Border.controlSpec(activeFocus?"focus":hovered?"hover-cursor":"normal",Commons.Color.foreground,Commons.Color.accent)
    background:Ui.BorderSurface{color:Style.controlFill(editor.activeFocus,editor.hovered,Commons.Color.foreground,Commons.Color.accent);borderSpec:editor.fieldBorder;radius:Style.cornerRadius}
    onTextChanged:if(activeFocus)root.edited(text)
    Keys.priority: Keys.BeforeItem
    Keys.onPressed:function(event){
      if(root.captureKeys){event.accepted=true;return}
      if(!root.submitOnReturn)return
      if(event.key===Qt.Key_Escape){event.accepted=true;root.cancelled()}
      else if((event.key===Qt.Key_Return||event.key===Qt.Key_Enter)&&!(event.modifiers&Qt.ShiftModifier)){event.accepted=true;root.accepted()}
    }
  }
}
