import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
const read=name=>readFileSync(new URL(name,import.meta.url),"utf8")
const W=runInNewContext(read("../qml/model/Workflow.js")+"\n({compile,waitCount,remainingWaits,copy,resolve,move,keys,wait,numeric})")
const D=runInNewContext(read("../qml/model/Dialogs.js")+"\n({open,submit})")
const env=()=>({script:"",vars:{A:"yes",S:"",D:"",F:""},buffers:{Z:"buffer",X:"",C:"",V:""},points:Array(16).fill(null),zones:{},shells:{X:"bash",C:"sh",G:"python",P:"pwsh",T:""}})
const context=()=>({deferred:true,profiles:{},tasks:[],status:{},shortcuts:{terminal:W.keys("SUPER+ENTER")}})

test("nested literal braces never become waits or branches, including profile resumptions",()=>{
  const c=context(),text='{\n"fake": "{wait 5}{if nope}{endif}", "object": {}\n}'
  c.profiles.json={script:"{set cbuf "+text+"}{type $C}{wait 2}{type "+text+"}"}
  expect(W.waitCount("{profile json}",c)).toBe(1)
  const first=W.compile("{profile json}{type done}",env(),c)
  expect(first.steps[0].actions[0].content).toBe(text)
  expect(W.remainingWaits(first.next,c)).toBe(1)
  const second=W.compile("",first.state,c,first.next)
  expect(second.steps[0].actions.map(a=>a.type)).toEqual(["wait","paste","paste"])
  expect(second.steps[0].actions[1].content).toBe(text)
  expect(second.steps[0].actions[2].content).toBe("done");expect(second.next).toBeNull()
  expect(()=>W.compile("{set A changed}{wait 1}{type {broken}",env(),c)).toThrow("Missing closing brace")
})
function savePoint(environment,name,at){
  let s=D.open("registry_hub",at);s=D.submit(s,name,environment,at,W).state
  const result=D.submit(s,"s",environment,at,W)
  for(const effect of result.effects)if(effect.kind==="change")environment[effect.section][effect.key]=effect.value
  return result
}

test("Alt+0 exports captured actions without overwriting Registry Hub points or variables",()=>{
  for(const name of ["3","point 3","A"]){
    const e=env(),before=JSON.stringify(e),at={x:-12,y:345}
    let s=D.open("save_cursor",at);s=D.submit(s,name,e,at,W).state
    const result=D.submit(s,"z",e,{x:999,y:999},W)
    expect(result.effects).toHaveLength(1);expect(result.effects[0].kind).toBe("export")
    expect(result.effects[0].actions[0].cursormovsmap[0].slice(2)).toEqual([-12,345])
    expect(JSON.stringify(e)).toBe(before)
  }
})
test("each next native task begins with its explicit wait and expands only at dispatch",()=>{
  const e=env(),c=context(),source="{type first}{wait 200}{point 3}{paste A}{wait 0}{type $A}"
  const first=W.compile(source,e,c)
  expect(first.steps[0].actions.map(a=>a.type)).toEqual(["paste"])
  expect(W.waitCount(source,c)).toBe(2)
  savePoint(first.state,"3",{x:40,y:50});first.state.vars.A="second"
  const second=W.compile("",first.state,c,first.next)
  expect(second.steps[0].actions[0]).toEqual({type:"wait",duration:.2})
  expect(second.steps[0].actions[1].cursormovsmap[0].slice(2)).toEqual([40,50])
  expect(second.steps[0].actions[2].content).toBe("second")
  second.state.vars.A="third"
  const third=W.compile("",second.state,c,second.next)
  expect(third.steps[0].actions[0]).toEqual({type:"wait",duration:0})
  expect(third.steps[0].actions[1].content).toBe("third");expect(third.next).toBeNull()
})
test("branch decisions and click mode survive a wait without reevaluating the branch",()=>{
  const first=W.compile("{click on}{if A == yes}{type first}{wait 1}{buf Z}{else}{type wrong}{endif}",env(),context())
  first.state.vars.A="no"
  const second=W.compile("",first.state,context(),first.next)
  expect(second.steps[0].actions.map(a=>a.type)).toEqual(["wait","keyssequence","paste"])
  expect(second.steps[0].actions[2].content).toBe("buffer")
})
test("native wait boundaries preserve multiline payloads and literal backslash-n",()=>{
  const first=W.compile("{type first}{wait 2}{set A second\n  line\\n}{type $A}",env(),context())
  const second=W.compile("",first.state,context(),first.next)
  expect(second.steps[0].actions[0].type).toBe("wait")
  expect(second.steps[0].actions[1].content).toBe("second\n  line\\n")
})
test("profile continuations retain their return frame and enclosing branch",()=>{
  const c=context();c.profiles.route={script:"{type in}{wait 1}{point 3}"}
  const first=W.compile("{if A == yes}{profile route}{type out}{endif}",env(),c)
  expect(W.waitCount("{profile route}",c)).toBe(1)
  first.state.points[2]=[8,9]
  const second=W.compile("",first.state,c,first.next)
  expect(second.steps[0].actions[0].type).toBe("wait")
  expect(second.steps[0].actions[1].cursormovsmap[0].slice(2)).toEqual([8,9])
  expect(second.steps[0].actions[2].content).toBe("out");expect(second.next).toBeNull()
})
test("only active explicit wait tokens split; internal helper waits and literal text do not",()=>{
  const plan=W.compile("{if A == no}{wait 20}{endif}{bash}{type wait 10}",env(),context())
  expect(plan.next).toBeNull();expect(plan.steps[0].actions.some(a=>a.type==="wait")).toBe(true)
  expect(plan.steps[0].actions.at(-1).content).toBe("wait 10")
  expect(()=>W.compile("{type no}{wait 1}{if A == yes}",env(),context())).toThrow("Missing endif")
})
test("leading, consecutive and trailing waits retain separate native actions",()=>{
  let plan=W.compile("{wait 0}{wait 2}",env(),context())
  expect(plan.steps).toHaveLength(0)
  plan=W.compile("",plan.state,context(),plan.next);expect(plan.steps[0].actions).toEqual([{type:"wait",duration:0}])
  plan=W.compile("",plan.state,context(),plan.next);expect(plan.steps[0].actions).toEqual([{type:"wait",duration:.002}]);expect(plan.next).toBeNull()
})

