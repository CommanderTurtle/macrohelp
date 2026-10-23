import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
const read=name=>readFileSync(new URL(name,import.meta.url),"utf8")
const W=runInNewContext(read("../qml/model/Workflow.js")+"\n({copy,keys,keyName,wait,move,paste,expression,vector,numeric,pointValue,zoneValue,pasteSequence,clearState})")
const D=runInNewContext(read("../qml/model/Dialogs.js")+"\n({open,clone,field,point,submit,preview,recorded,addStep,description,mouseDismiss,categories,help})")
const C=runInNewContext(read("../qml/model/Control.js")+"\n({merge,prepare})")
const env=()=>({version:1,script:"",registry:"/tmp/data",shells:{X:"bash",C:"sh",G:"python",P:"pwsh",T:""},paths:{export:"/tmp/export.json",import:"/tmp/import.json"},vars:{A:"original",S:"kept",D:"",F:""},buffers:{Z:"hello",X:"world",C:"",V:""},zones:{Z:[0,0,50,50],X:null,C:null,V:null},points:Array(16).fill(null),view:{crosshair:true,grid:false,coordinates:true,keys:true,coordinates_only:false},last_task:null})
const cursor={x:20,y:30}

test("config merge retains omitted fields and never mutates the original",()=>{
  const original=env(),before=JSON.stringify(original)
  const result=C.prepare({config:{vars:{A:"temporary"},points:{16:[10,20]},zones:{X:[1,2,3,4]}}},original,cursor,null,W,D)
  expect(JSON.stringify(original)).toBe(before);expect(result.environment.vars.S).toBe("kept")
  expect(result.environment.vars.A).toBe("temporary");expect(result.environment.points[15]).toEqual([10,20]);expect(result.environment.zones.Z).toEqual([0,0,50,50])
})
test("HTTP menu inputs use the real Registry Hub transitions and produce native effects",()=>{
  const result=C.prepare({menu:2,inputs:["Z","{zone Z,X}","P"],config:{vars:{A:"override"}}},env(),cursor,null,W,D)
  expect(result.environment.script).toBe("{zone Z,X}");expect(result.effects).toEqual([{kind:"play",origin:[20,30]}]);expect(result.closed).toBe(true)
})
test("bad later inputs are rejected before any live state or action is changed",()=>{
  const original=env(),before=JSON.stringify(original)
  expect(()=>C.prepare({menu:2,inputs:["A","changed","not-a-command"],config:{vars:{S:"override"}}},original,cursor,null,W,D)).toThrow("Input 3")
  expect(JSON.stringify(original)).toBe(before)
})
test("every native menu can be reached through the same request contract",()=>{
  const menus=["save_cursor","paste_buffers","registry_hub","circle_placer","click_left","click_right","click_middle","stop_all","view_toggles","record_keys"]
  for(let menu=0;menu<10;menu++)expect(C.prepare({menu},env(),cursor,null,W,D).state.kind).toBe(menus[menu])
  const click=C.prepare({menu:5,inputs:["X","350"]},env(),cursor,null,W,D)
  expect(click.effects[0].actions[1].keysmap[0]).toEqual([350,["RIGHT_MOUSE"]])
})
test("partial flows can continue and button actions use existing paste and recorder formats",()=>{
  const first=C.prepare({menu:1,inputs:["Z"]},env(),cursor,null,W,D)
  expect(first.state.stage).toBe("Edit")
  const second=C.prepare({inputs:[{action:"paste_enter",value:"with, commas"}]},first.environment,cursor,first.state,W,D)
  expect(second.effects[0].actions.some(a=>a.type==="paste"&&a.content==="with, commas")).toBe(true)
  const record=C.prepare({menu:9,inputs:["Example",{action:"add_step",keys:["CONTROL_LEFT","S"],delay:120},{action:"replay"}]},env(),cursor,null,W,D)
  expect(record.closed).toBe(true);expect(record.effects[0].kind).toBe("queue")
})
test("continued editor config overrides replace stale displayed text before Play",()=>{
  const first=C.prepare({menu:2,inputs:["Z"]},env(),cursor,null,W,D)
  const next=C.prepare({config:{script:"{type changed}"},inputs:[{action:"play"}]},first.environment,cursor,first.state,W,D)
  expect(next.environment.script).toBe("{type changed}")
  expect(first.state.text).toBe("")
  const variable=C.prepare({menu:2,inputs:["A"]},env(),cursor,null,W,D)
  const accepted=C.prepare({config:{vars:{A:"replacement"}},inputs:[{action:"accept"}]},variable.environment,cursor,variable.state,W,D)
  expect(accepted.environment.vars.A).toBe("replacement")
})

