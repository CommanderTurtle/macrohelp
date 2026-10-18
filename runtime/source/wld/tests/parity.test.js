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
