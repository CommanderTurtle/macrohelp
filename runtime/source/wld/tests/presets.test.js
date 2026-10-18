import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
const W=runInNewContext(readFileSync(new URL("../qml/model/Workflow.js",import.meta.url),"utf8")+"\n({compile,mergePreset,presetName})")
function state(){return {version:1,script:"original",registry:"",shells:{X:"bash",C:"sh",G:"python"},vars:{A:"existing",S:"kept",D:"",F:""},buffers:{Z:"",X:"",C:"",V:""},zones:{Z:null,X:null,C:null,V:null},points:Array(16).fill(null),view:{keys:true},last_task:null}}
test("partial presets inline their code, merge only selected settings and keep the source",()=>{
  const before=state(),profiles={"1 - Writing":{script:"{type $A}{profile Nested}",vars:{A:"saved"}},Nested:{script:"{type $S}"}}
  const plan=W.compile("{profile 1}{ENTER}",before,{profiles})
  expect(plan.steps[0].actions.map(a=>a.content).filter(Boolean)).toEqual(["saved","kept"])
  expect(plan.expanded).toBe("{type saved}{type kept}{enter}")
  expect(plan.state.script).toBe("original");expect(before.vars.A).toBe("existing");expect(plan.state.vars.S).toBe("kept")
})
test("presets have bounded expansion and cannot escape conditional scopes",()=>{
  for(const profiles of [{A:{script:"{profile B}"},B:{script:"{profile A}"}},{A:{script:"{endif}"}},{A:{script:"{if A notempty}"}}])expect(()=>W.compile("{profile A}",state(),{profiles})).toThrow()
  expect(()=>W.presetName("1",{"1 - A":{},"1 - B":{}})).toThrow("Ambiguous")
  expect(()=>W.compile("{profile missing}",state(),{profiles:{}})).toThrow("Preset not found")
  expect(W.compile("{if A empty}{profile missing}{endif}{click}",state(),{profiles:{}}).steps).toHaveLength(1)
})
test("ordinary workflows do not depend on a healthy preset library",()=>{
  expect(W.compile("{click}",state(),{profileError:"Damaged preset"}).steps).toHaveLength(1)
  expect(()=>W.compile("{profile 1}",state(),{profileError:"Damaged preset"})).toThrow("Damaged preset")
  expect(()=>W.mergePreset(state(),{events:[]})).toThrow("Unknown preset section")
})
test("resolved variable expansion is bounded even outside presets",()=>{
  const s=state();s.vars.A="x".repeat(600000)
  expect(()=>W.compile("{type $A}{type $A}",s,{})).toThrow("Expanded workflow exceeds")
})
