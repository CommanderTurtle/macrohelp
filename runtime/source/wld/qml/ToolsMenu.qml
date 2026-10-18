import QtQuick
import QtQuick.Controls as QQC
import qs.Commons
import qs.Commons as Commons

QQC.Menu {
  font.family:Style.font.family
  font.pixelSize:Style.font.body
  palette.window:Commons.Color.background
  palette.base:Commons.Color.background
  palette.button:Commons.Color.background
  palette.text:Commons.Color.foreground
  palette.buttonText:Commons.Color.foreground
  palette.windowText:Commons.Color.foreground
  palette.highlight:Commons.Color.accent
  palette.highlightedText:Commons.Color.background
}
