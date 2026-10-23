import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
const read=path=>readFileSync(new URL(path,import.meta.url),"utf8")
const W=runInNewContext(read("../qml/model/Workflow.js")+"\n({keys,keyName,wait,move,paste,expression,vector,numeric,pointValue,zoneValue,pasteSequence,clearState,viewPatch})")
const D=runInNewContext(read("../qml/model/Dialogs.js")+"\n({open,clone,field,point,submit,preview,recorded,addStep,description,mouseDismiss,categories})")
const env=()=>({script:"{paste Z}",registry:"/tmp/data",paths:{import:"",export:""},shells:{X:"bash",C:"sh",G:"python",P:"pwsh",T:""},vars:{A:"",S:"",D:"",F:""},buffers:{Z:"first",X:"second",C:"",V:"fourth"},zones:{Z:[1,2,40,50],X:null,C:null,V:null},points:[[20,30],[-10,40],...Array(14).fill(null)],view:{crosshair:true,keys:true,grid:false,coordinates_only:false},last_task:{task_number:1}})
const cursor={x:120,y:240}
function dialog(kind,environment=env()) {
  let state=D.open(kind,cursor),result
  return {get state(){return state},set state(value){state=value},environment,
    submit(text,at=cursor){result=D.submit(state,text,environment,at,W);state=result.state;for(const e of result.effects)if(e.kind==="change"){if(e.section)environment[e.section][e.key]=e.value;else environment[e.key]=e.value}return result},
    preview(text){state=D.preview({...state,text},W);return state}}
}

test("Save Cursor preserves entry origin before the menu warp; appends and clicks",()=>{
  const d=dialog("save_cursor")
  expect(()=>d.submit("")).toThrow("name")
  d.submit("Route");expect(D.mouseDismiss(d.state)).toBe(false)
  d.submit("x",{x:45,y:90})
  const e=d.submit("c").effects[0]
  expect(e.kind).toBe("export");expect(e.name).toBe("Route")
  expect(e.actions[0].cursormovsmap).toEqual([[0,1000,120,240],[0,1000,45,90]])
  expect(e.actions[2].keysmap[0][1]).toEqual(["LEFT_MOUSE"])
})

for(const [kind,button] of [["click_left","LEFT_MOUSE"],["click_right","RIGHT_MOUSE"],["click_middle","MIDDLE_MOUSE"]])test(kind+" supports click, timed hold and append",()=>{
  const d=dialog(kind)
  const click=d.submit("z").effects[0].actions
  expect(click[0].cursormovsmap[0].slice(2)).toEqual([120,240])
  expect(click[1].keysmap[0]).toEqual([100,[button]])
  d.submit("x");expect(d.state.stage).toBe("HoldMs")
  expect(()=>d.submit("0")).toThrow("Hold")
  expect(d.submit("600000").effects[0].actions[1].keysmap[0][0]).toBe(600000)
  expect(dialog(kind).submit("c").effects[0].kind).toBe("export")
})

test("Display retains existing commands and adds V for the large crosshair",()=>{
  for(const [key,name] of [["z","crosshair"],["x","grid"],["c","coordinates_only"],["k","keys"],["v","big_crosshair"]]){
    const d=dialog("view_toggles"),before=d.environment.view[name]
    expect(d.submit(key).close).toBe(true);expect(d.environment.view[name]).toBe(!before)
  }
  const d=dialog("view_toggles");d.submit("x");expect(d.environment.view.grid).toBe(true)
  d.submit("v");expect(d.environment.view.big_crosshair).toBe(true);expect(d.environment.view.grid).toBe(false)
  d.submit("v");expect(d.environment.view.big_crosshair).toBe(false);expect(d.environment.view.grid).toBe(false)
  d.submit("v");d.submit("x");expect(d.environment.view.grid).toBe(true);expect(d.environment.view.big_crosshair).toBe(false)
  d.submit("x");expect(d.environment.view.grid).toBe(false);expect(d.environment.view.big_crosshair).toBe(false)
  expect(dialog("stop_all").submit("").effects).toEqual([{kind:"stop"}])
})

