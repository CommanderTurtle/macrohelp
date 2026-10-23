import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
const read=name=>readFileSync(new URL(name,import.meta.url),"utf8")
const W=runInNewContext(read("../qml/model/Workflow.js")+"\n({compile,copy,rgdReference,keys,wait,remainingWaits,resolve})")
const env=()=>({registry:"/home/me/workflow.txt",vars:{A:"initial",S:"",D:"",F:""},buffers:{Z:"",X:"",C:"",V:""},points:Array(16).fill(null),zones:{},shells:{}})
const ctx=(lookup={},deferred=false)=>({profiles:{},tasks:[],status:{},rgd:lookup,deferred})
const key=(ref,file=env().registry)=>JSON.stringify([file,ref])
const contents=plan=>plan.steps.flatMap(s=>s.actions||[]).filter(a=>a.type==="paste").map(a=>a.content)

test("string and zone includes use normal tokens, nesting and multiline literals without changing the editor",()=>{
  const e=env(),before=JSON.stringify(e),source="{rgd i6s6}{type $A}",text='{\n"nested": {}, "literal": "\\n"\n}'
  const c=ctx({[key("i6s6")]:"{set A resolved}{rgd i6z6}",[key("i6z6")]:"{type "+text+"}"})
  const plan=W.compile(source,e,c)
  expect(contents(plan)).toEqual([text,"resolved"])
  expect(JSON.stringify(e)).toBe(before);expect(source).toBe("{rgd i6s6}{type $A}")
  expect(W.compile("{rgd i6z6}{rgd i6z6}",e,c).steps[0].actions).toHaveLength(2)
})

test("includes are requested on demand with the live Registry file and never inside disabled or literal payloads",()=>{
  const e=env(),source="{set registry /home/me/new file}{rgd i6z6}"
  let pending;try{W.compile(source,e,ctx())}catch(error){pending=error.rgd}
  expect(pending).toEqual({file:"/home/me/new file",reference:"i6z6",key:key("i6z6","/home/me/new file")})
  expect(e.registry).toBe(env().registry)
  const c=ctx({[pending.key]:"{type fetched}"})
  expect(contents(W.compile(source,e,c))).toEqual(["fetched"])
  expect(contents(W.compile("{if A == no}{rgd i6s6}{endif}{type {rgd i6s6}}{set C {rgd i6s6}}",e,ctx()))).toEqual(["{rgd i6s6}"])
})

test("native waits inside includes retain outer/profile return frames and expand live values at the next dispatch",()=>{
  const c=ctx({[key("i6s6")]:"{type first}{wait 20}{point 3}{type $A}"},true)
  c.profiles.nested={script:"{rgd i6s6}{type profile done}"}
  let plan=W.compile("{profile nested}{type outer}",env(),c)
  expect(contents(plan)).toEqual(["first"]);expect(W.remainingWaits(plan.next,c)).toBe(1)
  plan.state.vars.A="updated";plan.state.points[2]=[1280,720]
  plan=W.compile("",plan.state,ctx({},true),plan.next)
  expect(plan.steps[0].actions[0]).toEqual({type:"wait",duration:.020})
  expect(plan.steps[0].actions[1].cursormovsmap[0].slice(2)).toEqual([1280,720])
  expect(contents(plan)).toEqual(["updated","profile done","outer"]);expect(plan.next).toBeNull()
})

test("an include beyond a wait is not read ahead, and malformed included content fails with its selector",()=>{
  let c=ctx({},true),plan=W.compile("{type first}{wait 2}{rgd i6s6}",env(),c),pending
  expect(contents(plan)).toEqual(["first"])
  try{W.compile("",plan.state,c,plan.next)}catch(error){pending=error.rgd}
  expect(pending.reference).toBe("i6s6")
  for(const bad of ["{type {broken}","{if A == yes}{type x}","{else}","{endif}","plain text"]){
    expect(()=>W.compile("{rgd i6s6}",env(),ctx({[key("i6s6")]:bad}))).toThrow("rgd i6s6:")
  }
})

