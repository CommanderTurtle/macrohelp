import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui

Column {
  id:root
  required property var service
  spacing:Style.space(6)
  Flow {
    width:parent.width;spacing:Style.space(6)
    Ui.Button {text:root.service.bindingsRefreshing?"Reading hotkeys…":"Refresh hotkeys";enabled:root.service.ready&&!root.service.bindingsRefreshing;onClicked:root.service.refreshBindings()}
    Text {text:"Active desktop shortcuts";textFormat:Text.PlainText;color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
  }
  Text {visible:root.service.bindingsError!=="";width:parent.width;wrapMode:Text.Wrap;text:root.service.bindingsError;textFormat:Text.PlainText;color:Commons.Color.urgent;font.family:Style.font.family;font.pixelSize:Style.font.caption}
  Repeater {
    model:root.service.shortcutRows
    Text {required property var modelData;width:root.width;wrapMode:Text.Wrap;text:modelData.name+" · "+modelData.chord+(modelData.fallback?" · clipboard fallback":"");textFormat:Text.PlainText;color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
  }
}