function service(overrides={}) {
  const source=read("../qml/Service.qml"),timer=()=>({running:false,restart(){this.running=true},stop(){this.running=false}})
  const s={environment:env(),cursor:{...cursor},ready:true,connected:true,busy:false,localBusy:false,pending:null,recordingPath:false,error:"",serial:0,generation:0,
    controlRun:null,controlLast:null,controlPending:0,dispatch:null,activePanel:null,DialogState:D,Workflow:W,Control:C,persist:timer(),calls:[],saves:[],
    call(value){this.calls.push(value);if(value.method==="save-state")this.saves.push(W.copy(value.state))},
    dialogs:{dialogOpen:false,working:false,dialogState:null,openDialog(kind){this.dialogOpen=true;this.dialogState=D.open(kind,cursor)},closeDialog(){this.dialogOpen=false;this.working=false},focusInput(){}},
    controlEffect(){},stop(){this.dialogs.closeDialog();this.busy=false;this.localBusy=false;this.pending=null;this.finishControl("cancelled")},...overrides}
  s.root=s
  s.call=value=>{s.calls.push(value);if(value.method==="save-state")s.saves.push(W.copy(value.state))}
  s.stop=()=>{s.dialogs.closeDialog();s.busy=false;s.localBusy=false;s.pending=null;s.finishControl("cancelled")}
  for(const name of ["save","controlSnapshot","finishControl","handleControl","dialogsBusy","nextStep"]){
    const at=source.indexOf("  function "+name+"("),tail=source.slice(at),end=tail.slice(1).search(/\n  (?:function |Timer |Process |Hud |ShellIpc )/)
    runInNewContext(tail.slice(0,end<0?undefined:end+1)+"\n",s)
  }
  return s
}
const send=(s,body,path="/v1/control")=>{s.handleControl({ticket:1,body,path});return s.calls.findLast(c=>c.method==="control-reply")}

test("HTTP help is the actual native manual and does not open a run even while busy",()=>{
  const s=service({busy:true}),before=JSON.stringify(s.environment),reply=send(s,{},"/v1/help")
  expect(reply.result).toEqual({platform:"wayland",topic:"registry_hub",text:D.help()})
  expect(s.controlRun).toBeNull();expect(s.dialogs.dialogOpen).toBe(false)
  expect(s.saves).toHaveLength(0);expect(JSON.stringify(s.environment)).toBe(before)
})

test("no-replace overrides are used during work but never saved, then restored on completion",()=>{
  const s=service(),before=JSON.stringify(s.environment),result=send(s,{menu:2,inputs:["A","run-only"],config:{vars:{S:"also temporary"}},no_replace:true})
  expect(result.error).toBeUndefined();expect(s.environment.vars.A).toBe("run-only");expect(s.saves).toHaveLength(0)
  s.dialogs.closeDialog();s.finishControl()
  expect(JSON.stringify(s.environment)).toBe(before);expect(s.controlLast.environment.vars.A).toBe("run-only");expect(s.saves).toHaveLength(0)
})
test("normal requests save only their changes and preserve the rest of the config",()=>{
  const s=service();send(s,{config:{vars:{A:"saved"}},no_replace:false})
  expect(s.saves.at(-1).vars.A).toBe("saved");expect(s.saves.at(-1).vars.S).toBe("kept")
  s.finishControl();expect(s.environment.vars.A).toBe("saved")
})
test("busy work rejects a new run; matching run can continue; stop restores temporary config",()=>{
  const s=service();send(s,{menu:2,config:{vars:{A:"temporary"}},no_replace:true});const id=s.controlRun.id
  expect(send(s,{menu:3}).error.status).toBe(409)
  expect(send(s,{run:id,inputs:["A","continued"]}).error).toBeUndefined();expect(s.environment.vars.A).toBe("continued")
  s.busy=true;expect(send(s,{run:id,inputs:["H"]}).error.status).toBe(409)
  expect(send(s,{},"/v1/stop").result.stop_requested).toBe(true);expect(s.environment.vars.A).toBe("original")
})
test("invalid batches and stale run IDs do not change live configuration",()=>{
  const s=service(),before=JSON.stringify(s.environment)
  expect(send(s,{menu:2,inputs:["A","changed","invalid-command"]}).error.status).toBe(400)
  expect(JSON.stringify(s.environment)).toBe(before);expect(s.controlRun).toBeNull()
  expect(send(s,{run:"expired",inputs:["Z"]}).error.status).toBe(404)
})
test("oversized merges and wrong-stage actions fail before changing config",()=>{
  const original=env();original.vars.A="a".repeat(700000)
  expect(()=>C.prepare({config:{vars:{S:"b".repeat(500000)}}},original,cursor,null,W,D)).toThrow("1 MiB")
  expect(original.vars.S).toBe("kept")
  expect(()=>C.prepare({menu:2,inputs:["A",{action:"play"}]},env(),cursor,null,W,D)).toThrow("Finish the current")
})
test("temporary runs retain previously pending user saves and report silent async failures",()=>{
  const s=service();s.persist.restart();send(s,{menu:2,no_replace:true})
  expect(s.persist.running).toBe(false)
  s.connected=false;s.busy=true;s.generation=1;s.steps=[{kind:"regedited",action:"read",file:"file",reference:"x"}];s.stepIndex=0
  s.call=(request,callback)=>callback(null,"offline")
  s.nextStep(1);expect(s.error).toBe("");expect(s.controlRun.failure).toBe("offline")
  s.finishControl();expect(s.controlLast.error).toBe("offline");expect(s.persist.running).toBe(true)
})
test("delayed tray captures are busy and menu seven is always stop",()=>{
  const s=service({activePanel:{captureAction:()=>{}}})
  expect(send(s,{menu:2}).error.status).toBe(409)
  s.busy=true;expect(send(s,{menu:7}).error).toBeUndefined();expect(s.busy).toBe(false)
})
test("coordinate reads use the HUD physical snapshot even while busy, without changing state",()=>{
  const s=service({busy:true,physicalCursor:{x:123.6,y:-45.2}}),before=JSON.stringify(s.environment)
  expect(send(s,{},"/v1/coords").result).toEqual({x:124,y:-45})
  expect(JSON.stringify(s.environment)).toBe(before);expect(s.controlRun).toBeNull();expect(s.saves).toHaveLength(0)
  s.ready=false;expect(send(s,{},"/v1/coords").error.status).toBe(503)
})
