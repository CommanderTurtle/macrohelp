import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
import {runInNewContext} from "node:vm"
const load=(file,names)=>runInNewContext(readFileSync(new URL("../qml/model/"+file,import.meta.url),"utf8")+"\n({"+names+"})")
const W=load("Workflow.js","keys,keyName,expression,vector,circle,clearState,pasteSequence,compile")
const K=load("KeyTools.js","categories,chord,append")
const state=()=>({script:"old",registry:"/tmp/file",shells:{X:"fish",C:"sh",G:"python",P:"pwsh",T:"SUPER+RETURN"},paths:{import:"a",export:"b"},vars:{A:"keep?",S:"",D:"",F:""},buffers:{Z:"one",X:"two",C:"",V:""},zones:{Z:[1,2,3,4],X:null,C:null,V:null},points:[[10,20],[30,40],...Array(14).fill(null)],view:{panel:false,keys:true},last_task:{task_number:12}})
test("Windows key alias vocabulary maps to native backend names",()=>{
  const groups={CONTROL_LEFT:["ctrl","left-ctrl","ctrl_left","left control","lcontrol"],CONTROL_RIGHT:["rctrl","control-right"],SHIFT_RIGHT:["rshift","right_shift"],ALT_GR:["right_alt","ralt"],WINDOWS:["win","rightwin","super","meta"],SPACE:["spacebar"],ENTER:["return"],TAB:["tab"],HOME:["home"],END:["end"],LEFT_ARROW:["arrow-left"],PAGE_DOWN:["pgdn"],MOUSE_WHEEL_UP:["wheelup","mouse_wheel_up","scrollup"],PRINT_SCREEN:["printscreen"]}
  for(const [name,aliases] of Object.entries(groups))for(const alias of aliases)expect(W.keyName(alias)).toBe(name)
  expect(W.keys(["CTRL","+"],600000).keysmap[0]).toEqual([600000,["CONTROL_LEFT","+"]])
  for(const hold of [-1,0,600001,Infinity])expect(()=>W.keys("A",hold)).toThrow()
})
test("counted wheel aliases and point/zone shorthand use the Windows grammar",()=>{
  for(const alias of ["wheelup","scrollup","mousewheelup","MOUSE_WHEEL_UP"]){expect(W.compile("{"+alias+" 3}",state(),{}).steps[0].actions[0].vertical).toBe(-3)}
  expect(()=>W.compile("{wheelup 1.5}",state(),{})).toThrow("integer")
  const p=W.compile("{set point 3 1}{set zone X 1,2}{set zone C Z}",state(),{})
  expect(p.state.points[2]).toEqual([10,20]);expect(p.state.zones.X).toEqual([10,20,30,40]);expect(p.state.zones.C).toEqual([1,2,3,4])
  expect(()=>W.compile("{set point 3 16}",state(),{})).toThrow()
})
test("circle arithmetic and scaled vectors follow positive-up Windows geometry",()=>{
  for(const [s,n] of [["pi/2",Math.PI/2],["2pi",2*Math.PI],["2*(30+15)",90],["-(2+3)/5",-1],["1e-2+1",1.01]])expect(W.expression(s)).toBeCloseTo(n,10)
  for(const s of ["", "1/0","Math.random()","1;2","2**3","piX","(".repeat(40)+"1"+")".repeat(40)])expect(()=>W.expression(s)).toThrow()
  const v=W.vector("100 @ -2i+2j",[50,60]);expect(v.radius).toBe(100);expect(v.angle).toBeCloseTo(Math.PI*.75)
  expect(v.target).toEqual([-21,-11]);expect(W.circle([50,60],v.radius,v.angle,"radians")).toEqual(v.target)
  expect(W.vector("20,30",[10,10]).target).toEqual([30,40])
  expect(W.vector("-20i+20j",[10,10]).target).toEqual([-10,-10])
  for(const s of ["0i+0j","10 @ 0,0","1@2@3i","bad","1e300,1e300"])expect(()=>W.vector(s,[0,0])).toThrow()
})
test("Clear retains shared buffers/zones/views while resetting workflow-local fields",()=>{
  const before=state(),after=W.clearState(before)
  for(const key of ["buffers","zones","view","last_task"])expect(after[key]).toEqual(before[key])
  expect(after.points.every(p=>p===null)).toBe(true);expect(after.script).toBe("");expect(after.registry).toBe("")
  expect(after.paths).toEqual({import:"",export:""});expect(after.vars.A).toBe("");expect(after.shells.X).toBe("")
  expect(before.script).toBe("old")
})
test("ordered paste preserves duplicates, skips empty slots, and uses baseline delays",()=>{
  const actions=W.pasteSequence(["X","C","Z","X"],state())
  expect(actions.filter(a=>a.type==="paste").map(a=>a.content)).toEqual(["two","one","two"])
  expect(actions.filter(a=>a.type==="wait").map(a=>a.duration)).toEqual([.1,.08,.08,.08,.1])
  expect(actions[1].keysmap[0][1]).toEqual(["LEFT_MOUSE"])
  for(const slots of [[],["C"],["wrong"]])expect(()=>W.pasteSequence(slots,state())).toThrow()
})
test("manual three-key palette and mixed flow preserve data without mutating the source",()=>{
  const categories=K.categories();for(const name of ["Commons","Modifiers","Mouse buttons","NumPad","Navigations","Functions","Symbols"])expect(categories[name].length).toBeGreaterThan(0)
  expect(categories.NumPad).toContain("NUMPAD_ENTER")
  expect(K.chord(["CTRL","C",""])).toEqual(["CTRL","C"])
  for(const keys of [[],["FN"],["A","B","C","D"]])expect(()=>K.chord(keys)).toThrow()
  const source=[{type:"paste",content:"🐢"}],copy=K.append(source,{type:"keyssequence",keysmap:{0:[40,["ENTER"]]}},200)
  expect(copy.map(a=>a.type)).toEqual(["paste","wait","keyssequence"]);expect(copy[1].duration).toBe(.2);expect(source).toHaveLength(1)
})
