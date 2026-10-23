// Translate the public API into the same dialog state transitions and effects.
function skillsSnapshot(env) {
  var map=JSON.parse(JSON.stringify(env.skills||{})),names=Object.keys(map).sort()
  return {skills:map,text:"User saved skills:"+(names.length?"\n"+names.map(function(n){return n+" → "+map[n]}).join("\n"):" none")}
}
function updateSkill(env,request) {
  var next=JSON.parse(JSON.stringify(env)),map=next.skills||{}
  if(request.name.toLowerCase()==="remove")delete map[request.rgd]
  else Object.defineProperty(map,request.name,{value:request.rgd,writable:true,enumerable:true,configurable:true})
  if(Object.keys(map).length>256)throw Error("At most 256 saved skills")
  next.skills=map
  if(unescape(encodeURIComponent(JSON.stringify(next))).length>1024*1024)throw Error("Merged environment exceeds 1 MiB")
  return next
}
function skillQuery(env,name,summary) {
  if(!Object.prototype.hasOwnProperty.call(env.skills||{},name))throw Error("Unknown saved skill")
  var ref=env.skills[name]
  return {name:name,rgd:ref,summary:summary||"<none>",request:{menu:2,inputs:["Z","{rgd "+ref+"}","P"],config:{registry:env.registry},no_replace:true}}
}
function merge(original, patch, W, monitors) {
  var env=W.copy(original)
  Object.keys(patch||{}).forEach(function(section){
    if(section==="script"||section==="registry")env[section]=patch[section]
    else if(section==="view")env.view=W.viewPatch(env.view,patch.view)
    else if(section==="points")Object.keys(patch.points).forEach(function(key){env.points[Number(key)-1]=patch.points[key]===null?null:W.coordinateValues(patch.points[key],monitors)})
    else if(section==="zones")Object.keys(patch.zones).forEach(function(key){env.zones[key]=patch.zones[key]===null?null:W.coordinateValues(patch.zones[key],monitors)})
    else {if(!env[section])env[section]={};Object.keys(patch[section]).forEach(function(key){env[section][key]=W.copy(patch[section][key])})}
  })
  if(unescape(encodeURIComponent(JSON.stringify(env))).length>1024*1024)throw Error("Merged environment exceeds 1 MiB")
  return env
}
function prepare(request, original, cursor, current, W, D, monitors) {
  var env=merge(original,request.config,W,monitors),xy=request.origin?W.coordinateValues(request.origin,monitors):null,origin=xy?{x:xy[0],y:xy[1]}:cursor
  var state=request.menu!==undefined?D.open(["save_cursor","paste_buffers","registry_hub","circle_placer","click_left","click_right","click_middle","stop_all","view_toggles","record_keys"][request.menu],origin):current?W.copy(current):null
  var effects=[],closed=false
  // A continued open editor must not overwrite a supplied config patch with
  // the stale text it displayed before this request.
  if(state&&request.menu===undefined){
    var p=request.config||{},section="",key="",direct=""
    if(state.stage==="Script")direct="script"
    else if(state.stage==="RegistryPath")direct="registry"
    else if(state.stage==="ExportPath"||state.stage==="ImportPath"){section="paths";key=state.stage==="ExportPath"?"export":"import"}
    else if(state.stage==="Var"){section="vars";key=state.slot}
    else if(state.stage==="Shell"){section="shells";key=state.slot}
    else if(state.stage==="Edit"&&state.kind==="paste_buffers"){section="buffers";key=state.slot}
    else if(state.stage==="ZoneEdit"){section="zones";key=state.slot}
    else if(state.stage==="Point"){section="points";key=String(Number(state.slot)+1)}
    if(direct&&Object.prototype.hasOwnProperty.call(p,direct))state.text=env[direct]
    else if(section&&p[section]&&Object.prototype.hasOwnProperty.call(p[section],key)){
      var value=section==="points"?env.points[Number(key)-1]:env[section][key]
      state.text=Array.isArray(value)?value.join(","):value===null?"":String(value)
    }
  }
  function consume(result){
    state=result.state
    if(result.swap)state=D.open(result.swap,origin)
    closed=!!result.close
    result.effects.forEach(function(e){
      if(e.kind==="change"){if(e.section)env[e.section][e.key]=e.value;else env[e.key]=e.value}
      else if(e.kind==="clear")env=W.clearState(env)
      else if(e.kind==="zone-router"){state=D.open("paste_buffers",origin,e.rect);state.stage="ZoneCommand";closed=false}
      else {effects.push(e);if(e.kind==="export")closed=true}
    })
  }
  function submit(text){if(!state)throw Error("Choose a menu");consume(D.submit(state,text,env,origin,W,monitors))}
  function script(){if(!state||state.kind!=="registry_hub")throw Error("Registry Hub required");if(["Command","Script"].indexOf(state.stage)<0)throw Error("Finish the current Registry Hub input first");if(state.stage==="Script")env.script=state.text}
  ;(request.inputs||[]).forEach(function(input,index){
    try {
      if(closed)throw Error("The preceding input closed the menu")
      if(typeof input==="string"){submit(input);return}
      var action=input.action
      if(action==="submit")submit(input.value||"")
      else if(action==="accept")submit(state.text||"")
      else if(action==="cancel")closed=true
      else if(action==="play"||action==="clear"||action==="export"||action==="import"||action==="save_schedule"){
        script()
        if(action==="clear"){env=W.clearState(env);D.field(state,"Command")}
        else if(action==="play"){effects.push({kind:"play",origin:state.origin});closed=true}
        else if(action==="save_schedule"){if(!String(input.value||"").trim())throw Error("Schedule name required");effects.push({kind:"save_schedule",name:input.value});closed=true}
        else {if(!env.paths||!env.paths[action])throw Error("Set the "+action+" path first");effects.push({kind:action+"_state",path:env.paths[action]});closed=true}
      } else if(action==="paste"||action==="paste_enter") {
        if(!state||state.kind!=="paste_buffers"||state.stage!=="Edit")throw Error("Open a paste-buffer editor first")
        if(input.value!==undefined)state.text=input.value
        env.buffers[state.slot]=state.text
        var actions=[W.move(state.origin,0)].concat(W.pasteSequence([state.slot],env))
        if(action==="paste_enter")actions.push(W.keys("ENTER"))
        effects.push({kind:"queue",actions:actions});closed=true
      } else if(action==="add_point"||action==="save_click"){
        if(!state||state.kind!=="save_cursor")throw Error("Save Cursor required")
        if(action==="add_point")state.points.push(D.point(origin))
        else {if(state.stage==="Name")submit(input.value===undefined?state.text:input.value);submit("c")}
      } else if(action==="add_step") {
        if(!state||state.kind!=="record_keys")throw Error("Record Keys required")
        if(state.stage==="Name")submit(input.value===undefined?state.text:input.value)
        if(["Command","Manual"].indexOf(state.stage)<0)throw Error("Finish the current key-recording input first")
        if(!input.keys||!input.keys.length)throw Error("Choose keys for the step")
        D.addStep(state,input.keys);state.capturing=false;if(input.delay!==undefined)state.delay=input.delay;D.field(state,"Command")
      } else if(["capture","manual","add_paste"].indexOf(action)>=0){
        if(!state||state.kind!=="record_keys")throw Error("Record Keys required")
        if(state.stage==="Name")submit(input.value===undefined?state.text:input.value)
        submit(action==="capture"?"z":action==="manual"?"x":"c")
      } else if(action==="replay") {
        if(!state||state.kind!=="record_keys")throw Error("Record Keys required")
        if(state.stage!=="Command")throw Error("Finish the current key-recording input first")
        var result=D.submit(state,"",env,origin,W),exported=result.effects[0]
        effects.push({kind:"queue",actions:[W.move(state.origin,0)].concat(exported.actions)});closed=true
      } else throw Error("Unknown input action")
    }catch(error){throw Error("Input "+(index+1)+": "+error.message)}
  })
  if(unescape(encodeURIComponent(JSON.stringify(env))).length>1024*1024)throw Error("Merged environment exceeds 1 MiB")
  return {environment:env,state:state,effects:effects,closed:closed}
}