test("circle distance, radians, degrees and scaled vectors retain the frozen origin",()=>{
  const d=dialog("circle_placer")
  d.preview("100");d.submit("100",{x:1000,y:2000});d.submit("d");d.preview("90")
  expect(d.state.target).toEqual([120,140]);d.submit("90")
  expect(d.submit("z").effects[0].actions[0].cursormovsmap[0].slice(2)).toEqual([120,140])
  const r=dialog("circle_placer");r.submit("10");r.submit("r");r.submit("pi/2")
  expect(r.state.target).toEqual([120,230])
  const v=dialog("circle_placer");v.submit("100@-3i+4j")
  expect(v.state.target).toEqual([60,160]);expect(v.state.stage).toBe("Confirm")
  expect(v.submit("x").effects[0].kind).toBe("export")
  expect(v.preview("bad").origin).toEqual([120,240])
})

test("circle chains and zone storage never move the pointer to the menu",()=>{
  const d=dialog("circle_placer")
  d.submit("10i");d.submit("v");expect(d.state.origin).toEqual([130,240])
  expect(d.state.zoneStart).toEqual([130,240])
  d.submit("20j");d.submit("c");d.submit("10i");d.submit("v")
  expect(d.state.stage).toBe("ZoneAction");expect(d.state.pendingZone).toEqual([130,240,140,220])
  const result=d.submit("b")
  expect(result.effects).toEqual([{kind:"zone-router",rect:[130,240,140,220]}])
  const s=D.open("paste_buffers",cursor,result.effects[0].rect);s.stage="ZoneCommand"
  expect(D.submit(s,"x",env(),cursor,W).effects).toEqual([{kind:"change",section:"zones",key:"X",value:[130,240,140,220]}])
  for(const method of ["z","x","c","v"]){const copy={...d.state,stage:"ZoneAction"};expect(D.submit(copy,method,env(),cursor,W).effects[0].method).toBe(method.toUpperCase())}
})

test("circle invalid input cannot execute a stale target or numeric code",()=>{
  const d=dialog("circle_placer")
  for(const text of ["0","-10","i-i","Infinity","process.exit()","20@0i+0j"])expect(()=>d.submit(text)).toThrow()
  d.submit("20");d.submit("d");expect(d.preview("bad").target).toBeNull()
  expect(()=>d.submit("bad")).toThrow()
})

test("buffer editing, single paste and ordered sequences retain duplicates",()=>{
  const d=dialog("paste_buffers");d.submit("z");d.submit("multi\nline")
  expect(d.environment.buffers.Z).toBe("multi\nline")
  const p=dialog("paste_buffers");p.submit("b");p.submit("b")
  const effects=p.submit("Z;X,Z").effects
  expect(effects[0].actions.filter(a=>a.type==="paste").map(a=>a.content)).toEqual(["first","second","first"])
  const empty=dialog("paste_buffers");empty.submit("b");expect(()=>empty.submit("c")).toThrow("empty")
  expect(()=>p.submit("Z,C")).toThrow("empty")
  const literal=dialog("paste_buffers");literal.submit("v");literal.submit("cancel");expect(literal.environment.buffers.V).toBe("cancel")
})

test("zone edit/play/capture routes distinguish screenshot and text destinations",()=>{
  const d=dialog("paste_buffers");d.submit("n");d.submit("x");d.submit("1 2")
  expect(d.environment.zones.X).toEqual([20,30,-10,40])
  const p=dialog("paste_buffers");p.submit("n");p.submit("b");p.submit("z")
  expect(p.submit("x").effects[0]).toEqual({kind:"zone",method:"X",rect:[1,2,40,50],target:""})
  const c=dialog("paste_buffers");c.submit("n");c.submit("b");c.submit("b");c.submit("z")
  expect(()=>c.submit("z")).toThrow("X, C or V")
  c.submit("c");expect(c.submit("v").effects[0]).toEqual({kind:"zone",method:"C",rect:[1,2,40,50],target:"V"})
})

