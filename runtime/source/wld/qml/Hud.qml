import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import "model/Workflow.js" as Workflow

Item {
  id: root
  required property var service
  Variants {
    model: Quickshell.screens
    PanelWindow {
      id: window
      required property var modelData
      screen: modelData
      color: "transparent"
      anchors {top:true;bottom:true;left:true;right:true}
      exclusionMode: ExclusionMode.Ignore
      WlrLayershell.namespace: "macrohelp-hud"
      WlrLayershell.layer: WlrLayer.Overlay
      WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
      mask: Region {}
      visible: root.service.ready&&!root.service.hidden&&geometry!==null
      readonly property var geometry: root.service.monitors.find(function(m){return m.name===window.modelData.name})||null
      readonly property real pointerX: geometry?root.service.cursor.x-geometry.x:0
      readonly property real pointerY: geometry?root.service.cursor.y-geometry.y:0
      readonly property bool containsPointer: Workflow.containsPoint(root.service.cursor,geometry,width,height)
      readonly property var view: root.service.environment.view||({})
      Canvas {
        id: drawing
        anchors.fill:parent
        readonly property string labelFont:Style.font.caption+"px "+Style.font.family
        onLabelFontChanged:requestPaint()
        function paint(){requestPaint()}
        Connections {
          target:root.service
          function onCursorChanged(){drawing.paint()}
          function onEnvironmentChanged(){drawing.paint()}
          function onRadiusChanged(){drawing.paint()}
          function onAngleChanged(){drawing.paint()}
          function onUnitChanged(){drawing.paint()}
          function onCircleOriginChanged(){drawing.paint()}
          function onPageChanged(){drawing.paint()}
          function onMonitorsChanged(){drawing.paint()}
        }
        Connections {
          target:Commons.Color
          function onAccentChanged(){drawing.paint()}
          function onForegroundChanged(){drawing.paint()}
        }
        onWidthChanged:requestPaint()
        onHeightChanged:requestPaint()
        onPaint:{
          var c=getContext("2d");c.reset();var v=window.view,p=root.service.cursor,g=window.geometry
          if(!g)return
          c.lineWidth=1;c.strokeStyle=Qt.rgba(Commons.Color.accent.r,Commons.Color.accent.g,Commons.Color.accent.b,0.5)
          if(v.grid&&!v.coordinates_only){c.beginPath();for(var x=50;x<width;x+=50){c.moveTo(x,0);c.lineTo(x,height)}for(var y=50;y<height;y+=50){c.moveTo(0,y);c.lineTo(width,y)}c.stroke()}
          if(v.crosshair&&!v.coordinates_only&&window.containsPointer){c.beginPath();c.moveTo(window.pointerX-14,window.pointerY);c.lineTo(window.pointerX+14,window.pointerY);c.moveTo(window.pointerX,window.pointerY-14);c.lineTo(window.pointerX,window.pointerY+14);c.stroke()}
          if(!v.coordinates_only){
            var points=root.service.environment.points||[];c.fillStyle=Commons.Color.accent;c.font=labelFont
            points.forEach(function(pt,i){if(pt){var px=pt[0]-g.x,py=pt[1]-g.y;c.beginPath();c.arc(px,py,4,0,Math.PI*2);c.stroke();c.fillText(String(i+1),px+7,py-5)}})
            if(root.service.page==="Pointer"){var origin=root.service.circleOrigin,target=root.service.circleTarget;c.beginPath();c.arc(origin[0]-g.x,origin[1]-g.y,root.service.radius,0,Math.PI*2);c.moveTo(origin[0]-g.x,origin[1]-g.y);c.lineTo(target[0]-g.x,target[1]-g.y);c.stroke()}
            Object.keys(root.service.environment.zones||{}).forEach(function(k){var r=root.service.environment.zones[k];if(r){c.strokeRect(r[0]-g.x,r[1]-g.y,r[2]-r[0],r[3]-r[1]);c.fillText(k,r[0]-g.x+4,r[1]-g.y+14)}})
          }
        }
      }
      Rectangle {
        visible:window.view.coordinates&&window.containsPointer
        x:Math.min(Math.max(0,window.pointerX+Style.space(18)),Math.max(0,window.width-width));y:Math.min(Math.max(0,window.pointerY+Style.space(18)),Math.max(0,window.height-height))
        width:tag.implicitWidth+Style.spacing.controlPaddingX*2;height:tag.implicitHeight+Style.spacing.controlPaddingY*2;radius:Style.cornerRadius;color:Commons.Color.background
        Text {id:tag;anchors.centerIn:parent;textFormat:Text.PlainText;text:Math.round(root.service.cursor.x)+", "+Math.round(root.service.cursor.y);color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
      }
      Rectangle {
        visible:window.view.keys&&!window.view.coordinates_only&&root.service.history.length>0&&window.containsPointer
        anchors {left:parent.left;bottom:parent.bottom;margins:Style.space(20)}
        width:Math.max(0,Math.min(window.width-Style.space(40),log.implicitWidth+Style.space(20)));height:log.implicitHeight+Style.space(16);color:Commons.Color.background;radius:Style.cornerRadius
        Text {id:log;anchors.centerIn:parent;width:Math.max(0,parent.width-Style.space(20));elide:Text.ElideRight;textFormat:Text.PlainText;text:root.service.history.join("\n");color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
      }
    }
  }
}
