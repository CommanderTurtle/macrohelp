import { test, expect } from "bun:test"
import { readFileSync } from "node:fs"
import { runInNewContext } from "node:vm"
import {spawnSync} from "node:child_process"
const model = new URL("../qml/model/Workflow.js", import.meta.url)
const W = runInNewContext(readFileSync(model,"utf8")+"\n({compile,keys,circle,move,drag,resolve,desktopShortcuts,shortcutRows,zone,containsPoint,monitorScale,displayPoint})")
const state=()=>({version:1,script:"",registry:"/tmp/data.md",shells:{X:"bash",C:"sh",G:"uv run python",P:"pwsh"},vars:{A:"hello",S:"",D:"",F:""},buffers:{Z:"world",X:"",C:"",V:""},zones:{Z:[1,2,30,40],X:[1,2,30,40],C:null,V:[1,2,30,40]},points:[[10,20],...Array(15).fill(null)],last_task:{task_number:1}})
const bindings=[{modmask:68,key:"Print",description:"Extract text (OCR)",dispatcher:"__lua"},{modmask:0,key:"Print",description:"Screenshot"},{modmask:64,key:"Return",description:"Terminal"},{modmask:64,key:"C",description:"Universal copy"}]
const context={tasks:[{name:"Saved"}],status:{scheduled:[],running:[],finished:[{task_number:1,state:"finished"}]},shortcuts:W.desktopShortcuts(bindings)}
const compile=s=>W.compile(s,state(),context)

test("display geometry is queried once at startup, never from the overlay loop",()=>{
  const native=readFileSync(new URL("../src/overlay.rs",import.meta.url),"utf8")
  const session=native.slice(native.indexOf("pub fn run()"),native.indexOf("#[cfg(test)]"))
  const loop=session.indexOf("while !STOP.load")
  expect(loop).toBeGreaterThan(0)
  expect(session.match(/hypr\("monitors"\)/g)).toHaveLength(1)
  expect(session.slice(0,loop)).toContain('let monitors = hypr("monitors")')
  expect(session.slice(0,loop)).toContain("let scale = monitor_scale(&monitors);")
  expect(session.slice(loop)).not.toContain('hypr("monitors")')
  expect(session.slice(loop)).toContain("hello(&config, &monitors)")
  expect(session.slice(loop)).toContain("cursor_event(cursor, scale)")
  expect(native).not.toContain("last_monitors")
  const service=readFileSync(new URL("../qml/Service.qml",import.meta.url),"utf8")
  expect(service).toContain("root.monitors=value.monitors")
  expect(service).not.toContain('r.event==="monitors"')
})

test("desktop shortcut rows use the same live contracts as workflow compilation",()=>{
  const rows=W.shortcutRows(bindings)
  expect(rows.find(r=>r.name==="Screenshot").chord).toBe("PRINT_SCREEN")
  expect(rows.find(r=>r.name==="Single-line OCR").fallback).toBe(true)
  expect(W.shortcutRows([]).every(r=>r.chord==="Not configured")).toBe(true)
  expect(W.shortcutRows(bindings,{screenshot:"ALT+F12"})[0].chord).toBe("ALT_LEFT + F12")
})