test("include selectors, recursion through presets and cumulative expansion are bounded",()=>{
  for(const ref of ["i6","i6s","i6x6","--help","i6s6;echo x"])expect(()=>W.compile("{rgd "+ref+"}",env(),ctx())).toThrow("Use {rgd")
  expect(()=>W.compile("{rgd i6s6}",{...env(),registry:""},ctx())).toThrow("file path")
  const c=ctx({[key("i6s6")]:"{profile loop}"});c.profiles.loop={script:"{rgd i6s6}"}
  expect(()=>W.compile("{rgd i6s6}",env(),c)).toThrow("Recursive rgd include")
  const huge="{type "+"x".repeat(530000)+"}"
  expect(()=>W.compile("{rgd i6s6}{rgd i6z6}",env(),ctx({[key("i6s6")]:huge,[key("i6z6")]:huge}))).toThrow("1 MiB")
})

function service(){
  const reads=[],posts=[],timer=()=>({restart(){},stop(){}})
  const s={Workflow:W,environment:env(),generation:0,busy:false,localBusy:false,pending:null,dispatch:null,steps:[],stepIndex:0,currentJob:null,serial:0,hidden:false,controlRun:null,
    startDelay:timer(),poll:timer(),persist:timer(),clipboardTimeout:timer(),dialogs:{closeDialog(){}},emergency:{running:false},save(){},hidePanel(){},refresh(){},remember(){},change(section,key,value){if(section)this.environment[section][key]=value;else this.environment[key]=value},console:{info(){}},
    refreshContext(callback){callback(ctx({},true),null)},call(request,callback){reads.push({request,callback})},
    http(method,path,body,callback){posts.push({method,path,body});callback({task:{task_number:posts.length}},null)}}
  s.root=s;const source=read("../qml/Service.qml")
  for(const name of ["assemble","play","nextDispatch","nextStep","queue","cancelWork","stop"]){const tail=source.slice(source.indexOf("  function "+name+"(")),end=tail.slice(1).search(/\n  (?:function |Timer |Process |Hud |ShellIpc )/);runInNewContext(tail.slice(0,end<0?undefined:end+1),s)}
  return {s,reads,posts}
}
test("Service resolves via Run Command IPC before sending any actions and discards stopped replies",()=>{
  for(const stop of [false,true]){
    const {s,reads,posts}=service();s.play("{rgd i6s6}{type outer}")
    expect(reads).toHaveLength(1);expect(reads[0].request).toEqual({method:"rgd-read",file:env().registry,reference:"i6s6"})
    expect(posts).toHaveLength(0);expect(s.busy).toBe(true)
    if(stop)s.stop()
    reads[0].callback({text:"{type included}"},null)
    if(stop){expect(s.pending).toBeNull();expect(posts).toHaveLength(0)}
    else{s.pending();expect(posts).toHaveLength(1);expect(posts[0].path).toBe("/temp-task");expect(posts[0].body.task.actions.map(a=>a.content)).toEqual(["included","outer"])}
  }
  const {s,reads,posts}=service();s.play("{rgd i6s6}");reads[0].callback(null,"Run Command permission required")
  expect(s.busy).toBe(false);expect(s.error).toContain("rgd i6s6: Run Command permission required");expect(posts).toHaveLength(0)
})

test("include adapter does not add routes or invoke a local subprocess",()=>{
  const source=read("../src/rgd.rs")
  expect(source).not.toMatch(/Command::|\.spawn\(|std::process::Command/)
  expect(source).toContain('http("POST", "/temp-task"')
  expect(read("../qml/Service.qml")).not.toContain('"/rgd/read"')
  expect(read("../../src/registry_rgd.inc")).not.toMatch(/CreateProcess|ShellExecuteW|L"\/rgd\/read"/)
})
