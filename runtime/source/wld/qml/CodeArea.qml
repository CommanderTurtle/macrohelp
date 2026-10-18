import QtQuick
import QtQuick.Controls as QQC
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

QQC.ScrollView {
  id: root
  property alias text: editor.text
  property alias input: editor
  property string hint: ""
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
  implicitHeight: Style.space(160)
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
  }
}