test("shortcut refreshes coalesce, preserve known contracts on failure and ignore stale runtimes",()=>{
  const source=readFileSync(new URL("../qml/Service.qml",import.meta.url),"utf8")
  const code=source.slice(source.indexOf("  function refreshBindings()"),source.indexOf("  Timer {id:bindingsDelay"))
  const requests=[],delay={count:0,restart(){this.count++}}
  const s={ready:true,bindingsRefreshing:false,bindingsPending:false,bindingsEpoch:0,bindingsError:"",desktopBindings:[],shortcutRows:[],settings:{},call(r,cb){requests.push(cb)}}
  Object.assign(s,new Function("root","Workflow","bindingsDelay",`with(root){${code};return {refreshBindings,bindingEvent}}`)(s,W,delay))
  s.refreshBindings();s.refreshBindings();expect(requests).toHaveLength(1)
  requests[0]([]);expect(requests).toHaveLength(1);expect(delay.count).toBe(1)
  s.refreshBindings()
  requests[1](bindings);expect(s.shortcutRows[0].chord).toBe("PRINT_SCREEN")
  var rows=s.shortcutRows,live=s.desktopBindings
  s.refreshBindings();requests[2](bindings);expect(s.shortcutRows).toBe(rows);expect(s.desktopBindings).toBe(live)
  s.refreshBindings();requests[3](null,"offline");expect(s.shortcutRows[0].chord).toBe("PRINT_SCREEN");expect(s.bindingsError).toBe("offline")
  s.refreshBindings();s.bindingsEpoch++;requests[4]([]);expect(s.shortcutRows[0].chord).toBe("PRINT_SCREEN")
  for(const name of ["configreloaded","activelayout","activewindow","mousemove"])s.bindingEvent(name)
  expect(delay.count).toBe(2)
  for(var i=0;i<10000;i++){s.bindingEvent("activelayout");s.bindingEvent("activewindow");s.bindingEvent("focusedmon")}
  expect(delay.count).toBe(2)
})

test("capture schedules preserve C++ actions and deliberate delays; screen only opens the picker",()=>{
  const rect=state().zones.Z
  const shot=W.zone(rect,"Z",context.shortcuts),ocr=W.zone(rect,"X",context.shortcuts),copy=W.zone(rect,"V",context.shortcuts)
  expect(shot.map(a=>a.type)).toEqual(["keyssequence","wait","cursormovements","wait","cursormovements","wait"])
  expect(shot[0].keysmap[0]).toEqual([100,["PRINT_SCREEN"]])
  expect(shot[1].duration).toBe(2);expect(shot[2].cursormovsmap).toEqual([[0,300,1,2]])
  expect(shot[3].duration).toBe(.2);expect(shot[4].cursormovsmap).toEqual([[0,100,1,2],[0,400,30,40]])
  expect(shot[5].duration).toBe(2)
  expect(ocr[0].keysmap[0]).toEqual([100,["WINDOWS","CONTROL_LEFT","PRINT_SCREEN"]])
  expect(ocr.some(a=>a.type==="keyssequence"&&a.keysmap[0][1].includes("LEFT_MOUSE"))).toBe(false)
  const picker=compile("{screen}").steps[0]
  expect(picker.capture).toBe(true);expect(picker.actions).toEqual(shot.slice(0,2))
  if(process.env.MACROHELP_CPP_CONTROL_TEST){
    const result=spawnSync(process.env.MACROHELP_CPP_CONTROL_TEST,["--zone-contract"],{encoding:"utf8"})
    expect(result.status).toBe(0)
    const native=JSON.parse(result.stdout)
    const clean=actions=>JSON.parse(JSON.stringify(actions)).map(a=>{delete a.keysSeqId;delete a.cursorMovsId;return a})
    for(const [index,actions] of [[0,shot],[1,ocr],[3,copy]]){
      const expected=clean(index===1?native[index].actions.slice(3):native[index].actions),actual=clean(actions)
      // Only platform-native shortcut chords differ, never the action timing.
      const at=index===3?4:0
      actual[at].keysmap[0][1]=expected[at].keysmap[0][1]
      expect(actual).toEqual(expected)
    }
    expect(clean(picker.actions).map(a=>a.type)).toEqual(clean(native[4].actions).map(a=>a.type))
  }
})

