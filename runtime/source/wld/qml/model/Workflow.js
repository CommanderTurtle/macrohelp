function copy(v) { return JSON.parse(JSON.stringify(v)) }
function containsPoint(point,origin,width,height) { return !!point&&!!origin&&point.x>=origin.x&&point.y>=origin.y&&point.x<origin.x+width&&point.y<origin.y+height }
function numeric(v,min,max,label) { var n=Number(v);if(!isFinite(n)||n<min||n>max)throw Error(label+" must be "+min+"…"+max);return n }
function wait(ms) { return {type:"wait",duration:numeric(ms,0,86400000,"Wait")/1000} }
function paste(text) { return {type:"paste",content:String(text),contentId:"",loop:1} }
function keys(text,hold) {
  var aliases={CTRL:"CONTROL_LEFT",CONTROL:"CONTROL_LEFT",SHIFT:"SHIFT_LEFT",ALT:"ALT_LEFT",ESC:"ESCAPE",RETURN:"ENTER",WIN:"WINDOWS",LWIN:"WINDOWS",SUPER:"WINDOWS",PRINT:"PRINT_SCREEN",PRINTSCREEN:"PRINT_SCREEN",SPACEBAR:"SPACE",LEFT:"LEFT_ARROW",RIGHT:"RIGHT_ARROW",UP:"UP_ARROW",DOWN:"DOWN_ARROW",LEFTCLICK:"LEFT_MOUSE",RIGHTCLICK:"RIGHT_MOUSE",MIDDLECLICK:"MIDDLE_MOUSE"}
  var list=String(text).toUpperCase().split(/[+\s]+/).filter(Boolean).map(function(k){return aliases[k]||k})
  if(!list.length)throw Error("Enter a key or chord")
  return {type:"keyssequence",keysmap:{"0":[Number(hold)||40,list]},keysSeqId:"",loop:1}
}
function move(point,ms) { if(!point||point.length!==2)throw Error("Save a point first");return {type:"cursormovements",cursormovsmap:[[0,Number(ms)||0,Math.round(point[0]),Math.round(point[1])]],cursorMovsId:"",optionalkeysstroke:[],loop:1} }
function drag(rect) {
  if(!rect||rect.length!==4)throw Error("Save a zone first")
  return {type:"cursormovements",cursormovsmap:[[0,100,rect[0],rect[1]],[0,400,rect[2],rect[3]]],cursorMovsId:"",optionalkeysstroke:["LEFT_MOUSE"],loop:1}
}
// Read active compositor bindings, not the commands behind them or Lua source.
function desktopShortcuts(bindings,options) {
  options=options||{}
  function find(name,pattern,excludeSingleLine){
    if(options[name])return keys(options[name],100)
    var matches=(bindings||[]).filter(function(b){var label=String(b.description||"")+" "+String(b.arg||"");return !b.submap&&!b.mouse&&b.key&&!/^code:/i.test(b.key)&&Number(b.modmask)>=0&&(Number(b.modmask)&~77)===0&&pattern.test(label)&&(!excludeSingleLine||!/single[- ]line/i.test(label))})
    if(!matches.length)return null
    var b=matches[0],mods=[],mask=Number(b.modmask)
    if(mask&64)mods.push("SUPER");if(mask&4)mods.push("CTRL");if(mask&8)mods.push("ALT");if(mask&1)mods.push("SHIFT")
    // Native keysyms keep their case; only Tasket's established aliases change.
    var action=keys(mods.join("+")+(mods.length?"+":"")+b.key,100)
    if(String(b.key).length>1&&!/^(PRINT|PRINTSCREEN|PRINT_SCREEN|RETURN|ENTER|TAB|ESCAPE|ESC|SPACE|SPACEBAR|F\d{1,2}|BACKSPACE|DELETE|INSERT|HOME|END|LEFT|RIGHT|UP|DOWN)$/i.test(b.key))action.keysmap["0"][1][mods.length]=String(b.key)
    return action
  }
  var ocr=find("ocr",/\bOCR\b|extract text|omarchy-(?:capture-text|ppocr-capture)\b/i,true)
  return {Z:find("screenshot",/^Screenshot\b|omarchy-capture-screenshot\b/i),X:ocr,C:find("singleLine",/single[- ]line.*(?:OCR|text)|(?:OCR|text).*single[- ]line/i)||ocr,copy:find("copy",/^Universal copy\b/i),terminal:find("terminal",/^Terminal\b|omarchy-launch-terminal\b/i),waitMs:numeric(options.captureWaitMs===undefined?2000:options.captureWaitMs,100,60000,"Capture wait")}
}
function zone(rect,method,shortcuts) {
  if(!rect||rect.length!==4||rect.some(function(n){return !isFinite(n)}))throw Error("Save a zone first")
  if(rect[0]===rect[2]||rect[1]===rect[3])throw Error("Zone must have positive area")
  if(!/^[ZXCV]$/.test(method))throw Error("Zone method must be Z, X, C or V")
  var actions=[],pause=shortcuts&&shortcuts.waitMs!==undefined?shortcuts.waitMs:2000
  if(method!=="V"){
    if(!shortcuts||!shortcuts[method])throw Error("Choose a desktop shortcut in the Macrohelp plugin's shortcuts setting")
    actions.push(copy(shortcuts[method]),wait(pause))
  }
  actions.push(move(rect.slice(0,2),300),wait(200),drag(rect),wait(pause))
  if(method==="V"){
    if(!shortcuts||!shortcuts.copy)throw Error("Choose a desktop copy shortcut in the Macrohelp plugin's shortcuts setting")
    actions.push(copy(shortcuts.copy),wait(1000))
  }
  return actions
}
function circle(origin,radius,angle,unit) {
  radius=numeric(radius,0,100000,"Radius");angle=numeric(angle,-360000,360000,"Angle")
  var rad=unit==="radians"?angle:angle*Math.PI/180
  return [Math.round(origin[0]+radius*Math.cos(rad)),Math.round(origin[1]+radius*Math.sin(rad))]
}
function reference(s,state) {
  var k=String(s).replace(/^\$/,""),n=Number(k)
  if(Object.prototype.hasOwnProperty.call(state.vars,k))return state.vars[k]
  if(Object.prototype.hasOwnProperty.call(state.buffers,k))return state.buffers[k]
  if(n>=1&&n<=16&&state.points[n-1])return state.points[n-1].join(",")
  if(k==="REGISTRY")return state.registry
  if(k==="REGEDITED")return state.shells.G
  if(/^SHELL[XCG]$/.test(k))return state.shells[k.slice(-1)]
  throw Error("Unknown reference $"+k)
}
function expand(text,state) {return text.replace(/\$(REGEDITED|REGISTRY|SHELL[XCG]|1[0-6]|[1-9ASDFZXCV])\b/g,function(_,k){return reference(k,state)})}
function formatParts(text,state,live) {
  var out=[],at=0,rx=/\$(REGEDITED|REGISTRY|SHELL[XCG]|1[0-6]|[1-9ASDFZXCV])\b/g,m
  while((m=rx.exec(text))){out.push(text.slice(at,m.index));out.push(live[m[1]]?{reference:m[1]}:reference(m[1],state));at=rx.lastIndex}
  out.push(text.slice(at));return out
}
function resolve(actions,state){return actions.map(function(a){a=copy(a);if(a.macrohelp_parts){a.content=a.macrohelp_parts.map(function(p){return typeof p==="string"?p:reference(p.reference,state)}).join("");delete a.macrohelp_parts}return a})}
function compare(left,op,right) {
  switch(op.toLowerCase()) {
    case "empty":return String(left).length===0
    case "notempty":return String(left).length!==0
    case "contains":return String(left).indexOf(right)>=0
    case "notcontains":return String(left).indexOf(right)<0
    case "is":case "==":return String(left).toLowerCase()===String(right).toLowerCase()
    case "!=":return String(left).toLowerCase()!==String(right).toLowerCase()
    case ">":return Number(left)>Number(right)
    case ">=":return Number(left)>=Number(right)
    case "<":return Number(left)<Number(right)
    case "<=":return Number(left)<=Number(right)
  }
  throw Error("Unknown comparison "+op)
}
function condition(text,state,context) {
  var m=text.match(/^saved-task\s+(.+?)\s+(exists|missing)$/i)
  var tasks=context.tasks||[],status=context.status||{},jobs=(status.scheduled||[]).concat(status.running||[],status.finished||[])
  if(m){var exists=tasks.some(function(t){return t.name.toLowerCase()===m[1].replace(/\.(?:om)?scht$/i,"").toLowerCase()});return m[2].toLowerCase()==="exists"?exists:!exists}
  m=text.match(/^saved-tasks\s+count\s*(==|!=|>=|<=|>|<)\s*(\d+)$/i)
  if(m)return compare(tasks.length,m[1],m[2])
  m=text.match(/^task\s+(last|\d+)\s+(?:(state|remaining|message)\s+)?(is|==|!=|>=|<=|>|<|contains|notcontains)\s+(.+)$/i)
  if(m){var id=m[1].toLowerCase()==="last"?(state.last_task||{}).task_number:Number(m[1]),job=jobs.find(function(j){return j.task_number===id})||{};return compare(job[m[2]==="remaining"?"remaining_seconds":m[2]||"state"]||"",m[3],expand(m[4],state))}
  m=text.match(/^\$?([ASDFZXCV])\s*(empty|notempty|notcontains|contains|==|!=|>=|<=|>|<)\s*(.*)$/i)
  if(m)return compare(reference(m[1].toUpperCase(),state),m[2],expand(m[3],state))
  throw Error("Invalid condition: "+text)
}
function presetName(name,profiles) {
  var names=Object.keys(profiles||{}),exact=names.filter(function(k){return k.toLowerCase()===name.toLowerCase()})
  if(exact.length===1)return exact[0]
  var matches=names.filter(function(k){var lower=k.toLowerCase(),prefix=name.toLowerCase();return [" ","-","_"].some(function(separator){return lower.indexOf(prefix+separator)===0})})
  if(matches.length!==1)throw Error(matches.length?"Ambiguous preset: "+name:"Preset not found: "+name)
  return matches[0]
}
function mergePreset(original,data) {
  var state=copy(original)
  Object.keys(data).forEach(function(section){
    if(["script","registry","shells","vars","buffers","zones","points","view"].indexOf(section)<0)throw Error("Unknown preset section")
    if(section==="script")return
    if(["shells","vars","buffers","zones","view"].indexOf(section)>=0){Object.keys(data[section]).forEach(function(key){if(!Object.prototype.hasOwnProperty.call(state[section],key))throw Error("Unknown preset setting");state[section][key]=copy(data[section][key])})}
    else state[section]=copy(data[section])
  })
  return state
}
function compile(source,original,context) {
  var state=copy(original),steps=[],stack=[],enabled=true,focusClick=false,live={},actions=0
  function emit(action) { var last=steps[steps.length-1];if(!last||last.kind!=="input"){last={kind:"input",actions:[]};steps.push(last)}last.actions.push(action);if(++actions>10000)throw Error("Too many actions") }
  function local(step){steps.push(step)}
  function buffers(text){return text.split(/[,\s]+/).filter(Boolean).map(function(k){return reference(k.toUpperCase(),state)}).join("")}
  function scan(source,ancestry){
  if(source.length>1000000)throw Error("Expanded workflow exceeds 1 MiB")
  var token=/\{([^{}]*)\}/g,match,at=0,initialDepth=stack.length,expanded=""
  function append(text){if(expanded.length+text.length>1000000)throw Error("Expanded workflow exceeds 1 MiB");expanded+=text}
  while((match=token.exec(source))){
    if(source.slice(at,match.index).trim())throw Error("Use {type text} for literal text")
    at=token.lastIndex;var raw=match[1].trim(),parts=raw.split(/\s+/),op=parts.shift().toLowerCase(),arg=parts.join(" ")
    if(op==="if"){if(stack.length>=16)throw Error("Conditions nested too deeply");var value=enabled?condition(arg,state,context):false;stack.push({parent:enabled,value:value,otherwise:false});enabled=enabled&&value;append(match[0]);continue}
    if(op==="else"){if(stack.length<=initialDepth||stack[stack.length-1].otherwise)throw Error("Unexpected else");var frame=stack[stack.length-1];frame.otherwise=true;enabled=frame.parent&&!frame.value;append(match[0]);continue}
    if(op==="endif"){if(stack.length<=initialDepth)throw Error("Unexpected endif");enabled=stack.pop().parent;append(match[0]);continue}
    if(!enabled){append(match[0]);continue}
    var rawArg=arg;arg=expand(arg,state)
    if(op==="profile"){
      if(context.profileError)throw Error(context.profileError)
      var name=presetName(arg,context.profiles),data=context.profiles[name]
      if(ancestry.indexOf(name)>=0||ancestry.length>=16)throw Error("Recursive preset: "+name)
      state=mergePreset(state,data)
      ;["vars","buffers"].forEach(function(section){Object.keys(data[section]||{}).forEach(function(k){delete live[k]})})
      append(scan(String(data.script||""),ancestry.concat([name])))
      continue
    }
    append("{"+op+(arg?" "+arg:"")+"}")
    if(op==="set"){
      var setting=arg.match(/^(?:(var|paste|zone|point|shell)\s+)?([^=\s]+)\s*(?:=\s*|\s+)(.*)$/i)
      if(!setting)throw Error("Use {set A value}, {set paste Z value}, {set point 1 x,y}")
      var kind=(setting[1]||"").toLowerCase(),k=setting[2].toUpperCase(),v=setting[3]
      if((!kind||kind==="var")&&/^[ASDF]$/.test(k)){state.vars[k]=v;delete live[k]}
      else if((!kind||kind==="paste")&&/^[ZXCV]$/.test(k)){state.buffers[k]=v;delete live[k]}
      else if(kind==="shell"&&/^[XCG]$/.test(k))state.shells[k]=v
      else if(k==="REGISTRY")state.registry=v
      else if(kind==="point"||kind==="zone"){
        var a=v.split(/[,\s]+/).filter(Boolean).map(function(n){return Math.round(numeric(n,-100000,100000,"Coordinate"))})
        if(kind==="point"&&/^([1-9]|1[0-6])$/.test(k)&&a.length===2)state.points[Number(k)-1]=a
        else if(kind==="zone"&&/^[ZXCV]$/.test(k)&&a.length===4)state.zones[k]=a
        else throw Error("Invalid point or zone")
      } else throw Error("Unknown set target")
    }else if(op==="wait")emit(wait(arg))
    else if(op==="point"||op==="pt")emit(move(state.points[numeric(arg,1,16,"Point")-1],100))
    else if(op==="type"){var typed=paste(arg),textParts=formatParts(rawArg,state,live);if(textParts.some(function(p){return typeof p!=="string"}))typed.macrohelp_parts=textParts;emit(typed)}
    else if(op==="paste"||op==="buf"||op==="enter"){
      if(op!=="paste"&&focusClick)emit(keys("LEFT_MOUSE"))
      var pasted=paste(buffers(arg)),refs=arg.split(/[,\s]+/).filter(Boolean).map(function(k){k=k.replace(/^\$/," ").trim().toUpperCase();return live[k]?{reference:k}:reference(k,state)})
      if(refs.some(function(p){return typeof p!=="string"}))pasted.macrohelp_parts=refs
      emit(pasted);if(op==="enter")emit(keys("ENTER"))
    }else if(op==="click"&&/^(on|off)$/i.test(arg))focusClick=arg.toLowerCase()==="on"
    else if(["click","left","right","middle"].indexOf(op)>=0)emit(keys(op==="right"?"RIGHT_MOUSE":op==="middle"?"MIDDLE_MOUSE":"LEFT_MOUSE",Number(arg)||40))
    else if(op==="play")emit({type:"runningothertask",otherTaskName:arg.replace(/\.(?:om)?scht$/i,""),otherTaskDelay:0,otherTaskLoops:1})
    else if(["wheelup","wheeldown","scrollup","scrolldown"].indexOf(op)>=0){var count=Math.round(numeric(arg||1,1,1000,"Wheel count"));emit({type:"scroll",vertical:(/down$/.test(op)?1:-1)*count,horizontal:0})}
    else if(["ctrlmod","controlmod","shiftmod"].indexOf(op)>=0){var chord=arg.match(/^(.+?)(?:\s+(\d+))?$/);var action=keys((op==="shiftmod"?"SHIFT+":"CONTROL+")+chord[1]);action.loop=Math.round(numeric(chord[2]||1,1,1000,"Chord count"));emit(action)}
    else if(op==="zone"||op==="zonebuf"){
      var slots=arg.toUpperCase().split(/[,\s]+/).filter(Boolean),slot=slots[0],rect=state.zones[slot],method=slots[1]||slot,destination=""
      if(!/^[ZXCV]$/.test(slot)||!rect)throw Error("Save zone "+slot+" first")
      if(op==="zonebuf"){
        if(slots.length===2){method=slot;destination=slots[1]}else if(slots.length===3)destination=slots[2];else throw Error("Use {zonebuf zone,method,buffer}")
        if(!/^[ASDFZXCV]$/.test(destination))throw Error("Invalid destination buffer/variable")
        if(method==="Z")throw Error("Images cannot be saved in a text buffer")
        live[destination]=true
      }else if(slots.length>2)throw Error("Use {zone zone,method}")
      zone(rect,method,context.shortcuts).forEach(emit)
      steps[steps.length-1].capture=true
      if(destination)local({kind:"clipboard",slot:destination})
    }else if(op==="rgdclip"||op==="rgdappend")local({kind:"regedited",action:op==="rgdclip"?"clip":"append",file:state.registry,reference:arg})
    else if(op==="shell"||["bash","cmd","python","py","regedited","regbin"].indexOf(op)>=0){
      var alias=op==="shell"?arg.toUpperCase():op==="bash"?"X":op==="cmd"?"C":"G"
      if(!/^[XCG]$/.test(alias))throw Error("Shell alias must be X, C or G")
      if(!state.shells[alias])throw Error("Configure shell alias "+alias+" first")
      if(!context.shortcuts||!context.shortcuts.terminal)throw Error("Choose a terminal shortcut in the Macrohelp plugin's shortcuts setting")
      emit(copy(context.shortcuts.terminal));emit(wait(700))
      emit(paste(state.shells[alias]));emit(keys("ENTER"))
    }else if(["exec","system","sys","powershell"].indexOf(op)>=0)throw Error("No implicit system/Windows commands; use an explicitly permitted Omarsket task")
    else emit(keys(raw))
  }
  if(source.slice(at).trim())throw Error("Use {type text} for literal text")
  if(stack.length!==initialDepth)throw Error("Missing endif")
  return expanded
  }
  var expanded=scan(String(source),[])
  return {state:state,steps:steps,expanded:expanded}
}
