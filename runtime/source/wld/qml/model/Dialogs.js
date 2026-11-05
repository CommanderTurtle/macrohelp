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
  var hints={Name:"Name",Distance:"Radius, -20i+20j, or 316@-2i+2j",Mode:"R · radians   D · degrees",Angle:s.unit==="radians"?"Radians · pi/2":"Degrees",Confirm:"Z · move   X · diff zone   C · next circle   V · point zone",ZoneAction:"Z · screenshot   V · copy   X · OCR   C · single line   S · picker   B · store zone",
    Edit:"Buffer "+s.slot+" · Shift+Enter for a new line",PasteSelect:"Z / X / C / V · paste   B · sequence",PasteSequence:"Buffer order · Z,X,C,V",ZoneCommand:"Z / X / C / V · edit   B · play",ZonePlaySelect:"Z / X / C / V · choose zone   B · capture to buffer",ZonePlayFlow:"Z · screenshot   V · copy   X · OCR   C · single line   S · picker",ZoneCaptureZoneSelect:"Z / X / C / V · source zone",ZoneCaptureFlow:"X · text   C · single line   V · copy",ZoneCaptureTarget:"Z / X / C / V · destination buffer",ZoneEdit:"x1,y1,x2,y2 · two point numbers · saved zone",ZoneSelect:"Z / X / C / V · edit zone",Point:"x,y · point number · S to save current cursor",Paste:"Paste text · Shift+Enter for a new line",HoldMs:"Hold milliseconds · 1–600000",Script:"Workflow · Shift+Enter for a new line",Manual:"Choose up to three keys per step",RegistryPath:"Registry file path",ExportPath:"Export file path",ImportPath:"Import file path",Shell:"Shell "+s.slot,Var:"Variable "+s.slot}
  if(s.stage==="Command") {
    if(s.kind==="save_cursor")hints.Command="Z · save   X · add point   C · save + click   V · cancel"
    else if(s.kind==="paste_buffers")hints.Command="Z / X / C / V · edit   B · paste   N · zones"
    else if(s.kind==="registry_hub")hints.Command="Z · script   P · play   L · clear   Q · display   H · help\nA/S/D/F · vars   1–16 · points   N · zones\nX/C/G · shells   R · registry   E/I · export/import path"
    else if(s.kind==="record_keys")hints.Command="Z · capture   X · manual   C · paste   Enter · write"
    else if(s.kind==="view_toggles")hints.Command="Z · crosshair   X · grid   C · coords only   K · keys   V · bigger crosshair"
    else if(s.kind==="stop_all")hints.Command="Enter · stop all workflows"
    else hints.Command="Z · click in 1s   X · hold ms   C · write JSON"
  }
  var shortcuts=[]
  function buttons(keys,labels){shortcuts=keys.map(function(k,i){return {key:k,label:labels?labels[i]:k}})}
  if(commandStage(s)){
    if(s.stage==="Mode")buttons(["r","d"],["Radians","Degrees"])
    else if(s.stage==="Confirm")buttons(["z","x","c","v"],["Move","Diff zone","Next circle","Point zone"])
    else if(s.stage==="ZoneAction")buttons(["z","v","x","c","s","b"],["Screenshot zone","Copy zone","OCR zone","OCR single line","Screenshot picker","Store zone"])
    else if(s.stage==="ZonePlayFlow")buttons(["z","v","x","c","s"],["Screenshot zone","Copy zone","OCR zone","OCR single line","Screenshot picker"])
    else if(s.stage==="ZoneCaptureFlow")buttons(["x","c","v"])
    else if(s.stage==="Command"&&s.kind==="save_cursor")buttons(["z","x","c"],["Save","Add point","Save + click"])
    else if(s.stage==="Command"&&s.kind==="record_keys")buttons(["z","x","c"],["Capture","Manual","Paste"])
    else if(s.stage==="Command"&&s.kind==="view_toggles")buttons(["z","x","c","k","v"],["Crosshair","Grid","Coords only","Keys","Bigger crosshair"])
    else if(s.stage==="Command"&&/^click_/.test(s.kind))buttons(["z","x","c"],["Click","Hold ms","Write JSON"])
    else if(s.stage==="Command"&&s.kind==="registry_hub")buttons(["z","p","q","h"],["Script","Play","Display","Help"])
    else if(s.kind==="paste_buffers"||s.stage==="ZoneSelect") {buttons(["z","x","c","v"]);if(s.stage==="Command"||s.stage==="PasteSelect"||s.stage==="ZoneCommand"||s.stage==="ZonePlaySelect")shortcuts.push({key:"b",label:s.stage==="Command"?"Paste":s.stage==="PasteSelect"?"Sequence":s.stage==="ZoneCommand"?"Play":"Capture"});if(s.stage==="Command")shortcuts.push({key:"n",label:"Zones"})}
  }
  return {title:title+(s.name?" · "+s.name:""),hint:s.capturing?"Press a chord, then release it":hints[s.stage]||s.stage,buttons:shortcuts,
    multiline:["Edit","Script","Var","Paste","Help"].indexOf(s.stage)>=0,readonly:s.stage==="Help"||s.capturing,command:commandStage(s),manual:s.stage==="Manual"}
}
function submit(original, text, env, cursor, W, monitors) {
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
    var toggles={z:"crosshair",crosshair:"crosshair",x:"grid",grid:"grid",c:"coordinates_only",coords:"coordinates_only",k:"keys",keys:"keys",v:"big_crosshair",big_crosshair:"big_crosshair"}
    if(toggles[cmd]){var key=toggles[cmd],patch={};patch[key]=!env.view[key];var view=W.viewPatch(env.view,patch);Object.keys(view).forEach(function(k){if(view[k]!==env.view[k])change("view",k,view[k])});close=true}
    else throw Error("Use Z, X, C, K or V")
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
      else if(cmd==="x"||cmd==="append"||cmd==="2") {
        // Diff zone: the rectangle between where the circle was placed and
        // where the pointer ended up - the movement itself, made into a zone.
        // (V still means the two-centre zone, so the two stay distinct.)
        if(s.origin[0]===s.target[0]&&s.origin[1]===s.target[1])throw Error("Move to a different position first")
        s.pendingZone=s.origin.concat(s.target);field(s,"ZoneAction");return {state:s,effects:effects,close:false}
      }
      else if(cmd==="c"||cmd==="circle"||cmd==="v"||cmd==="zone"||cmd==="zonemake") {
        if(cmd==="v"||cmd==="zone"||cmd==="zonemake") {
          if(s.zoneStart){s.pendingZone=s.zoneStart.concat(s.target);field(s,"ZoneAction");return {state:s,effects:effects,close:false}}
          s.zoneStart=s.target.slice()
        }
        s.origin=s.target.slice();s.target=null;s.radius=0;field(s,"Distance")
      } else throw Error("Use Z, X, C or V")
    } else if(s.stage==="ZoneAction") {
      if(cmd==="b"||cmd==="buffer"){effects.push({kind:"zone-router",rect:s.pendingZone});close=true}
      else if(cmd==="s"||cmd==="screen")zone("S",s.pendingZone)
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
    } else if(s.stage==="ZoneEdit") {change("zones",s.slot,W.zoneValue(raw,env,monitors));close=true}
    else if(s.stage==="ZonePlaySelect"||s.stage==="ZoneCaptureZoneSelect") {
      if(s.stage==="ZonePlaySelect"&&(cmd==="b"||cmd==="clip"||cmd==="capture"))field(s,"ZoneCaptureZoneSelect")
      else if(slots.indexOf(key)>=0&&env.zones[key]){s.slot=key;field(s,s.stage==="ZonePlaySelect"?"ZonePlayFlow":"ZoneCaptureFlow")}
      else throw Error("Choose a saved zone")
    } else if(s.stage==="ZonePlayFlow") {if(slots.indexOf(key)<0&&key!=="S"&&cmd!=="screen")throw Error("Choose Z, V, X, C or S");zone(cmd==="screen"?"S":key,env.zones[s.slot])}
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
      else if(s.stage==="Point")if(["s","save","cursor"].indexOf(cmd)>=0){change("points",Number(s.slot),s.origin.slice());close=true}else change("points",Number(s.slot),W.pointValue(raw,env,monitors))
      else if(s.stage==="ZoneEdit")change("zones",s.slot,W.zoneValue(raw,env,monitors))
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
  return "{screen} opens the native screenshot picker without a zone drag. S selects it in zone method menus; Circle B still stores a zone. C single-line OCR uses a native shortcut when configured, otherwise a temporary shell P (pwsh) session updates the clipboard with spaces and exits both shells.\n\nRegistry Hub\nZ script · P play · L clear local workflow · Q display · R registry file · E/I export/import paths\nA/S/D/F variables · 1–16 points · N zones · X/C/G shells · shell P/T additional shell settings\n\nWorkflow\n{point 1} {paste Z} {buf Z,X} {enter Z} {click on} {click off}\n{type text} {wait 200} {CTRL+S} {left} {right} {middle} {wheelup 3} {wheeldown 3}\n{set A value} {set paste Z text} {set point 1 x,y} {set zone Z x1,y1,x2,y2}\n{zone Z,X} {zonebuf Z,X,V} {rgd i6s6} {rgd i6z6} {rgdclip reference} {rgdappend reference}\n{terminal} {bash} {pwsh} {cmd} {python} {shell X}\n{play schedule} {profile name} {if A contains text} {else} {endif}\n\n$A/$S/$D/$F variables · $Z/$X/$C/$V buffers · $1…$16 points · $REGISTRY · $REGEDITED\n\nEach explicit wait starts the next native task. Play builds that task from current values after the preceding task finishes; its first action is the wait. Stop All cancels remaining dispatches.\n\nShift+Enter adds a line. Enter saves the current field. Escape closes.\nZone methods: Z screenshot, X text, C single-line text, V select and copy. Desktop shortcuts are read from your existing compositor bindings.\nCircle: radius → R/D → angle; i is right, j is up; 316@-2i+2j scales the vector. At Confirm: Z moves the pointer, X zones from the placement spot to the cursor, C chains the next circle from the new position, V starts/ends a two-point zone; B stores the pending zone in a named zone.\n\nThe Full Registry Hub Field Manual\n===========================\n\nPurpose\n-------\nRegistry Hub is a Macrohelp command surface that translates small brace tokens into native Tasket schedules.\nIt should stay lightweight: Macrohelp builds Tasket JSON, sends it to tasket-httpd, and lets Tasket perform the real keyboard, mouse, paste, and schedule work.\nThe normal verb is Play. Low-level .scht saving exists only for debugging and future-proofing against Tasket format changes.\n\nRouter Keys Before Opening The Script Editor\n--------------------------------------------\nQ  Open the visibility/display toggles. In that screen, Z toggles the target, X toggles gridlines, C leaves only cursor coordinates, and K toggles key visibility.\nZ  Open the workflow script editor.\nP  Play the current workflow immediately, then hide the Registry Hub window on success.\nL  Clear Registry Hub script-local state. Paste buffers and zone buffers remain available.\nH  Open this help/manual screen.\nR  Set the registry file path used by scripts and exports.\nE  Set the plaintext export path. Default is %USERPROFILE%\\Desktop\\temps\\macrohelp-registry-hub-environment.txt.\nI  Set the plaintext import path. Default is the same Desktop\\temps file.\nX  Set shell alias X. Example contents: bash\nC  Set shell alias C. Blank resets to cmd.\nG  Set shell alias G. Example contents: uv run python\nA/S/D/F  Set script-local multiline variables. Shift+Enter adds lines; Enter saves.\nN  Set zone buffers Z/X/C/V as x1,y1,x2,y2, as two point references such as 1,2, or by copying another zone slot.\n1/2/3/4/5/6  Set visible important points as x,y. Type S while editing a point to save the current cursor coordinate.\nEsc  Close and preserve state.\n\nScript Editor Buttons\n---------------------\nClear      Clears Registry Hub script-local state and returns to a clean script editor.\nExport     Writes a plaintext environment file to the configured export path.\nImport     Reads a plaintext environment file from the configured import path.\nSave Schts Writes the compiled Tasket .scht file and manifest to Desktop\\temps for inspection.\nPlay       Schedules the compiled Tasket flow through tasket-httpd.\n\nVisibility Toggles From Q\n-------------------------\nQ opens the visibility screen from the Registry Hub router. It is the only router key for display toggles.\nInside Q, Z toggles the target/crosshair window.\nInside Q, X toggles the cursor-centered gridlines.\nInside Q, C toggles coordinates-only mode, leaving the coordinate tag while hiding target/grid drawing.\nInside Q, K toggles the key-press HUD visibility. This mirrors the tray option without leaving the keyboard flow.\n\nText Buffers And Variables\n--------------------------\nA/S/D/F are Registry Hub script variables. They are edited from the Registry Hub router.\nZ/X/C/V are the main paste buffers owned by Shift+Alt+1. Registry Hub can read or set them, but it does not replace that menu.\nZone buffers also use Z/X/C/V names, but they are separate physical coordinate ranges, not stored paste text.\n$A, $S, $D, $F, $Z, $X, $C, and $V resolve to the current buffer text inside {set ...} and condition tokens.\n$REGISTRY resolves to the configured registry file path. $SHELLG and $REGEDITED resolve to shell alias G.\n{buf A} or {buf A,Z,C} pastes listed buffers with small Tasket waits. By default it does not click first, so focused shells stay focused.\n{enter A,Z} pastes the listed buffers and presses Enter afterward. It also defaults to no-click.\n{paste A} is an explicit no-click paste token for readability.\n{type literal text} sends the literal remainder through Tasket's native paste action.\n{rgd i6s6} / {rgd i6z6} includes a string/zone from the Registry file. Run Command stages output briefly in the schedule directory; included waits and conditions work normally.\nBare {A}, {S}, {D}, {F}, {Z}, {X}, {C}, and {V} are keyboard strokes; use {buf ...}, {paste ...}, or {enter ...} for stored text.\n{click on} enables prepended focus-clicks for following {buf ...} and {enter ...} tokens.\n{click off} disables prepended focus-clicks again. Registry Hub starts in click-off mode every Play.\n\nSetting Values\n--------------\n{set A literal text}\n{set S $Z}\n{set paste Z literal text}\n{set Z $A}\n{set zone Z 100,200,500,600}\n{set zone Z 1,2} still sets zone slot Z; the two-value comma list means start at point 1 and end at point 2.\n{set zone Z X} copies existing physical zone X into physical zone Z. Zone slots remain only Z/X/C/V.\n{set point 3 1200,700}\n{set point 4 3} copies important point 3 into important point 4.\n{set point 5 $3} also works; $1 through $16 resolve to important-point coordinates.\n{set registry %USERPROFILE%\\Desktop\\New Text Document.txt}\n{set shell X bash}\n{set shell C cmd}\n{set shell G uv run python}\n\nRegistry File And Regedited CLI\n--------------------------------\nR stores one registry-file path in the Registry Hub environment. It persists through Export/Import and does not open, parse, serve, or modify the file by itself.\n$REGISTRY expands to that configured path inside {set ...} values and conditions. {paste REGISTRY} pastes the path directly; {enter REGISTRY} pastes it and presses Enter.\n{rgdclip i6z6} emits a native Tasket command for rgd rg <saved-file> i6z6 --clip. No rgd load state is used.\n{rgdappend i6z6} pipes the current multiline clipboard into rgd rs <saved-file> i6z6 --append. Regedited owns string, zone, DB-value, DB-line, and zone-relocation behavior.\nBoth tokens pass the native reference through verbatim, so i6s6, i6dbl, DB refs, and other Regedited reference forms work the same way as at the CLI.\nG is one configurable shell-command field. G, python, py, regedited, and regbin intentionally resolve to that same field; they are aliases, not executable auto-detection.\n{shell G}, {python}, {py}, {regedited}, or {regbin} opens PowerShell, pastes the current G field, and presses Enter through Tasket. Set G to the complete command you want that shortcut to run.\nGeneric {exec ...}, {system ...}, and {sys ...} execution stays disabled. rgdclip and rgdappend are the only direct process adapters in Registry Hub.\n\nBranching\n---------\n{if A contains waterfront} ... {else} ... {endif}\n{if A notcontains done} ... {endif}\n{if A == $Z} ... {endif}\n{if A != text} ... {endif}\n{if A > 5} ... {endif}\n{if A >= 5} ... {endif}\n{if A < 5} ... {endif}\n{if A <= 5} ... {endif}\n{if A empty} ... {endif}\n{if A notempty} ... {endif}\n\nDaemon-Backed Branching\n-----------------------\nTasket-httpd exposes task status and saved-task lists. Registry Hub can query those during compile-time branching.\nThese checks happen when you press Play or Save Schts, before the final Tasket schedule is emitted.\nThey are best for deciding whether to include or skip the next native Tasket actions based on a task that already exists in the daemon ledger.\nThey do not pause in the middle of the same schedule waiting for a child action to finish; use explicit waits and a later Play if you need a second-stage poll.\nMacrohelp remembers the newest task_number returned by /temp-task. That is what task last points at.\n\n{if task last is finished} ... {endif}\n{if task 12 is running} ... {endif}\n{if task 12 state == failed} ... {endif}\n{if task 12 remaining <= 2} ... {endif}\n{if task 12 message contains finished} ... {endif}\n{if saved-task MyMacro exists} ... {endif}\n{if saved-task MyMacro missing} ... {endif}\n{if saved-tasks count > 10} ... {endif}\nsaved-task reads the live /tasks list, so new .scht files appear without restarting Macrohelp.\nsaved-tasks count is useful as a sanity gate before playing a workflow that expects a populated Tasket library.\nLast task number, state, and message are included in Export/Import so a saved environment can remember the last known daemon branch point.\n\nPointer And Click Tokens\n------------------------\n{wait 200} waits 200 milliseconds.\nDuring Play, every explicit wait begins the next native task. That task is compiled from current values after the previous task finishes, with the wait as its first action. Stop All cancels remaining dispatches.\n{pt 1} through {pt 16} move through the canonical Tasket cursor-move template when that point is set. {point N} remains the long alias.\n{click} or {left} emits a left-click action.\n{click on} / {click off} changes whether buffer paste tokens click before pasting.\n{right} emits a right-click action.\n{middle} emits a middle-click action.\n{ALT_LEFT F4} and similar unknown brace tokens become Tasket key chords.\nChord aliases are normalized before JSON output: left/right/up/down become arrow keys; LEFT_SHIFT, left_shift, shift_left, ctrl, alt, win, esc, pgdn, and similar names become Tasket names.\n{ctrlmod right 3} emits Ctrl+Right three times. {controlmod right 3} is the same alias.\n{shiftmod up 3} emits Shift+Up three times. Counts are clamped to a sane maximum.\n\nMouse Wheel Tokens\n------------------\nTasket v2 exposes MOUSE_WHEEL_UP and MOUSE_WHEEL_DOWN as native mouse strokes. Macrohelp lists both under Mouse buttons in the Shift+Alt+9 manual key picker.\n{MOUSE_WHEEL_UP} and {MOUSE_WHEEL_DOWN} emit one corresponding Tasket wheel stroke.\n{wheelup}, {scrollup}, and {mousewheelup} normalize to MOUSE_WHEEL_UP.\n{wheeldown}, {scrolldown}, and {mousewheeldown} normalize to MOUSE_WHEEL_DOWN.\n{wheelup 3}, {scrollup 3}, and {MOUSE_WHEEL_UP 3} emit three separate upward wheel strokes.\n{wheeldown 2}, {scrolldown 2}, and {MOUSE_WHEEL_DOWN 2} emit two separate downward wheel strokes.\nCounts must be positive integers and use the same safety clamp as counted modifier commands.\n\nShell Tokens\n------------\n{powershell} opens Windows Terminal / PowerShell Preview with Win+Alt+Space, waits, then presses Enter.\nEvery shell alias uses that same visible flow: open PowerShell, wait for focus, paste the configured alias text, and press Enter through Tasket. No alias launches a process natively.\n{shell X} pastes shell alias X and presses Enter. Configure X as bash when {bash} should enter Bash.\n{shell C} pastes shell alias C and presses Enter. C defaults to cmd, so {cmd} enters Command Prompt inside the visible shell.\n{shell G} pastes shell alias G and presses Enter. Configure G as uv run python, regedited plus arguments, or another complete shell command.\n{cmd}, {bash}, {python}, {py}, {regedited}, and {regbin} are shortcuts for the configured shell aliases.\n{paste REGISTRY} pastes the selected registry file path. {paste REGEDITED} pastes shell alias G without opening a new shell.\n{exec ...}, {system ...}, and {sys ...} are intentionally disabled here. Use visible shell primitives and paste buffers instead.\n\nTask And Schedule Tokens\n------------------------\n{play SomeTask.scht} emits a Tasket running-other-task action.\n{play SomeTask} also works; the .scht suffix is normalized away for Tasket.\nUse Save Schts to inspect the exact generated schedule before relying on a complex workflow.\n\nZone Tokens\n-----------\nZone buffers are physical coordinate ranges. A valid zone is either x1,y1,x2,y2, two point references, or a copy of another zone.\nZone slot names are Z/X/C/V. Uppercase and lowercase are accepted in script tokens.\nZone methods match the circle-zone menu:\nZ = image snip/copy flow.\nX = text extractor flow.\nC = single-line text extractor flow.\nV = drag-copy flow.\n{zone Z,Z} plays zone buffer Z using Snipping Tool image capture.\n{zone Z,X} plays zone buffer Z using method X.\n{zone Z,C} plays zone buffer Z using method C.\n{zone V,V} plays zone buffer V using drag-copy.\n{screen} opens the native screenshot picker without a zone drag. S selects it in zone method menus.\n{zonebuf Z,X,V} captures zone Z using method X, opens the paste-buffer HUD, and saves the clipboard into paste buffer V.\nImage capture cannot be saved into a text paste buffer, so zonebuf supports X, C, and V only.\n\nExport / Import File\n--------------------\nThe plaintext environment export stores registry path, import/export paths, shell aliases, script text, A/S/D/F vars, Z/X/C/V paste buffers, zone buffers, points 1-16, Q visibility toggles, coordinates-only mode, and last status.\nBlank values are intentional. They make the file stable and easy to diff or edit by hand.\n\nExamples\n--------\n{powershell}{wait 700}{paste A}{ENTER}{wait 300}{ALT_LEFT F4}\n{powershell}{wait 700}{paste A}{ctrlmod right 3}{shiftmod up 2}{ALT_LEFT F4}\n{set A $Z}{if A contains waterfront}{enter A}{else}{enter D}{endif}\n{rgdclip i6z6}{wait 500}{CONTROL_LEFT V}\n{type literal text pasted by Tasket}\n{rgdappend i6s6}{pt 3}{buf Z}{ENTER}\n{set zone Z 100,100,600,400}{zonebuf Z,C,V}{if V contains approved}{play FollowupTask}{endif}\n{if saved-task OpenDailyFocus exists}{play OpenDailyFocus}{endif}\n{if saved-tasks count > 10}{play MorningStartup}{else}{enter D}{endif}\n{if task last is finished}{enter S}{else}{enter D}{endif}"
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