test("single-line OCR updates the clipboard through manual PowerShell keys and exits both shells",()=>{
  const actions=compile("{zone Z,C}").steps[0].actions
  expect(actions.slice(0,6)).toEqual(W.zone(state().zones.Z,"X",context.shortcuts))
  const shell=actions.slice(6),typed=shell.filter(a=>a.type==="type").map(a=>a.text)
  expect(typed).toEqual(["pwsh","$macrohelpText = Get-Clipboard -Raw; Set-Clipboard -Value (($macrohelpText -replace '[\\r\\n]+', ' ').Trim())","exit","exit"])
  expect(shell.filter(a=>a.type==="wait").every(a=>a.duration===.2)).toBe(true)
  expect(shell.some(a=>a.type==="systemcommand")).toBe(false)
  const p=compile("{zonebuf Z,C,V}");expect(p.steps[1].singleLine).toBe(true)
  // A user-provided native single-line shortcut does not open a shell.
  const native=W.zone(state().zones.Z,"C",W.desktopShortcuts(bindings,{singleLine:"SUPER+F8"}))
  expect(native).toHaveLength(6);expect(native.some(a=>a.type==="type")).toBe(false)
  for(const command of ["",undefined]){const missing=state();missing.shells.P=command;expect(()=>W.compile("{zone Z,C}",missing,context)).toThrow("Set the single-line PowerShell command")}
  const custom=state();custom.shells.P="my-powershell --profile";expect(W.compile("{zone Z,C}",custom,context).steps[0].actions.find(a=>a.type==="type").text).toBe("my-powershell --profile")
  const literal=compile("{type echo @'\ncontent\n'@}{CTRL+SHIFT+V}").steps[0].actions
  expect(literal[0].content).toBe("echo @'\ncontent\n'@");expect(literal[1].keysmap[0][1]).toEqual(["CONTROL_LEFT","SHIFT_LEFT","V"])
})

test("only visual projections scale physical points; compiled task coordinates stay physical",()=>{
  const scale=W.monitorScale([{width:3840,height:2160,scale:2}])
  expect(W.displayPoint([1280,720],{x:0,y:0},scale)).toEqual([640,360])
  expect(W.displayPoint([-1280,720],{x:-1920,y:0},scale)).toEqual([1280,360])
  const plan=compile("{set point 1 1280,720}{point 1}{set zone C 1280,720,1600,900}{zone C,V}")
  expect(plan.state.points[0]).toEqual([1280,720])
  expect(plan.steps[0].actions[0].cursormovsmap[0].slice(2)).toEqual([1280,720])
  expect(plan.state.zones.C).toEqual([1280,720,1600,900])
  for(const monitors of [[],[{scale:0}],[{scale:-2}],[{scale:"bad"}]])expect(W.monitorScale(monitors)).toBe(1)
  const panel=readFileSync(new URL("../qml/Panel.qml",import.meta.url),"utf8")
  expect(panel).toContain("f([Math.round(root.app.physicalCursor.x),Math.round(root.app.physicalCursor.y)])")
})

test("balanced outer braces preserve nested JSON, code and command-looking literal text",()=>{
  const text='{\n  "nested": {"list": [{"n": 1}]},\n  "script": "{wait 99}{if nope}{endif}",\n  "literal": "&amp;\\\\n"\n}'
  expect(compile("{type "+text+"}{type after}").steps[0].actions.map(a=>a.content)).toEqual([text,"after"])
  for(const target of ["A","paste C","buffer C","cbuf"]){
    const p=compile("{set "+target+" "+text+"}{paste "+(target==="A"?"A":"C")+"}")
    expect(p.steps[0].actions[0].content).toBe(text)
  }
  for(const source of ["{type {broken}","{type ok}}","{type ok}{","{press }}","{type ok} trailing"]){expect(()=>compile(source)).toThrow()}
})

test("press decodes whole named keys only and holds modifiers across repeated taps",()=>{
  const a=compile("{press Shift &amp; 3}").steps[0].actions[0]
  expect(a.type).toBe("keyssequence");expect(a.keysmap[0]).toEqual([282,["SHIFT_LEFT"]])
  for(const time of [1,121,241])expect(a.keysmap[time]).toEqual([40,["ampersand"]])
  expect(compile("{press &rbrace;}").steps[0].actions[0].keysmap[1][1]).toEqual(["braceright"])
  expect(compile("{press CTRL+&comma;}").steps[0].actions[0].keysmap[1][1]).toEqual(["comma"])
  expect(compile("{press &plus;}").steps[0].actions[0].keysmap[1][1]).toEqual(["plus"])
  expect(compile("{press 3}").steps[0].actions[0].keysmap[1][1]).toEqual(["3"])
  expect(compile("{press CTRL+3}").steps[0].actions[0].keysmap[1][1]).toEqual(["3"])
  expect(Object.keys(compile("{press CTRL+3}").steps[0].actions[0].keysmap)).toHaveLength(2)
  expect(compile("{type &rbrace; &amp;}").steps[0].actions[0].content).toBe("&rbrace; &amp;")
  expect(compile("{set C &rbrace;}{paste C}").steps[0].actions[0].content).toBe("&rbrace;")
  for(const s of ["{press}","{press Shift &amp; 0}","{press Shift &amp; 1001}","{press &xml;}","{press &amp;rbrace;}"]){expect(()=>compile(s)).toThrow()}
})

