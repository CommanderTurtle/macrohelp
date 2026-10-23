import { test, expect } from "bun:test"
import { readFileSync } from "node:fs"
import { runInNewContext } from "node:vm"

// Exercise the actual service functions without a compositor or input injection.
const source=readFileSync(new URL("../qml/Service.qml",import.meta.url),"utf8")
function service(overrides={}) {
  const timer=()=>({running:false,restart(){this.running=true},stop(){this.running=false}})
  const s={generation:0,busy:false,localBusy:false,pending:null,steps:[],stepIndex:0,controlRun:null,
    hidden:false,currentJob:null,dispatch:null,recordingPath:false,error:"",message:"",environment:{},
    clipboardCallback:null,clipboardSerial:0,activePanel:{opened:true},page:"Workflow",settings:{},
    startDelay:timer(),poll:timer(),clipboardTimeout:timer(),persist:timer(),dialogWarp:timer(),dialogBlink:null,
    save(){},hidePanel(){},showPanel(){},remember(){},change(){},refresh(){},
    emergency:{running:false},runtime:{running:true},dialogs:{closeDialog(){}},...overrides}
  s.root=s
  for(const name of ["play","cancelWork","stop","queue","runSaved","saveSchedule","refreshContext","readClipboard","finishClipboard","reconnect","panel","blinkDialog","dialogClosed","restoreDialogPointer","finishDialogBlink"]) {
    const at=source.indexOf("  function "+name+"(")
    if(at<0)throw Error("Missing service function "+name)
    const tail=source.slice(at),end=tail.slice(1).search(/\n  (?:function |Timer |Process |Hud |ShellIpc )/)
    runInNewContext(tail.slice(0,end<0?undefined:end+1)+"\n",s)
  }
  return s
}

test("preparing Play excludes duplicate starts; Stop invalidates its callback",()=>{
  let prepared,calls=0
  const s=service({Workflow:{compile(){throw Error("stale compile")}}})
  s.refreshContext=callback=>{calls++;prepared=callback}
  s.play("{click}");expect(s.busy).toBe(true)
  s.play("{click}");expect(calls).toBe(1)
  s.stop();prepared({},null)
  expect(s.busy).toBe(false);expect(s.pending).toBeNull()
  expect(s.startDelay.running).toBe(false)
})

test("failed preparation and compiler errors release the busy state",()=>{
  const s=service({Workflow:{compile(){throw Error("Invalid command")}}})
  s.refreshContext=callback=>callback(null,"offline")
  s.play("{click}");expect(s.busy).toBe(false)
  s.refreshContext=callback=>callback({},null)
  s.play("{bad}");expect(s.busy).toBe(false);expect(s.error).toBe("Invalid command")
})

test("successful preparation schedules its own generation only",()=>{
  const s=service({Workflow:{remainingWaits(){return 0},compile(){return {state:{script:"ok"},steps:[{kind:"input"}]}}}})
  s.refreshContext=callback=>callback({},null)
  s.play("ok")
  expect(s.busy).toBe(true);expect(s.startDelay.running).toBe(true)
  expect(s.environment.script).toBe("ok");expect(s.steps).toHaveLength(1)
  let token;s.nextStep=value=>token=value;s.pending()
  expect(token).toBe(s.generation)
})

test("each context request failure reaches its caller",()=>{
  for(const failureAt of [1,2,3]) {
    let requests=0,result
    const s=service({Workflow:{desktopShortcuts(){return {}}}})
    const reply=callback=>{requests++;callback(requests===failureAt?null:{tasks:[]},requests===failureAt?"offline":null)}
    s.http=(verb,path,body,callback)=>reply(callback);s.call=(request,callback)=>reply(callback)
    s.refreshContext((value,error)=>result={value,error})
    expect(requests).toBe(failureAt);expect(result.error).toBe("offline")
  }
})

test("cancel releases timers, recording and clipboard before delayed work can run",()=>{
  const s=service({busy:true,localBusy:true,hidden:true,pending:()=>{},currentJob:{task_number:1},recordingPath:true,clipboardCallback:()=>{}})
  for(const timer of [s.startDelay,s.poll,s.clipboardTimeout])timer.restart()
  s.cancelWork()
  expect(s.generation).toBe(1)
  for(const key of ["busy","localBusy","hidden","recordingPath"])expect(s[key]).toBe(false)
  for(const key of ["pending","currentJob","clipboardCallback"])expect(s[key]).toBeNull()
  for(const timer of [s.startDelay,s.poll,s.clipboardTimeout])expect(timer.running).toBe(false)
})

