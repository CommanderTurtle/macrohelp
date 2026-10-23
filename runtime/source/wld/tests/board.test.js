import {test,expect} from 'bun:test'
import {readFileSync} from 'node:fs'
import {runInNewContext} from 'node:vm'
const read=p=>readFileSync(new URL(p,import.meta.url),'utf8')
const W=runInNewContext(read('../qml/model/Workflow.js')+'\n({board,boardCode,boardCoordinate,pointValue,zoneValue,compile,viewPatch})')
const state=()=>({vars:{},buffers:{},zones:{},points:Array(16).fill(null),shells:{},view:{crosshair:true,grid:false,big_crosshair:false}})
test('board labels, point inputs and native assembly share physical cell centers',()=>{
  expect(W.boardCoordinate('_a')).toBe(25);expect(W.boardCoordinate('_h')).toBe(375);expect(W.boardCoordinate('_aj')).toBe(1775)
  for(const n of [-99975,-50,-1,0,24,25,49,50,1775,99975]){
    const label=W.boardCode(n),center=W.boardCoordinate(label)
    expect(W.boardCode(center)).toBe(label);expect(Math.abs(center-n)).toBeLessThanOrEqual(25)
  }
  expect(W.board({x:1776,y:376})).toEqual({x:'_aj',y:'_h',text:'_aj, _h',center:{x:1775,y:375},cell_size:50})
  expect(W.board({x:49.6,y:0}).x).toBe('_b');expect(()=>W.boardCode(Infinity)).toThrow()
  const p=W.compile('{set point a _a,_h}{point a}{point _aj,_h}{set zone Z _a,_a,_h,_aj}',state(),{})
  expect(p.state.points[0]).toEqual([25,375]);expect(p.state.zones.Z).toEqual([25,25,375,1775])
  expect(p.steps[0].actions[0].cursormovsmap[0].slice(2)).toEqual([25,375])
  expect(p.steps[0].actions[1].cursormovsmap[0].slice(2)).toEqual([1775,375])
  expect(W.pointValue('-_a,_h',state())).toEqual([-25,375])
  expect(W.zoneValue('_a,_h,_aj,2000',state())).toEqual([25,375,1775,2000])
  for(const code of ['_','_1','_zzzz','_a;exit'])expect(()=>W.boardCoordinate(code)).toThrow()
})
test('view patches enforce one-way exclusion for UI and HTTP without enabling the other on disable',()=>{
  let v=W.viewPatch(state().view,{grid:true});v=W.viewPatch(v,{big_crosshair:true})
  expect(v.grid).toBe(false);expect(v.big_crosshair).toBe(true)
  v=W.viewPatch(v,{big_crosshair:false});expect(v.grid).toBe(false)
  v=W.viewPatch(v,{big_crosshair:true});v=W.viewPatch(v,{grid:true});expect(v.big_crosshair).toBe(false)
  v=W.viewPatch(v,{grid:false});expect(v.big_crosshair).toBe(false)
  const hud=read('../qml/Hud.qml'),service=read('../qml/Service.qml')
  expect(hud).toContain('(v.grid||v.big_crosshair)&&!v.coordinates_only')
  expect(hud).toContain('anchors.top:coordinatesTag.bottom')
  expect(service).toContain('readonly property var board: Workflow.board(physicalCursor)')
  expect(service).toContain('if(event.path==="/v1/board")')
})