test("Registry command router edits all persistent fields without treating field text as shortcuts",()=>{
  const d=dialog("registry_hub")
  for(const [cmd,stage,text,section,key] of [["z","Script","{paste X}","","script"],["r","RegistryPath","/tmp/registry","","registry"],["e","ExportPath","/tmp/export","paths","export"],["i","ImportPath","/tmp/import","paths","import"],["x","Shell","fish","shells","X"],["shell P","Shell","pwsh -NoLogo","shells","P"],["a","Var","z\nx","vars","A"],["16","Point","-10,40","points",15]]){
    d.submit(cmd);expect(d.state.stage).toBe(stage);expect(D.description(d.state,d.environment).command).toBe(false)
    d.submit(text);expect(d.state.stage).toBe("Command");expect(section?d.environment[section][key]:d.environment[key]).toEqual(stage==="Point"?[-10,40]:text)
  }
})

test("Registry points/zones, help, clear, display and play use the existing environment",()=>{
  const d=dialog("registry_hub");d.submit("1");d.submit("s",{x:-50,y:200});expect(d.environment.points[0]).toEqual([-50,200])
  d.submit("n");d.submit("x");d.submit("1,2,3,4");expect(d.environment.zones.X).toEqual([1,2,3,4])
  d.submit("h");expect(d.state.stage).toBe("Help");expect(d.state.text).toContain("{profile name}");d.submit("")
  expect(d.submit("p").effects).toEqual([{kind:"play",origin:[120,240]}])
  expect(d.submit("q").swap).toBe("view_toggles")
  expect(d.submit("l").effects).toEqual([{kind:"clear"}])
  const reset=W.clearState(d.environment);expect(reset.buffers).toEqual(d.environment.buffers);expect(reset.zones).toEqual(d.environment.zones)
})

test("recorded chords wait for trigger release and keep Esc as a recordable key",()=>{
  const d=dialog("record_keys");d.submit("Keys");d.submit("z")
  let s=D.recorded(d.state,{available:true,held:[44],events:[[44,1]]},code=>({44:"Z",29:"CONTROL_LEFT",1:"ESCAPE"}[code]))
  expect(s.captured).toEqual([])
  s=D.recorded(s,{available:true,held:[],events:[[44,0]]},String)
  s=D.recorded(s,{available:true,held:[],events:[[29,1],[1,1],[1,0],[29,0]]},code=>({29:"CONTROL_LEFT",1:"ESCAPE"}[code]))
  expect(s.capturing).toBe(false);expect(s.steps[0].keys).toEqual(["CONTROL_LEFT","ESCAPE"])
  d.state=s;d.submit("c");d.submit("paste\ntext");d.submit("x")
  expect(D.categories()).toHaveLength(9);D.addStep(d.state,["NUMPAD_ENTER"])
  d.state={...d.state,stage:"Command",delay:200}
  const result=d.submit("").effects[0]
  expect(result.kind).toBe("export");expect(result.actions.map(a=>a.type)).toEqual(["keyssequence","wait","paste","wait","keyssequence","wait"])
  expect(result.actions[0].keysmap[0]).toEqual([100,["CONTROL_LEFT","ESCAPE"]])
})

test("capture failure stops recording; duplicate steps and unknown buttons cannot silently execute",()=>{
  const d=dialog("record_keys");d.submit("Keys");d.submit("z")
  expect(D.recorded(d.state,{available:false},String).capturing).toBe(false)
  D.addStep(d.state,["A"]);D.addStep(d.state,["A"]);expect(d.state.steps).toHaveLength(1)
  expect(()=>dialog("click_left").submit("oops")).toThrow()
  expect(D.mouseDismiss(D.open("save_cursor",cursor))).toBe(true)
  expect(D.mouseDismiss({...D.open("registry_hub",cursor),stage:"Script"})).toBe(false)
})

test("a fresh fast chord after arming is not discarded just because no idle sample arrived",()=>{
  const d=dialog("record_keys");d.submit("Keys");d.submit("z")
  const s=D.recorded(d.state,{available:true,events:[[29,1],[30,1],[30,0],[29,0]],held:[]},code=>({29:"CONTROL_LEFT",30:"A"}[code]))
  expect(s.steps[0].keys).toEqual(["CONTROL_LEFT","A"])
  expect(s.capturing).toBe(false)
})

