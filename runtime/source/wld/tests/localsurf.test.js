import {test, expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
import {spawnSync} from "node:child_process"
import {fileURLToPath} from "node:url"

const model = name => runInNewContext(readFileSync(new URL(`../qml/model/${name}.js`,import.meta.url),"utf8")+
  (name==="Workflow"?"\n({compile,copy,viewPatch,coordinateValues,move})":name==="Dialogs"?"\n({open,submit,field})":"\n({prepare})"))
const W=model("Workflow"),D=model("Dialogs"),C=model("Control")
const dir=fileURLToPath(new URL("../../mcp/localsurf/",import.meta.url))
const python=process.env.LOCALSURF_TEST_PYTHON||(process.platform==="win32"?"python":"python3")
function translate(name,args){
  const code="import sys,json;from workflows import compile_tool;m=json.load(sys.stdin.buffer);sys.stdout.buffer.write(json.dumps(compile_tool(m['name'],m['args'])).encode('utf-8'))"
  const out=spawnSync(python,["-c",code],{cwd:dir,input:JSON.stringify({name,args}),encoding:"utf8"})
  if(out.status!==0)throw Error(out.stderr)
  return JSON.parse(out.stdout)
}
function environment(){return {version:1,script:"saved",registry:"",shells:{X:"",C:"",G:"",P:"",T:""},vars:{A:"",S:"",D:"",F:""},buffers:{Z:"saved clipboard buffer",X:"",C:"",V:""},zones:{Z:null,X:null,C:null,V:null},points:Array(16).fill(null),paths:{import:"",export:""},view:{grid:false}}}
function compile(request){
  const original=environment()
  const plan=C.prepare(request,original,{x:120,y:100},null,W,D,[])
  expect(plan.effects[0].kind).toBe("play")
  expect(plan.closed).toBe(true)
  expect(original.script).toBe("saved")
  return W.compile(plan.environment.script,plan.environment,{shortcuts:{Z:W.compile("{PRINT_SCREEN}",original,{}).steps[0].actions[0]}})
}

const nativeTest=process.env.MACROHELP_CPP_CONTROL_TEST?test:test.skip
nativeTest("translated requests preserve the native C++ schema contract when its existing host harness is provided",()=>{
  const requests=[translate("localsurf_keys",{keys:"ggG<Ctrl-i>"}),translate("localsurf_type",{text:"a,b\n{unbalanced"}),
    translate("localsurf_workflow",{name:"scrsht"}),translate("localsurf_workflow",{name:"find",text:"test"})]
  const output=spawnSync(process.env.MACROHELP_CPP_CONTROL_TEST,["--normalize"],{input:requests.map(v=>JSON.stringify(v)).join("\n")+"\n",encoding:"utf8"})
  expect(output.status).toBe(0)
  const replies=output.stdout.trim().split("\n").map(s=>JSON.parse(s))
  replies.forEach((reply,i)=>{expect(reply.ok).toBe(true);expect(reply.result).toEqual(requests[i])})
})

test("translated keys pass through real Control -> dialog Play -> native compiler without collapsing sequences",()=>{
  const request=translate("localsurf_keys",{keys:"ggG<Ctrl-i>?<<>>{}<Ctrl->>"})
  const actions=compile(request).steps.flatMap(s=>s.actions||[])
  const keys=actions.filter(a=>a.type==="keyssequence").map(a=>Object.values(a.keysmap).flatMap(v=>v[1]))
  expect(keys[0]).toEqual(["G"]);expect(keys[1]).toEqual(["G"])
  expect(keys[2]).toEqual(["SHIFT_LEFT","G"])
  expect(keys[3]).toEqual(["CONTROL_LEFT","I"])
  expect(keys[4]).toEqual(["question"])
  expect(keys[9]).toEqual(["braceleft"]);expect(keys[10]).toEqual(["braceright"])
  expect(keys[11]).toEqual(["CONTROL_LEFT","greater"])
})

test("literal multiline text survives real parser and keeps command-looking text and variables inert",()=>{
  const text="{unbalanced\n{screen}{wait 1}\n$Z,$REGISTRY _aj \\n 😅"
  const request=translate("localsurf_type",{text})
  const plan=compile(request)
  expect(plan.steps).toHaveLength(1)
  expect(plan.steps[0].actions).toHaveLength(1)
  expect(plan.steps[0].actions[0]).toEqual({type:"paste",content:text,contentId:"",loop:1})
})

test("compound find keeps native deferred-wait sequencing and does not turn search text into registry tokens",()=>{
  const request=translate("localsurf_workflow",{name:"find",text:"some, text"})
  const plan=compile(request)
  const actions=plan.steps.flatMap(s=>s.actions||[])
  expect(actions.map(a=>a.type)).toEqual(["keyssequence","wait","paste","keyssequence"])
  expect(actions[1].duration).toBe(.3);expect(actions[2].content).toBe("some, text")
})

test("capture translation uses the native picker schedule only",()=>{
  const request=translate("localsurf_workflow",{name:"scrsht"})
  const plan=compile(request)
  expect(plan.steps[0].capture).toBe(true)
  expect(plan.steps[0].actions.map(a=>a.type)).toEqual(["keyssequence","wait"])
  expect(plan.steps[0].actions[0].keysmap[0][1]).toEqual(["PRINT_SCREEN"])
})
