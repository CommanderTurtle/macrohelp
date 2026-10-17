import { test, expect } from "bun:test"
import { readFileSync } from "node:fs"
import { runInNewContext } from "node:vm"
const model = new URL("../qml/model/Workflow.js", import.meta.url)
const W = runInNewContext(readFileSync(model,"utf8")+"\n({compile,keys,circle,move,drag,resolve,desktopShortcuts,zone})")
const state=()=>({version:1,script:"",registry:"/tmp/data.md",shells:{X:"bash",C:"sh",G:"uv run python"},vars:{A:"hello",S:"",D:"",F:""},buffers:{Z:"world",X:"",C:"",V:""},zones:{Z:[1,2,30,40],X:[1,2,30,40],C:null,V:[1,2,30,40]},points:[[10,20],...Array(15).fill(null)],last_task:{task_number:1}})
const bindings=[{modmask:68,key:"Print",description:"Extract text (OCR)",dispatcher:"__lua"},{modmask:0,key:"Print",description:"Screenshot"},{modmask:64,key:"Return",description:"Terminal"},{modmask:64,key:"C",description:"Universal copy"}]
const context={tasks:[{name:"Saved"}],status:{scheduled:[],running:[],finished:[{task_number:1,state:"finished"}]},shortcuts:W.desktopShortcuts(bindings)}
const compile=s=>W.compile(s,state(),context)

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
  expect(p.steps[0].actions[2].content).toBe("fish")
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
  expect(service).toContain('function point():void')
  expect(service).toContain('function click(button:string):void')
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
  expect(readFileSync(new URL("runtime/source/wld/qml/CodeArea.qml",root),"utf8")).toContain("editor.paste()")
})