test("dialog wiring preserves baseline rendering and leaves tray controls intact",()=>{
  const qml=read("../qml/Dialogs.qml"),service=read("../qml/Service.qml"),hud=read("../qml/Hud.qml"),rust=read("../src/overlay.rs")
  expect(qml).toContain("opacity: 0.93");expect(qml).toContain("y: window.height-height-14")
  expect(qml).toContain("Commons.Color.background");expect(qml).toContain("WlrKeyboardFocus.Exclusive")
  expect(qml).toContain("mask: Region { item: dialogBox }");expect(qml).not.toContain("5000")
  expect(service).toContain('if(kind==="circle_placer")return')
  expect(service).toContain('"8":"view_toggles"');expect(hud).toContain("root.service.quickCircle")
  expect(rust).not.toContain("macrohelp_debug.log");expect(rust).toContain("keys || tracking")
  expect(qml).not.toMatch(/property var state\s*:/)
})

// Run the actual QML JS handlers as well as the pure model: this catches wiring
// mistakes that syntax parsing and model-only tests cannot detect.
function controller() {
  const source=read("../qml/Dialogs.qml"),exports=[],queued=[],closed=[]
  const service={ready:true,cursor:{...cursor},physicalCursor:{...cursor},monitors:[{name:"test",x:0,y:0,width:3840,height:2160,scale:1}],environment:env(),
    appendActions(name,actions,callback){exports.push({name,actions,callback})},later(actions){queued.push(actions)},
    change(section,key,value){if(section)this.environment[section][key]=value;else this.environment[key]=value},dialogClosed(restore,keep){closed.push({restore,keep})},play(){},clearWorkflow(){},stop(){},captureZone(){}}
  const s={service,dialogOpen:false,dialogState:{kind:"",stage:"",text:"",steps:[],capturing:false},revision:0,working:false,
    anchorX:0,anchorY:0,clipboardCallback:null,clipboardWait:{stop(){},restart(){}},focusInput(){},manualKeys:["","", ""],
    DialogState:D,Workflow:{...W,containsPoint:(p,g,w,h)=>p.x>=g.x&&p.x<g.x+w&&p.y>=g.y&&p.y<g.y+h},
    KeyDisplay:{name:String},Qt:{callLater:f=>f()},Quickshell:{screens:[{name:"test"}]}}
  s.dlgRoot=s
  Object.defineProperties(s,{capturing:{get(){return this.dialogOpen&&this.dialogState.capturing}},dialogKind:{get(){return this.dialogState.kind}},view:{get(){return D.description(this.dialogState,service.environment)}}})
  const names=[...source.matchAll(/^    function (\w+)\(/gm)].map(m=>m[1])
  for(const name of names){
    const at=source.indexOf("    function "+name+"("),tail=source.slice(at)
    const end=tail.slice(1).search(/\n    (?:function |TextEdit |Timer )/)
    runInNewContext(tail.slice(0,end<0?undefined:end+1)+"\n",s)
  }
  return {s,exports,queued,closed}
}

test("scaled dialogs capture physical pixels while placement and dismissal stay logical",()=>{
  const {s,exports,queued}=controller()
  s.service.cursor={x:868,y:572};s.service.physicalCursor={x:1736,y:1146};s.service.monitors[0].scale=2
  s.openDialog("save_cursor")
  expect(s.dialogState.origin).toEqual([1736,1146]);expect([s.anchorX,s.anchorY]).toEqual([868,572])
  s.service.cursor={x:960,y:1062};s.service.physicalCursor={x:1920,y:2124}
  s.edit("Physical");s.submit();s.submit("z")
  expect(exports[0].actions[0].cursormovsmap[0].slice(2)).toEqual([1736,1146])
  exports[0].callback({},null)
  s.service.cursor={x:640,y:360};s.service.physicalCursor={x:1280,y:720}
  s.openDialog("paste_buffers");s.edit("z");s.edit("text");s.pasteBuffer(false)
  expect(queued.at(-1)[0].cursormovsmap[0].slice(2)).toEqual([1280,720])
  s.openDialog("registry_hub");s.submit("3");s.submit("s")
  expect(s.service.environment.points[2]).toEqual([1280,720])
  s.openDialog("circle_placer");s.edit("150i");s.submit();s.submit("z")
  expect(queued.at(-1)[0].cursormovsmap[0].slice(2)).toEqual([1430,720])
})

test("QML handlers open, edit and submit through the model, retaining the captured origin",()=>{
  const {s,exports}=controller();s.openDialog("save_cursor")
  s.service.cursor={x:1920,y:2124};s.edit("Route");s.submit()
  expect(s.dialogState.stage).toBe("Command")
  s.submit("z");expect(exports).toHaveLength(1);expect(s.working).toBe(true)
  expect(exports[0].actions[0].cursormovsmap[0].slice(2)).toEqual([120,240])
  exports[0].callback({},null);expect(s.dialogOpen).toBe(false)
})

test("QML single-letter routing, manual cancellation and stale export replies do not leak between dialogs",()=>{
  const {s,exports}=controller();s.openDialog("record_keys");s.edit("Keys");s.submit()
  s.edit("x");expect(s.dialogState.stage).toBe("Manual")
  s.manualKeys=["CONTROL_LEFT","A",""];s.addManual(false);expect(s.dialogState.steps).toHaveLength(1)
  s.cancel();expect(s.dialogOpen).toBe(true);expect(s.dialogState.steps).toHaveLength(0)
  s.openDialog("save_cursor");s.edit("One");s.submit();s.submit("z")
  s.openDialog("circle_placer");const revision=s.revision
  exports[0].callback({},null)
  expect(s.dialogOpen).toBe(true);expect(s.dialogKind).toBe("circle_placer");expect(s.revision).toBe(revision)
  s.edit("10i");s.submit();expect(s.dialogState.stage).toBe("Confirm")
})

test("QML buffer execution restores the original pointer and preserves edited text",()=>{
  const {s,queued}=controller();s.openDialog("paste_buffers");s.edit("z");expect(s.dialogState.stage).toBe("Edit")
  s.edit("hello\nworld");s.service.cursor={x:1920,y:2124};s.pasteBuffer(true)
  expect(s.dialogOpen).toBe(false);expect(queued[0][0].cursormovsmap[0].slice(2)).toEqual([120,240])
  expect(queued[0].filter(a=>a.type==="paste")[0].content).toBe("hello\nworld")
  expect(queued[0].at(-1).keysmap[0][1]).toEqual(["ENTER"])
})

test("all four buffer editors preserve multiline text and literal escape characters through paste",()=>{
  for(const slot of ["Z","X","C","V"]){
    const {s,queued}=controller(),text="first\r\n\n\t  second\\n third"
    s.openDialog("paste_buffers");s.edit(slot.toLowerCase());s.edit(text);s.pasteBuffer(false)
    expect(s.service.environment.buffers[slot]).toBe(text)
    expect(queued[0].find(a=>a.type==="paste").content).toBe(text)
  }
})
test("Escape and the existing greater-than-60px dismissal request a return, not execution closes",()=>{
  const {s,closed}=controller();s.openDialog("paste_buffers");s.cancel()
  expect(closed.at(-1).restore).toBe(true)
  s.openDialog("paste_buffers");s.graceEnd=0
  s.service.cursor={x:s.anchorX+60,y:s.anchorY};s.dismissIfMoved();expect(s.dialogOpen).toBe(true)
  s.service.cursor.x++;s.dismissIfMoved();expect(s.dialogOpen).toBe(false);expect(closed.at(-1).restore).toBe(true)
  s.openDialog("paste_buffers");s.edit("z");s.edit("hello");s.pasteBuffer(false)
  expect(closed.at(-1).restore).toBe(false)
})
test("menu swaps retain the pre-blink return lease without launching another blink",()=>{
  const {s,closed}=controller();s.openDialog("registry_hub");s.submit("q")
  expect(s.dialogKind).toBe("view_toggles");expect(closed.at(-1).keep).toBe(true)
})
