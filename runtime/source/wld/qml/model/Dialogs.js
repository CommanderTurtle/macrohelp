// Dialog state only. Desktop input, persistence and tools remain in Service.
var slots = ["Z", "X", "C", "V"]
function clone(value) { return JSON.parse(JSON.stringify(value)) }
function point(cursor) { return [Math.round(cursor.x), Math.round(cursor.y)] }
function open(kind, cursor, pendingZone) {
  return {kind:kind, stage:kind === "save_cursor" || kind === "record_keys" ? "Name" : kind === "circle_placer" ? "Distance" : "Command",
    text:"", name:"", error:"", slot:"Z", origin:point(cursor), points:[point(cursor)], radius:0, angle:0, unit:"degrees",
    target:null, zoneStart:null, pendingZone:pendingZone || null, steps:[], delay:200, capturing:false, captured:[], held:[], captureReady:false}
}
function field(s, stage, text) { s.stage=stage; s.text=text===undefined?"":String(text); s.error="" }
function commandStage(s) { return ["Command","Mode","Confirm","ZoneAction","PasteSelect","ZoneCommand","ZonePlaySelect","ZonePlayFlow","ZoneCaptureZoneSelect","ZoneCaptureFlow","ZoneCaptureTarget","ZoneSelect"].indexOf(s.stage)>=0 }
function mouseDismiss(s) { return !s.capturing && (s.kind==="save_cursor" || s.kind==="record_keys" ? s.stage==="Name" : s.kind!=="registry_hub" || s.stage==="Command") }
function preview(s, W) {
  s=clone(s);s.text=String(s.text)
  try {
    if(s.kind==="circle_placer" && s.stage==="Distance") {
      s.target=null
      if(/[ij@,]/i.test(s.text)) { var v=W.vector(s.text,s.origin); s.radius=v.radius; s.angle=v.angle; s.unit="radians"; s.target=v.target }
      else s.radius=W.numeric(W.expression(s.text),0.000001,100000,"Radius")
    } else if(s.kind==="circle_placer" && s.stage==="Angle") {
      s.target=null;s.angle=W.numeric(W.expression(s.text),-360000,360000,"Angle")
      var a=s.unit==="radians"?s.angle:s.angle*Math.PI/180
      // C++ mathematical angles: +j points up. No screen-coordinate scaling here.
      s.target=[Math.round(s.origin[0]+s.radius*Math.cos(a)),Math.round(s.origin[1]-s.radius*Math.sin(a))]
    }
    s.error=""
  } catch(e) { s.error=s.text.trim()?e.message:"" }
  return s
}
function addStep(s, keys) {
  if(!keys.length)throw Error("Choose a key")
  if(s.steps.length>=10000)throw Error("Sequence limit reached")
  var last=s.steps[s.steps.length-1]
  if(!last || last.kind!=="keys" || last.keys.join("+")!==keys.join("+"))s.steps.push({kind:"keys",keys:keys.slice()})
}
function recorded(s, sample, name) {
  if(!s.capturing)return s
  s=clone(s)
  if(!sample.available || sample.locked){s.capturing=false;s.error="Key capture unavailable";return s}
  // Wait for the command key/button to release before accepting a new chord.
  var events=sample.events||[]
  if(!s.captureReady){
    if((sample.held||[]).some(function(code){return code===44||code===28}))return s
    events=events.filter(function(e){return e[0]!==44&&e[0]!==28})
    s.captureReady=true
  }
  events.forEach(function(e){
    var key=name(e[0]),at=s.held.indexOf(e[0])
    if(e[1]===1){if(at<0)s.held.push(e[0]);if(s.captured.indexOf(key)<0)s.captured.push(key)}
    else if(at>=0)s.held.splice(at,1)
  })
  s.held=(sample.held||[]).slice()
  if(s.captured.length&&!s.held.length){addStep(s,s.captured);s.capturing=false;s.captured=[];field(s,"Command")}
  return s
}
function description(s, env) {
  var title={save_cursor:"Save Cursor",paste_buffers:"Paste Buffers",registry_hub:"Registry Hub",circle_placer:"Circle Placer",click_left:"Left Click",click_right:"Right Click",click_middle:"Middle Click",stop_all:"Stop All",view_toggles:"Display",record_keys:"Record Keys"}[s.kind]||s.kind
  var hints={Name:"Name",Distance:"Radius, -20i+20j, or 316@-2i+2j",Mode:"R · radians   D · degrees",Angle:s.unit==="radians"?"Radians · pi/2":"Degrees",Confirm:"Z · move   X · append   C · next circle   V · zone",ZoneAction:"Z · snip   X · text   C · single line   V · copy   B · store zone",
    Edit:"Buffer "+s.slot+" · Shift+Enter for a new line",PasteSelect:"Z / X / C / V · paste   B · sequence",PasteSequence:"Buffer order · Z,X,C,V",ZoneCommand:"Z / X / C / V · edit   B · play",ZonePlaySelect:"Z / X / C / V · choose zone   B · capture to buffer",ZonePlayFlow:"Z · snip   X · text   C · single line   V · copy",ZoneCaptureZoneSelect:"Z / X / C / V · source zone",ZoneCaptureFlow:"X · text   C · single line   V · copy",ZoneCaptureTarget:"Z / X / C / V · destination buffer",ZoneEdit:"x1,y1,x2,y2 · two point numbers · saved zone",ZoneSelect:"Z / X / C / V · edit zone",Point:"x,y · point number · S to save current cursor",Paste:"Paste text · Shift+Enter for a new line",HoldMs:"Hold milliseconds · 1–600000",Script:"Workflow · Shift+Enter for a new line",Manual:"Choose up to three keys per step",RegistryPath:"Registry file path",ExportPath:"Export file path",ImportPath:"Import file path",Shell:"Shell "+s.slot,Var:"Variable "+s.slot}
  if(s.stage==="Command") {
    if(s.kind==="save_cursor")hints.Command="Z · save   X · add point   C · save + click   V · cancel"
    else if(s.kind==="paste_buffers")hints.Command="Z / X / C / V · edit   B · paste   N · zones"
    else if(s.kind==="registry_hub")hints.Command="Z · script   P · play   L · clear   Q · display   H · help\nA/S/D/F · vars   1–16 · points   N · zones\nX/C/G · shells   R · registry   E/I · export/import path"
    else if(s.kind==="record_keys")hints.Command="Z · capture   X · manual   C · paste   Enter · write"
    else if(s.kind==="view_toggles")hints.Command="Z · crosshair   X · grid   C · coords only   K · keys"
    else if(s.kind==="stop_all")hints.Command="Enter · stop all workflows"
    else hints.Command="Z · click in 1s   X · hold ms   C · write JSON"
  }
  var shortcuts=[]
  function buttons(keys,labels){shortcuts=keys.map(function(k,i){return {key:k,label:labels?labels[i]:k}})}
  if(commandStage(s)){
    if(s.stage==="Mode")buttons(["r","d"],["Radians","Degrees"])
    else if(s.stage==="Confirm")buttons(["z","x","c","v"],["Move","Append","Next circle","Zone"])
    else if(s.stage==="ZoneAction")buttons(["z","x","c","v","b"],["Snip","Text","Single line","Copy","Store zone"])
    else if(s.stage==="ZonePlayFlow")buttons(["z","x","c","v"])
    else if(s.stage==="ZoneCaptureFlow")buttons(["x","c","v"])
    else if(s.stage==="Command"&&s.kind==="save_cursor")buttons(["z","x","c"],["Save","Add point","Save + click"])
    else if(s.stage==="Command"&&s.kind==="record_keys")buttons(["z","x","c"],["Capture","Manual","Paste"])
    else if(s.stage==="Command"&&s.kind==="view_toggles")buttons(["z","x","c","k"],["Crosshair","Grid","Coords only","Keys"])
    else if(s.stage==="Command"&&/^click_/.test(s.kind))buttons(["z","x","c"],["Click","Hold ms","Write JSON"])
    else if(s.stage==="Command"&&s.kind==="registry_hub")buttons(["z","p","q","h"],["Script","Play","Display","Help"])
    else if(s.kind==="paste_buffers"||s.stage==="ZoneSelect") {buttons(["z","x","c","v"]);if(s.stage==="Command"||s.stage==="PasteSelect"||s.stage==="ZoneCommand"||s.stage==="ZonePlaySelect")shortcuts.push({key:"b",label:s.stage==="Command"?"Paste":s.stage==="PasteSelect"?"Sequence":s.stage==="ZoneCommand"?"Play":"Capture"});if(s.stage==="Command")shortcuts.push({key:"n",label:"Zones"})}
  }
  return {title:title+(s.name?" · "+s.name:""),hint:s.capturing?"Press a chord, then release it":hints[s.stage]||s.stage,buttons:shortcuts,
    multiline:["Edit","Script","Var","Paste","Help"].indexOf(s.stage)>=0,readonly:s.stage==="Help"||s.capturing,command:commandStage(s),manual:s.stage==="Manual"}
}
function submit(original, text, env, cursor, W) {
  var s=clone(original),effects=[],close=false,swap="",raw=String(text),cmd=raw.trim().toLowerCase(),key=cmd.toUpperCase()
  s.error=""
  function change(section,k,value){effects.push({kind:"change",section:section,key:k,value:value})}
  function queue(actions){effects.push({kind:"queue",actions:s.kind==="circle_placer"?actions:[W.move(s.origin,0)].concat(actions)});close=true}
  function output(name,actions){effects.push({kind:"export",name:name,actions:actions})}
  function back(){field(s,"Command")}
  function zone(method,rect,target){effects.push({kind:"zone",method:method,rect:rect,target:target||""});close=true}
  var literal=["Name","Edit","Script","Var","RegistryPath","ExportPath","ImportPath","Shell","Paste"].indexOf(s.stage)>=0
  if(!literal&&(cmd==="esc"||cmd==="cancel"))return {state:s,effects:[],close:true}
  if(s.kind==="save_cursor") {
    if(s.stage==="Name"){if(!raw.trim())throw Error("Enter a name");s.name=raw.trim();back()}
    else if(["x","add","point"].indexOf(cmd)>=0){s.points.push(point(cursor));s.text=""}
    else if(["","z","save","c","click"].indexOf(cmd)>=0){var movement=W.move(s.points[0],1000);movement.cursorMovsId=s.name;movement.cursormovsmap=s.points.map(function(p){return [0,1000,p[0],p[1]]});var a=[movement,W.wait(200)];if(cmd==="c"||cmd==="click")a.push(W.keys("LEFT_MOUSE",100),W.wait(200));output(s.name,a)}
    else if(cmd==="v")close=true
    else throw Error("Use Z, X, C or V")
  } else if(/^click_/.test(s.kind)) {
    var button={click_left:"LEFT_MOUSE",click_right:"RIGHT_MOUSE",click_middle:"MIDDLE_MOUSE"}[s.kind]
    if(s.stage==="HoldMs")queue([W.keys(button,Math.round(W.numeric(raw,1,600000,"Hold")))])
    else if(cmd==="z"||cmd==="click")queue([W.keys(button,100)])
    else if(cmd==="x"||cmd==="hold")field(s,"HoldMs","100")
    else if(cmd==="c"||cmd==="write"||cmd==="json")output(button,[W.keys(button,100),W.wait(200)])
    else throw Error("Use Z, X or C")
  } else if(s.kind==="stop_all") {effects.push({kind:"stop"});close=true}
  else if(s.kind==="view_toggles") {
    var toggles={z:"crosshair",crosshair:"crosshair",x:"grid",grid:"grid",c:"coordinates_only",coords:"coordinates_only",k:"keys",keys:"keys"}
    if(cmd==="v")close=true
    else if(toggles[cmd]){change("view",toggles[cmd],!env.view[toggles[cmd]]);close=true}
    else throw Error("Use Z, X, C or K")
  } else if(s.kind==="circle_placer") {
    if(s.stage==="Distance"||s.stage==="Angle") {
      s.text=raw;s=preview(s,W)
      if(s.error||(!s.target&&s.stage==="Angle")||!(s.radius>0))throw Error(s.error||"Enter a distance")
      field(s,s.target?"Confirm":"Mode")
    } else if(s.stage==="Mode") {
      if(["r","rad","radians","d","deg","degrees"].indexOf(cmd)<0)throw Error("Use R or D")
      s.unit=cmd[0]==="r"?"radians":"degrees";field(s,"Angle")
    } else if(s.stage==="Confirm") {
      if(cmd==="z"||cmd==="move"||cmd==="1")queue([W.move(s.target,220)])
      else if(cmd==="x"||cmd==="append"||cmd==="2")output("Circle",[W.move(s.target,1000),W.wait(200)])
      else if(cmd==="c"||cmd==="circle"||cmd==="v"||cmd==="zone"||cmd==="zonemake") {
        if(cmd==="v"||cmd==="zone"||cmd==="zonemake") {
          if(s.zoneStart){s.pendingZone=s.zoneStart.concat(s.target);field(s,"ZoneAction");return {state:s,effects:effects,close:false}}
          s.zoneStart=s.target.slice()
        }
        s.origin=s.target.slice();s.target=null;s.radius=0;field(s,"Distance")
      } else throw Error("Use Z, X, C or V")
    } else if(s.stage==="ZoneAction") {
      if(cmd==="b"||cmd==="buffer"){effects.push({kind:"zone-router",rect:s.pendingZone});close=true}
      else if(slots.indexOf(key)>=0)zone(key,s.pendingZone)
      else throw Error("Use Z, X, C, V or B")
    }
  } else if(s.kind==="paste_buffers") {
    if(s.stage==="Command") {
      if(slots.indexOf(key)>=0){s.slot=key;field(s,"Edit",env.buffers[key]||"")}
      else if(cmd==="b"||cmd==="paste"||cmd==="pasteone")field(s,"PasteSelect")
      else if(cmd==="n"||cmd==="zone"||cmd==="zones")field(s,"ZoneCommand")
      else throw Error("Use Z, X, C, V, B or N")
    } else if(s.stage==="Edit") {change("buffers",s.slot,raw);close=true}
    else if(s.stage==="PasteSequence")queue(W.pasteSequence(raw.toUpperCase().split(/[,;\s]+/).filter(Boolean),env))
    else if(s.stage==="PasteSelect") {
      if(cmd==="b"||cmd==="sequence"||cmd==="list")field(s,"PasteSequence")
      else if(slots.indexOf(key)>=0)queue(W.pasteSequence([key],env))
      else throw Error("Choose Z, X, C or V")
    } else if(s.stage==="ZoneCommand") {
      if(slots.indexOf(key)>=0){s.slot=key;if(s.pendingZone){change("zones",key,s.pendingZone);close=true}else field(s,"ZoneEdit",(env.zones[key]||[]).join(","))}
      else if(cmd==="b"||cmd==="play"||cmd==="buffer")field(s,"ZonePlaySelect")
      else throw Error("Choose Z, X, C, V or B")
    } else if(s.stage==="ZoneEdit") {change("zones",s.slot,W.zoneValue(raw,env));close=true}
    else if(s.stage==="ZonePlaySelect"||s.stage==="ZoneCaptureZoneSelect") {
      if(s.stage==="ZonePlaySelect"&&(cmd==="b"||cmd==="clip"||cmd==="capture"))field(s,"ZoneCaptureZoneSelect")
      else if(slots.indexOf(key)>=0&&env.zones[key]){s.slot=key;field(s,s.stage==="ZonePlaySelect"?"ZonePlayFlow":"ZoneCaptureFlow")}
      else throw Error("Choose a saved zone")
    } else if(s.stage==="ZonePlayFlow") {if(slots.indexOf(key)<0)throw Error("Choose Z, X, C or V");zone(key,env.zones[s.slot])}
    else if(s.stage==="ZoneCaptureFlow") {if(["X","C","V"].indexOf(key)<0)throw Error("Choose X, C or V");s.method=key;field(s,"ZoneCaptureTarget")}
    else if(s.stage==="ZoneCaptureTarget") {if(slots.indexOf(key)<0)throw Error("Choose Z, X, C or V");zone(s.method,env.zones[s.slot],key)}
  } else if(s.kind==="registry_hub") {
    if(s.stage==="Command") {
      if(cmd==="z"||cmd==="script")field(s,"Script",env.script||"")
      else if(cmd==="p"||cmd==="play"){effects.push({kind:"play",origin:s.origin});close=true}
      else if(cmd==="l"||cmd==="clear"){effects.push({kind:"clear"});back()}
      else if(cmd==="q"||cmd==="view"||cmd==="display"){swap="view_toggles"}
      else if(cmd==="h"||cmd==="?")field(s,"Help",help())
      else if(cmd==="r")field(s,"RegistryPath",env.registry||"")
      else if(cmd==="e"||cmd==="i")field(s,cmd==="e"?"ExportPath":"ImportPath",(env.paths||{})[cmd==="e"?"export":"import"]||"")
      else if(/^(?:shell\s+)?[xcgpt]$/i.test(cmd)&&cmd!=="p"){s.slot=key.slice(-1);field(s,"Shell",env.shells[s.slot]||"")}
      else if(/^[ASDF]$/.test(key)){s.slot=key;field(s,"Var",env.vars[key]||"")}
      else if(/^(?:point\s+)?(?:[1-9]|1[0-6])$/.test(cmd)){s.slot=String(Number(cmd.replace(/^point\s+/,""))-1);field(s,"Point",(env.points[Number(s.slot)]||[]).join(","))}
      else if(cmd==="n")field(s,"ZoneSelect")
      else if(cmd==="v")close=true
      else throw Error("Choose a registry command")
    } else if(s.stage==="ZoneSelect") {if(slots.indexOf(key)<0)throw Error("Choose Z, X, C or V");s.slot=key;field(s,"ZoneEdit",(env.zones[key]||[]).join(","))}
    else if(s.stage==="Help")back()
    else {
      if(s.stage==="Script")change("","script",raw)
      else if(s.stage==="RegistryPath")change("","registry",raw)
      else if(s.stage==="ExportPath"||s.stage==="ImportPath")change("paths",s.stage==="ExportPath"?"export":"import",raw)
      else if(s.stage==="Shell")change("shells",s.slot,raw)
      else if(s.stage==="Var")change("vars",s.slot,raw)
      else if(s.stage==="Point")change("points",Number(s.slot),["s","save","cursor"].indexOf(cmd)>=0?point(cursor):W.pointValue(raw,env))
      else if(s.stage==="ZoneEdit")change("zones",s.slot,W.zoneValue(raw,env))
      back()
    }
  } else if(s.kind==="record_keys") {
    if(s.stage==="Name"){if(!raw.trim())throw Error("Enter a name");s.name=raw.trim();back()}
    else if(s.stage==="Paste"){s.steps.push({kind:"paste",text:raw});back()}
    else if(s.stage==="Command") {
      if(cmd==="z"||cmd==="capture"){s.capturing=true;s.captureReady=false;s.captured=[];s.held=[];s.text=""}
      else if(cmd==="x"||cmd==="manual"){s.manualStart=s.steps.length;field(s,"Manual")}
      else if(cmd==="c"||cmd==="paste")field(s,"Paste")
      else if(cmd==="v")close=true
      else if(cmd===""||cmd==="write"){
        if(!s.steps.length)throw Error("Record a step first")
        var delay=W.numeric(s.delay,10,600000,"Step delay"),actions=[]
        var keysmap={},at=0
        function flush(){if(Object.keys(keysmap).length){actions.push({type:"keyssequence",keysmap:keysmap,keysSeqId:s.name,loop:1},W.wait(200));keysmap={};at=0}}
        s.steps.forEach(function(step){if(step.kind==="paste"){flush();actions.push(W.paste(step.text),W.wait(200))}else{keysmap[String(at)]=[100,step.keys.map(W.keyName)];at+=delay}})
        flush()
        output(s.name,actions)
      } else throw Error("Use Z, X, C or Enter")
    }
  }
  return {state:s,effects:effects,close:close,swap:swap}
}
function help() {
  return "Registry Hub\nZ script · P play · L clear local workflow · Q display · R registry file · E/I export/import paths\nA/S/D/F variables · 1–16 points · N zones · X/C/G shells · shell P/T additional shell settings\n\nWorkflow\n{point 1} {paste Z} {buf Z,X} {enter Z} {click on} {click off}\n{type text} {wait 200} {CTRL+S} {left} {right} {middle} {wheelup 3} {wheeldown 3}\n{set A value} {set paste Z text} {set point 1 x,y} {set zone Z x1,y1,x2,y2}\n{zone Z,X} {zonebuf Z,X,V} {rgdclip reference} {rgdappend reference}\n{terminal} {bash} {pwsh} {cmd} {python} {shell X}\n{play schedule} {profile name} {if A contains text} {else} {endif}\n\n$A/$S/$D/$F variables · $Z/$X/$C/$V buffers · $1…$16 points · $REGISTRY · $REGEDITED\n\nEach explicit wait starts the next native task. Play builds that task from current values after the preceding task finishes; its first action is the wait. Stop All cancels remaining dispatches.\n\nShift+Enter adds a line. Enter saves the current field. Escape closes.\nZone methods: Z screenshot, X text, C single-line text, V select and copy. Desktop shortcuts are read from your existing compositor bindings.\nCircle: radius → R/D → angle; i is right, j is up; 316@-2i+2j scales the vector. V starts/ends a zone; B stores it in a named zone."
}
function categories() {
  return [
    {name:"Commons",keys:"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789".split("").concat(["SPACE","ENTER","TAB","BACKSPACE","ESCAPE"])},
    {name:"Modifiers",keys:["CONTROL_LEFT","CONTROL_RIGHT","SHIFT_LEFT","SHIFT_RIGHT","ALT_LEFT","ALT_GR","WINDOWS","FN"]},
    {name:"Mouse buttons",keys:["LEFT_MOUSE","RIGHT_MOUSE","MIDDLE_MOUSE","XBUTTON_1","XBUTTON_2","MOUSE_WHEEL_UP","MOUSE_WHEEL_DOWN"]},
    {name:"NumPad",keys:["NUMPAD_0","NUMPAD_1","NUMPAD_2","NUMPAD_3","NUMPAD_4","NUMPAD_5","NUMPAD_6","NUMPAD_7","NUMPAD_8","NUMPAD_9","NUMPAD_PLUS","NUMPAD_MINUS","NUMPAD_STAR","NUMPAD_SLASH","NUMPAD_DOT","NUMPAD_ENTER","NUM_LOCK"]},
    {name:"Specials",keys:["PRINT_SCREEN","PRINT","INSERT","DELETE"]},
    {name:"Navigations",keys:["UP_ARROW","DOWN_ARROW","LEFT_ARROW","RIGHT_ARROW","HOME","END","PAGE_UP","PAGE_DOWN"]},
    {name:"Functions",keys:["F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"]},
    {name:"Toggles",keys:["CAPS_LOCK","NUM_LOCK","SCROLL_LOCK"]},
    {name:"Misc chars",keys:["plus","minus","asterisk","slash","period","comma"]}
  ]
}