function service(){
  const timer=()=>({restart(){},stop(){}}),posts=[],s={Workflow:W,environment:env(),generation:0,busy:false,localBusy:false,pending:null,dispatch:null,steps:[],stepIndex:0,currentJob:null,serial:0,hidden:false,controlRun:null,
    startDelay:timer(),poll:timer(),persist:timer(),clipboardTimeout:timer(),dialogs:{closeDialog(){}},emergency:{running:false},save(){},hidePanel(){},refresh(){},remember(){},change(section,key,value){if(section)this.environment[section][key]=value;else this.environment[key]=value},console:{info(){}},
    refreshContext(callback){callback(context(),null)},http(method,path,body,callback){posts.push({method,path,body});callback({task:{task_number:posts.length}},null)}}
  s.root=s
  const source=read("../qml/Service.qml")
  for(const name of ["assemble","play","nextDispatch","nextStep","queue","cancelWork","stop"]){
    const tail=source.slice(source.indexOf("  function "+name+"(")),end=tail.slice(1).search(/\n  (?:function |Timer |Process |Hud |ShellIpc )/)
    runInNewContext(tail.slice(0,end<0?undefined:end+1),s)
  }
  return {s,posts}
}
test("service waits for prior task completion, reads fresh state, and names each native segment",()=>{
  const {s,posts}=service();s.play("{type first}{wait 12}{point 3}");s.pending();s.pending=null
  expect(posts).toHaveLength(1);expect(posts[0].body.name).toBe("Macrohelp sequence 1 of 2")
  expect(s.dispatch.index).toBe(1)
  s.environment.points[2]=[77,88];s.nextStep(s.generation)
  expect(posts).toHaveLength(2);expect(posts[1].body.name).toBe("Macrohelp sequence 2 of 2")
  expect(posts[1].body.task.actions[0]).toEqual({type:"wait",duration:.012})
  expect(posts[1].body.task.actions[1].cursormovsmap[0].slice(2)).toEqual([77,88])
})
test("Stop invalidates continuation preparation and prevents any subsequent native submission",()=>{
  const {s,posts}=service();s.play("{type first}{wait 1}{type second}");s.pending();s.pending=null
  let callback;s.refreshContext=f=>callback=f;s.nextStep(s.generation)
  expect(s.dispatch.preparing).toBe(true);s.stop();callback(context(),null)
  expect(s.dispatch).toBeNull();expect(s.busy).toBe(false);expect(posts).toHaveLength(1)
})