test("type and set preserve actual multiline whitespace without interpreting backslash escapes",()=>{
  const text="first  line\n\n\tindented\r\nlast \\n literal"
  expect(compile("{type "+text+"}").steps[0].actions[0].content).toBe(text)
  for(const target of ["A","paste Z"]){
    const p=compile("{set "+target+" "+text+"}{paste "+(target==="A"?"A":"Z")+"}")
    expect(p.steps[0].actions[0].content).toBe(text)
    expect(target==="A"?p.state.vars.A:p.state.buffers.Z).toBe(text)
  }
  expect(compile("{type first\\nsecond}").steps[0].actions[0].content).toBe("first\\nsecond")
})

test("multiline buffer references and deferred clipboard content are not flattened",()=>{
  const e=state();e.vars.A="variable\n\tline";e.buffers.Z="buffer\r\n\nline\\n"
  const p=W.compile("{type $A}\n{buf Z}",e,context)
  expect(p.steps[0].actions.map(a=>a.content)).toEqual([e.vars.A,e.buffers.Z])
  const live=W.compile("{zonebuf Z,X,A}{type $A}",e,context)
  expect(W.resolve(live.steps.at(-1).actions,e)[0].content).toBe(e.vars.A)
})

test("all ordinary input stays in one native plan",()=>{
  const p=compile("{point 1}{click}{paste A,Z}{wait 200}{CTRL+S}{play Saved.scht}")
  expect(p.steps).toHaveLength(1)
  expect(p.steps[0].actions.map(a=>a.type)).toEqual(["cursormovements","keyssequence","paste","wait","keyssequence","runningothertask"])
  expect(p.steps[0].actions[2].content).toBe("helloworld")
  expect(p.steps[0].actions[3].duration).toBe(.2)
  expect(p.steps[0].actions[5].otherTaskName).toBe("Saved")
})
test("native key aliases and scroll schema",()=>{
  expect(W.keys("ctrl+shift+left").keysmap[0][1]).toEqual(["CONTROL_LEFT","SHIFT_LEFT","LEFT_ARROW"])
  expect(compile("{wheelup 3}{ctrlmod right 2}").steps[0].actions[0]).toEqual({type:"scroll",vertical:-3,horizontal:0})
  expect(compile("{ctrlmod right 2}").steps[0].actions[0].loop).toBe(2)
})
test("variables, references and compile-time nested conditions",()=>{
  const p=compile("{set A changed}{if A == changed}{if task last is finished}{type $A}{else}{type no}{endif}{endif}")
  expect(p.state.vars.A).toBe("changed");expect(p.steps[0].actions[0].content).toBe("changed")
  expect(state().vars.A).toBe("hello")
})
test("saved schedule conditions and disabled branches",()=>{
  expect(compile("{if saved-task Saved exists}{type yes}{endif}{if saved-tasks count > 0}{type next}{endif}").steps[0].actions).toHaveLength(2)
  expect(compile("{if A == no}{if nonsense}{type no}{endif}{else}{type yes}{endif}").steps[0].actions[0].content).toBe("yes")
})
test("desktop OCR uses native input; clipboard and unchanged registry steps follow",()=>{
  const p=compile("{type first}{zonebuf Z,X,A}{rgdclip section}{paste A}")
  expect(p.steps.map(s=>s.kind)).toEqual(["input","clipboard","regedited","input"])
  expect(p.steps[0].actions[1].keysmap[0][1]).toEqual(["WINDOWS","CONTROL_LEFT","PRINT_SCREEN"])
  expect(p.steps[1].slot).toBe("A")
  expect(p.steps[2]).toEqual({kind:"regedited",action:"clip",file:"/tmp/data.md",reference:"section"})
  const next=state();next.vars.A="OCR result"
  expect(W.resolve(p.steps[3].actions,next)[0].content).toBe("OCR result")
  expect(compile("{zonebuf X,A}").steps[1].slot).toBe("A")
})
test("drag-copy and zone errors",()=>{
  const actions=compile("{zone V}").steps[0].actions
  expect(actions[2].optionalkeysstroke).toEqual(["LEFT_MOUSE"])
  expect(actions[4].keysmap[0][1]).toEqual(["WINDOWS","C"])
  expect(()=>compile("{zone C}")).toThrow("Save zone")
  expect(()=>compile("{zonebuf X,Q}")).toThrow("Invalid destination")
  expect(()=>compile("{zonebuf Z,Z,V}")).toThrow("Images cannot")
  expect(()=>compile("{zone Z,Q}")).toThrow("Zone method")
})
test("circle math, zero angle and negative multi-monitor points",()=>{
  expect(W.circle([-100,20],100,0,"degrees")).toEqual([0,20])
  expect(W.circle([0,0],100,Math.PI/2,"radians")).toEqual([0,100])
  expect(W.move([-200,400],50).cursormovsmap[0]).toEqual([0,50,-200,400])
})
test("malformed conditions and commands fail before input",()=>{
  for(const s of ["{if A empty}","{else}","{endif}","{if A empty}{else}{else}{endif}","{exec rm}","{system shutdown}","{point 16}","{wait -1}","unwrapped text"]){expect(()=>compile(s)).toThrow()}
})
test("environment point, zone and shell edits",()=>{
  const p=compile("{set point 2 -20,30}{set zone C 1,2,3,4}{set shell X fish}{shell X}")
  expect(p.state.points[1]).toEqual([-20,30]);expect(p.state.zones.C).toEqual([1,2,3,4])
  expect(p.steps[0].actions[0].keysmap[0][1]).toEqual(["WINDOWS","ENTER"])
  expect(p.steps[0].actions[2]).toEqual({type:"type",text:"fish",interval_ms:0})
})
test("shells use user commands and terminal chords, never native process execution",()=>{
  expect(()=>compile("{shell type}")).toThrow("Shell alias must")
  expect(()=>compile("{screen unwanted}")).toThrow("takes no arguments")
  for(const op of ["pwsh","bash","shell X"]){const bad=state();bad.shells.P="type";bad.shells.X="type";expect(()=>W.compile("{"+op+"}",bad,context)).toThrow("shell starter")}
  const badCapture=state();badCapture.shells.P="type";expect(()=>W.compile("{zone Z,C}",badCapture,context)).toThrow("shell starter")
  const s=state();s.shells.P="pwsh -NoLogo";s.shells.T="SUPER+F12"
  const actions=W.compile("{pwsh}",s,context).steps[0].actions
  expect(actions.map(a=>a.type)).toEqual(["keyssequence","wait","type","keyssequence"])
  expect(actions[0].keysmap[0][1]).toEqual(["WINDOWS","F12"])
  expect(actions[2].text).toBe("pwsh -NoLogo")
  expect(actions[3].keysmap[0][1]).toEqual(["ENTER"])
  expect(compile("{pwsh}").steps[0].actions[2].text).toBe("pwsh")
  expect(compile("{powershell}").steps).toEqual(compile("{pwsh}").steps)
  expect(compile("{terminal}").steps[0].actions).toHaveLength(2)
  s.shells.X="";expect(W.compile("{bash}",s,context).steps[0].actions).toHaveLength(2)
  expect(()=>W.compile("{bash}",state(),{shortcuts:{}})).toThrow("terminal shortcut")
  s.shells.P="pwsh\nunsafe";expect(()=>W.compile("{pwsh}",s,context)).toThrow("single line")
})
test("overlay uses native theme, empty input mask and non-secret helper",()=>{
  const base=new URL("../qml/",import.meta.url)
  const service=readFileSync(new URL("Service.qml",base),"utf8"),hud=readFileSync(new URL("Hud.qml",base),"utf8")
  expect(hud).toContain("mask: Region {}");expect(hud).toContain("WlrKeyboardFocus.None")
  expect(service).toContain('[root.helper,"stop"]');expect(service).toContain('v.session!==job.session')
  expect(service).not.toMatch(/API_KEY|X-Tasket-Key|https?:\/\/[^" ]+/)
  const shortcuts=readFileSync(new URL("../bindings.lua",base),"utf8")
  expect((shortcuts.match(/o\.bind\(/g)||[])).toHaveLength(10)
  expect(shortcuts).toContain('omarchy-shell -q io.github.commanderturtle.macrohelp ')
  expect(service).toContain('if(r.event==="dialog")')
  expect(service).toContain('root.showDialog(kind)')
  expect(service).toContain('if(r.offline)')
  expect(hud).toContain('Commons.Color.accent')
  expect(service).not.toMatch(/macrohelp-httpd|SocketExecutor|UnixListener|TcpListener/)
})
test("overlay and HTTP server have independent builds",()=>{
  const root=new URL("../../../../",import.meta.url)
  const cargo=readFileSync(new URL("runtime/source/wld/Cargo.toml",root),"utf8")
  expect(cargo).not.toMatch(/macrohelp-wayland|http-daemon/)
  const overlayBuild=readFileSync(new URL("runtime/source/wld/build.sh",root),"utf8")
  expect(overlayBuild).not.toMatch(/macrohelp-httpd|--init|OMARSKET/)
  const serverBuild=readFileSync(new URL("http-daemon/wld/build.sh",root),"utf8")
  expect(serverBuild).not.toMatch(/macrohelpctl|plugin enable|runtime\/source/)
  const manifest=JSON.parse(readFileSync(new URL("manifest.json",root),"utf8"))
  for(const entry of Object.values(manifest.entryPoints)){expect(entry).toStartWith("runtime/source/wld/");expect(()=>readFileSync(new URL(entry,root))).not.toThrow()}
})

test("OCR engine replacement does not change a desktop shortcut macro",()=>{
  const tesseract=[{...bindings[0],arg:"omarchy-capture-text"}]
  const paddle=[{...bindings[0],arg:"/home/test/.local/bin/omarchy-ppocr-capture"}]
  expect(W.zone(state().zones.Z,"X",W.desktopShortcuts(tesseract))).toEqual(W.zone(state().zones.Z,"X",W.desktopShortcuts(paddle)))
})

test("active remaps, custom single-line bindings and native keysyms are respected",()=>{
  const remapped=W.desktopShortcuts([{key:"F10",modmask:9,description:"Extract text (OCR) with PP-OCRv6",dispatcher:"__lua"}])
  expect(remapped.X.keysmap[0][1]).toEqual(["ALT_LEFT","SHIFT_LEFT","F10"])
  expect(remapped.C).toEqual(remapped.X)
  const custom=W.desktopShortcuts(bindings,{ocr:"CTRL+F9",singleLine:"SUPER+F8",captureWaitMs:3000})
  expect(custom.X.keysmap[0][1]).toEqual(["CONTROL_LEFT","F9"])
  expect(custom.C.keysmap[0][1]).toEqual(["WINDOWS","F8"])
  expect(W.zone(state().zones.Z,"C",custom)[1].duration).toBe(3)
  expect(W.desktopShortcuts([{key:"XF86Launch1",modmask:64,description:"Extract text (OCR)"}]).X.keysmap[0][1]).toEqual(["WINDOWS","XF86Launch1"])
  const both=W.desktopShortcuts([{key:"F8",modmask:64,description:"Single-line OCR"},...bindings])
  expect(both.X.keysmap[0][1]).toEqual(["WINDOWS","CONTROL_LEFT","PRINT_SCREEN"])
  expect(both.C.keysmap[0][1]).toEqual(["WINDOWS","F8"])
})

test("missing, submapped and keycode-only bindings never trigger a guessed shortcut",()=>{
  for(const list of [[],[{...bindings[0],submap:"selection"}],[{...bindings[0],key:"",keycode:107}],[{...bindings[0],key:"code:107"}]]){
    expect(W.desktopShortcuts(list).X).toBeNull()
    expect(()=>W.zone(state().zones.Z,"X",W.desktopShortcuts(list))).toThrow("Choose a desktop shortcut")
  }
})

test("capture is only a shortcut, waits and coordinate actions over the existing queue",()=>{
  const p=compile("{zone V,X}")
  expect(p.steps).toHaveLength(1)
  expect(p.steps[0].capture).toBe(true)
  expect(p.steps[0].actions.map(a=>a.type)).toEqual(["keyssequence","wait","cursormovements","wait","cursormovements","wait"])
  expect(p.steps[0].actions[2].cursormovsmap[0].slice(2)).toEqual(state().zones.V.slice(0,2))
  const root=new URL("../../../../",import.meta.url)
  const helper=readFileSync(new URL("runtime/source/wld/src/overlay.rs",root),"utf8")
  expect(helper).not.toMatch(/grim|tesseract|wl-copy|fn zone\(|"zone" =>|"clipboard" =>/)
  expect(helper).toContain('command("rgd", &args, input)')
  const manifest=JSON.parse(readFileSync(new URL("manifest.json",root),"utf8"))
  expect(manifest.dependencies.map(d=>d.name)).not.toContain("grim")
  expect(manifest.dependencies.map(d=>d.name)).not.toContain("tesseract")
  const service=readFileSync(new URL("runtime/source/wld/qml/Service.qml",root),"utf8")
  expect(service).not.toContain('method:"zone"')
  expect(service).toContain('method:"bindings"')
  const editor=readFileSync(new URL("runtime/source/wld/qml/CodeArea.qml",root),"utf8")
  expect(editor).toContain("clipboard.paste()")
  expect(editor).not.toMatch(/editor\.(?:paste|undo|selectAll)\(/)
})

test("pointer HUD belongs to exactly one logical output, including negative origins",()=>{
  const left={x:-1920,y:0},right={x:0,y:0}
  expect(W.containsPoint({x:-1,y:100},left,1920,1080)).toBe(true)
  expect(W.containsPoint({x:0,y:100},left,1920,1080)).toBe(false)
  expect(W.containsPoint({x:0,y:100},right,1280,720)).toBe(true)
  expect(W.containsPoint({x:1280,y:100},right,1280,720)).toBe(false)
  expect(W.containsPoint({x:200,y:720},right,1280,720)).toBe(false)
  expect(W.containsPoint({x:0,y:0},null,1920,1080)).toBe(false)
})

test("native panel bounds, wrapping, focus and multi-output clipboard ownership",()=>{
  const base=new URL("../qml/",import.meta.url)
  const panel=readFileSync(new URL("Panel.qml",base),"utf8")
  expect(panel).toContain("panel.fittedContentWidth")
  expect(panel).toContain("panel.fittedContentHeight")
  expect(panel).toContain("contentWidth:availableWidth")
  expect(panel).toContain("width:scroll.availableWidth")
  expect(panel).toContain("Keys.onEscapePressed:root.close()")
  expect(panel).toContain("workflowEditor.input")
  expect(panel).toContain("bufferEditor.input")
  expect(panel).toContain("if(root.app.activePanel===root)Qt.callLater")
  const editor=readFileSync(new URL("CodeArea.qml",base),"utf8")
  expect(editor).toContain("contentWidth: availableWidth")
  expect(editor).toContain("Border.controlSpec")
  expect(editor).toContain("Style.selectionFillFor")
})

test("focused key recording uses key identities, not Ctrl-generated control text",()=>{
  const source=readFileSync(new URL("../qml/KeyRecorder.qml",import.meta.url),"utf8")
  const line=source.split("\n").find(line=>line.trim().startsWith("function key(event)"))
  const key=runInNewContext(line+"\nkey")
  expect(key({key:65,text:"\x01"})).toBe("A")
  expect(key({key:16777225,text:""})).toBe("PRINT_SCREEN")
  expect(key({key:16777222,text:""})).toBe("INSERT")
  expect(key({key:0,text:""})).toBe("")
  expect(source).toContain("root.recording?root.finish():root.start()")
})
