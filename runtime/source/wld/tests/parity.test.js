import {test,expect} from "bun:test"
import {readFileSync} from "node:fs"
const root=new URL("../../../../",import.meta.url)
const read=path=>readFileSync(new URL(path,root),"utf8")

test("parity inventory covers every Qt and Rust HTTP route",()=>{
  const doc=read("docs/feature-parity.md")
  const cpp=[...read("http-daemon/src/HttpServer.cpp").matchAll(/m_server->(Get|Post)\("([^"]+)"/g)].map(m=>[m[1].toUpperCase(),m[2]])
  const rust=[...read("http-daemon/wld/src/http_server.rs").matchAll(/\("(GET|POST)", "([^"]+)"\)/g)].map(m=>[m[1],m[2]])
  expect(cpp.length).toBe(14)
  for(const [method,path] of cpp){expect(rust.some(r=>r[0]===method&&r[1]===path)).toBe(true);expect(doc).toContain(method+" `"+path)}
})

test("parity inventory names every Windows dialog and native overlay page",()=>{
  const doc=read("docs/feature-parity.md")
  for(const name of ["Save Cursor Position","Record Keys","Manual Keys","Circle Placer","Manual Click","Display","Paste Buffers","Registry Hub"])expect(doc).toContain(name)
  const panel=read("runtime/source/wld/qml/Panel.qml")
  const pages=panel.match(/model:\[("Workflow"[^\]]+)\]/)[1].match(/"[^"]+"/g).map(s=>JSON.parse(s))
  for(const name of pages)expect(doc).toContain(name)
  expect(doc).toContain("Remaining parity checklist")
})

test("implementation coverage is calculated from the complete row inventory",()=>{
  const doc=read("docs/feature-parity.md")
  const rows=doc.split("\n").filter(line=>/^\| [HO]\d{2} ·/.test(line)).map(line=>{
    const cells=line.split(" | ");return {id:cells[0].slice(2,5),status:cells[2].replace(/\s*\|\s*$/,"")}
  })
  expect(new Set(rows.map(r=>r.id)).size).toBe(132)
  for(const [prefix,total,covered,eligible] of [["H",50,48,48],["O",82,76,77]]){
    const section=rows.filter(r=>r.id.startsWith(prefix));expect(section).toHaveLength(total)
    for(let i=1;i<=total;i++)expect(section.some(r=>r.id===prefix+String(i).padStart(2,"0"))).toBe(true)
    const included=section.filter(r=>! /^(Additional|Retired|Not implemented)/.test(r.status))
    const implemented=included.filter(r=>/^(Present|Different)/.test(r.status))
    expect(included).toHaveLength(eligible);expect(implemented).toHaveLength(covered)
    expect(100*implemented.length/included.length).toBeGreaterThanOrEqual(98)
  }
  expect(rows.filter(r=>r.status.startsWith("Partial")).map(r=>r.id)).toEqual(["O73"])
})

test("new native controls reach implemented handlers rather than placeholder buttons",()=>{
  const panel=read("runtime/source/wld/qml/Panel.qml"),service=read("runtime/source/wld/qml/Service.qml"),helper=read("runtime/source/wld/src/overlay.rs")
  for(const name of ["clearWorkflow","browsePath","exportState","exportActions","vector","exitOverlay"]){expect(panel).toContain("."+name+"(");expect(service).toContain("function "+name+"(")}
  for(const file of ["KeyTools.qml","Help.qml","ToolsMenu.qml"])expect(panel).toContain(file.replace(".qml","")+" {")
  expect(helper).toContain('"export-actions" => crate::artifacts::actions(r)')
  expect(service).toContain('root.change("view","panel",!root.environment.view.panel)')
  expect(service).toContain('["omarchy","plugin","disable","io.github.commanderturtle.macrohelp"]')
})
