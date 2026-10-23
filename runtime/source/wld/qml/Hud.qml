import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons
import qs.Commons as Commons
import "model/Workflow.js" as Workflow
import "model/KeyDisplay.js" as KeyDisplay

Item {
  id: root
  required property var service
  FontLoader {id:promptFont;source:Qt.resolvedUrl("../../res/promptfont.ttf")}
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
        Connections {
          target:root.service
          function onCursorChanged(){drawing.requestPaint()}
          function onEnvironmentChanged(){drawing.requestPaint()}
          function onRadiusChanged(){drawing.requestPaint()}
          function onAngleChanged(){drawing.requestPaint()}
          function onUnitChanged(){drawing.requestPaint()}
          function onCircleOriginChanged(){drawing.requestPaint()}
          function onPageChanged(){drawing.requestPaint()}
          function onMonitorsChanged(){drawing.requestPaint()}
          function onCoordinateScaleChanged(){drawing.requestPaint()}
          function onQuickCircleChanged(){drawing.requestPaint()}
        }
        Connections {
          target:Commons.Color
          function onAccentChanged(){drawing.requestPaint()}
          function onForegroundChanged(){drawing.requestPaint()}
        }
        onWidthChanged:requestPaint()
        onHeightChanged:requestPaint()
        onPaint:{
          var c=getContext("2d");c.reset();var v=window.view,p=root.service.cursor,g=window.geometry
          if(!g)return
          var scale=root.service.coordinateScale
          function project(point){return Workflow.displayPoint(point,g,scale)}
          c.lineWidth=1;c.strokeStyle=Qt.rgba(Commons.Color.accent.r,Commons.Color.accent.g,Commons.Color.accent.b,0.5)
          if(v.grid&&!v.coordinates_only){c.beginPath();for(var x=50;x<width;x+=50){c.moveTo(x,0);c.lineTo(x,height)}for(var y=50;y<height;y+=50){c.moveTo(0,y);c.lineTo(width,y)}c.stroke();var fs=Style.font.caption;c.font=fs+"px "+Style.font.family;c.textAlign="center";var px2=Style.spacing.controlPaddingX,py2=Style.spacing.controlPaddingY,bgC=Qt.rgba(Commons.Color.background.r,Commons.Color.background.g,Commons.Color.background.b,1),fgC=Qt.rgba(Commons.Color.foreground.r,Commons.Color.foreground.g,Commons.Color.foreground.b,1),rowY=50;c.textBaseline="bottom";for(var col=0;col*50<width;col++){var cx=col*50+25,lbl="",n=col;do{lbl=String.fromCharCode(97+n%26)+lbl;n=Math.floor(n/26)-1}while(n>=0);var tw=c.measureText(lbl).width,bw=tw+px2*2,bh=fs+py2*2;c.fillStyle=bgC;c.fillRect(cx-bw/2,rowY-bh,bw,bh);c.fillStyle=fgC;c.fillText(lbl,cx,rowY-py2)}c.textBaseline="top";for(var col=0;col*50<width;col++){var cx=col*50+25,coord=String(col*50+25);var tw=c.measureText(coord).width,bw=tw+px2*2,bh=fs+py2*2;c.fillStyle=bgC;c.fillRect(cx-bw/2,rowY,bw,bh);c.fillStyle=fgC;c.fillText(coord,cx,rowY+py2)}c.textBaseline="middle";for(var row=0;row*50<height;row++){var cy=row*50+25,lbl="",n=row;do{lbl=String.fromCharCode(97+n%26)+lbl;n=Math.floor(n/26)-1}while(n>=0);var tw=c.measureText(lbl).width,bw=tw+px2*2,bh=fs+py2*2;c.fillStyle=bgC;c.fillRect(50-bw/2,cy-bh/2,bw,bh);c.fillStyle=fgC;c.fillText(lbl,50,cy)}}
          if(v.crosshair&&!v.coordinates_only&&window.containsPointer){c.beginPath();if(v.big_crosshair){c.moveTo(0,window.pointerY);c.lineTo(width,window.pointerY);c.moveTo(window.pointerX,0);c.lineTo(window.pointerX,height)}else{c.moveTo(window.pointerX-14,window.pointerY);c.lineTo(window.pointerX+14,window.pointerY);c.moveTo(window.pointerX,window.pointerY-14);c.lineTo(window.pointerX,window.pointerY+14)}c.stroke()}
          if(!v.coordinates_only){
            var points=root.service.environment.points||[];c.fillStyle=Commons.Color.accent;c.font=labelFont
            points.forEach(function(pt,i){if(pt){var p=project(pt),px=p[0],py=p[1];c.beginPath();c.arc(px,py,4,0,Math.PI*2);c.stroke();c.fillText(String(i+1),px+7,py-5)}})
            var quick=root.service.quickCircle
            if(quick||root.service.page==="Pointer"){
              var origin=quick?quick.origin:root.service.circleOrigin,target=quick?(quick.target||quick.origin):root.service.circleTarget,radius=quick?quick.radius:root.service.radius
              var o=project(origin),t=project(target)
              c.beginPath();c.arc(o[0],o[1],radius/scale,0,Math.PI*2);c.moveTo(o[0],o[1]);c.lineTo(t[0],t[1]);c.stroke()
              if(quick&&quick.zoneStart){var corner=project(quick.zoneStart),end=project(quick.target||quick.origin);c.strokeRect(corner[0],corner[1],end[0]-corner[0],end[1]-corner[1])}
            }
            Object.keys(root.service.environment.zones||{}).forEach(function(k){var r=root.service.environment.zones[k];if(r){var a=project(r.slice(0,2)),b=project(r.slice(2));c.strokeRect(a[0],a[1],b[0]-a[0],b[1]-a[1]);c.fillText(k,a[0]+4,a[1]+14)}})
          }
        }
      }
      Rectangle {
        visible:window.view.coordinates&&window.containsPointer
        x:Math.min(Math.max(0,window.pointerX+Style.space(18)),Math.max(0,window.width-width));y:Math.min(Math.max(0,window.pointerY+Style.space(18)),Math.max(0,window.height-height))
        width:tag.implicitWidth+Style.spacing.controlPaddingX*2;height:tag.implicitHeight+Style.spacing.controlPaddingY*2;radius:Style.cornerRadius;color:Commons.Color.background
        Text {id:tag;anchors.centerIn:parent;textFormat:Text.PlainText;text:Math.round(root.service.physicalCursor.x)+", "+Math.round(root.service.physicalCursor.y);color:Commons.Color.foreground;font.family:Style.font.family;font.pixelSize:Style.font.caption}
      }
      Rectangle {
        visible:root.service.keysEnabled&&root.service.keyRows.length>0&&window.containsPointer
        x:Math.max(0,Math.min(window.pointerX+width+Style.space(18)>window.width?window.pointerX-width-Style.space(18):window.pointerX+Style.space(18),window.width-width))
        y:Math.max(0,Math.min(window.pointerY-height-Style.space(12),window.height-height))
        width:Math.max(0,Math.min(window.width-Style.space(16),keyStack.implicitWidth+Style.spacing.controlPaddingX*2))
        height:keyStack.implicitHeight+Style.spacing.controlPaddingY*2
        color:"transparent";clip:true
        Column {
          id:keyStack
          x:Style.spacing.controlPaddingX;y:Style.spacing.controlPaddingY;spacing:Style.space(3)
          Repeater {
            model:root.service.keyRows
            Row {
              required property var modelData
              opacity:modelData.opacity;spacing:Style.space(3)
              Repeater {
                model:parent.modelData.keys
                Rectangle {
                  required property string modelData
                  width:keyLabel.implicitWidth+Style.space(10);height:keyLabel.implicitHeight+Style.space(6)
                  color:Commons.Color.background;radius:Style.cornerRadius
                  Text {
                    id:keyLabel;anchors.centerIn:parent;text:KeyDisplay.glyph(parent.modelData);textFormat:Text.PlainText
                    color:Commons.Color.foreground
                    font.family:promptFont.status===FontLoader.Ready?promptFont.name:Style.font.family
                    font.pixelSize:Style.font.subtitle
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