test("late queue and saved-run replies do not mutate state after Stop",()=>{
  for(const action of [s=>s.queue([{type:"wait"}]),s=>s.runSaved("Saved")]) {
    let reply,changes=0
    const s=service({serial:0,change(){changes++}})
    s.http=(verb,path,body,callback)=>reply=callback
    action(s);s.stop();reply({task:{task_number:1},task_number:1},null)
    expect(changes).toBe(0);expect(s.localBusy).toBe(false)
  }
})

test("clipboard is read once and a late timed-out read cannot complete a newer one",()=>{
  const replies=[],seen=[]
  const s=service({clipboardRequested:callback=>replies.push(callback)})
  s.readClipboard((value,error)=>seen.push([value,error]))
  expect(s.page).toBe("Workflow");expect(s.localBusy).toBe(true)
  s.finishClipboard(null,"timeout")
  s.readClipboard((value,error)=>seen.push([value,error]))
  replies[0]({text:"old"},null)
  expect(seen).toHaveLength(1);expect(s.localBusy).toBe(true)
  replies[1]({text:"new"},null);replies[1]({text:"duplicate"},null)
  expect(seen).toHaveLength(2);expect(seen[1][0].text).toBe("new")
  expect(s.localBusy).toBe(false);expect(s.clipboardTimeout.running).toBe(false)
})

test("quick dialog owns clipboard focus when the tray panel is closed",()=>{
  let result,read
  const s=service({activePanel:{opened:false},dialogs:{readClipboard(callback){read=callback}}})
  s.readClipboard((value,error)=>result={value,error})
  expect(s.localBusy).toBe(true);expect(result).toBeUndefined()
  read({text:"captured text"},null)
  expect(result.value.text).toBe("captured text");expect(s.localBusy).toBe(false)
})

test("stopped clients can reconnect without starting a server",()=>{
  const s=service();s.runtime.running=false;s.reconnect()
  expect(s.runtime.running).toBe(true)
  expect(source).toContain("root.cancelWork();persist.stop()")
})
function blinkService(){
  const calls=[],s=service({ready:true,dialogs:{dialogOpen:true,dialogState:{origin:[-120,240]},revision:3,working:false,reanchor(){this.anchored=true}}})
  s.dialogs.closeDialog=()=>{s.dialogs.dialogOpen=false;s.dialogClosed(false,false)}
  s.http=(method,path,body,callback)=>calls.push({method,path,body,callback})
  return {s,calls}
}
test("dismissal returns to frozen entry coords via one memory-only native blink task",()=>{
  const {s,calls}=blinkService();s.blinkDialog("Blink to 1920x2124 (center bottom)")
  const lease=s.dialogBlink;s.dialogs.dialogState.origin=[1920,2124]
  calls[0].callback({},null);s.finishDialogBlink(lease)
  expect(s.dialogs.anchored).toBe(true);expect(s.dialogs.working).toBe(false)
  s.generation++ // Editing/resetting a workflow does not dismiss this popup.
  s.dialogClosed(true,false);s.dialogClosed(true,false)
  expect(calls).toHaveLength(2);expect(calls[1].path).toBe("/temp-task")
  expect(calls[1].body).toMatchObject({run:true,cleanup:true,delay:0,task:{auto_start:false,allow_system:false,delay_seconds:0,loop_times:1,actions:[{type:"move",x:-120,y:240,duration_ms:20}]}})
  expect(s.dialogBlink).toBeNull();expect(s.environment).toEqual({})
})
test("closing before the entry blink acknowledges waits, while a replaced lease cannot jump back",()=>{
  const {s,calls}=blinkService();s.blinkDialog("center")
  const first=s.dialogBlink;s.dialogClosed(true,false)
  expect(calls).toHaveLength(1)
  calls[0].callback({},null);s.finishDialogBlink(first)
  expect(calls).toHaveLength(2)
  s.blinkDialog("next");const next=s.dialogBlink
  s.blinkDialog("newest");s.finishDialogBlink(next)
  expect(calls.filter(c=>c.path==="/temp-task")).toHaveLength(1)
})
test("programmatic action closes and Stop never enqueue a stray return; circles have no blink lease",()=>{
  const {s,calls}=blinkService();s.dialogClosed(true,false);expect(calls).toHaveLength(0)
  s.blinkDialog("center");const lease=s.dialogBlink
  s.dialogClosed(false,false);calls[0].callback({},null);s.finishDialogBlink(lease)
  expect(calls).toHaveLength(1)
  s.blinkDialog("center");const stopped=s.dialogBlink;s.stop()
  s.finishDialogBlink(stopped);expect(calls).toHaveLength(2)
})
