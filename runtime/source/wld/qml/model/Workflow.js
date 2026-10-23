function copy(v) { return JSON.parse(JSON.stringify(v)) }
// Action/environment coordinates are physical pixels; Qt placement is logical.
function monitorScale(monitors) { var scale=Number(((monitors||[])[0]||{}).scale);return isFinite(scale)&&scale>0?scale:1 }
function displayPoint(point,origin,scale) { return [point[0]/scale-origin.x,point[1]/scale-origin.y] }
function containsPoint(point,origin,width,height) { return !!point&&!!origin&&point.x>=origin.x&&point.y>=origin.y&&point.x<origin.x+width&&point.y<origin.y+height }
function numeric(v,min,max,label) { var n=Number(v);if(!isFinite(n)||n<min||n>max)throw Error(label+" must be "+min+"…"+max);return n }
function wait(ms) { return {type:"wait",duration:numeric(ms,0,86400000,"Wait")/1000} }
function paste(text) { return {type:"paste",content:String(text),contentId:"",loop:1} }
function keyName(raw) {
  var text=String(raw).trim(),k=text.toLowerCase().replace(/[_\s-]/g,"")
  var groups={CONTROL_LEFT:"ctrl control leftctrl ctrlleft leftcontrol controlleft lctrl lcontrol",CONTROL_RIGHT:"rightctrl ctrlright rightcontrol controlright rctrl rcontrol",SHIFT_LEFT:"shift leftshift shiftleft lshift",SHIFT_RIGHT:"rightshift shiftright rshift",ALT_LEFT:"alt leftalt altleft lalt",ALT_GR:"rightalt altright altgr ralt",WINDOWS:"win windows leftwin rightwin lwin super meta",LEFT_ARROW:"left leftarrow arrowleft",RIGHT_ARROW:"right rightarrow arrowright",UP_ARROW:"up uparrow arrowup",DOWN_ARROW:"down downarrow arrowdown",SPACE:"space spacebar",ENTER:"return enter",ESCAPE:"escape esc",BACKSPACE:"backspace bksp",DELETE:"delete del",INSERT:"insert ins",PAGE_UP:"pageup pgup",PAGE_DOWN:"pagedown pgdn",PRINT_SCREEN:"print printscreen",LEFT_MOUSE:"leftmouse mouseleft lmouse leftclick",RIGHT_MOUSE:"rightmouse mouseright rmouse rightclick",MIDDLE_MOUSE:"middlemouse mousemiddle mmouse middleclick",MOUSE_WHEEL_UP:"mousewheelup wheelup scrollup",MOUSE_WHEEL_DOWN:"mousewheeldown wheeldown scrolldown"}
  for(var name in groups)if(groups[name].split(" ").indexOf(k)>=0)return name
  if(["tab","home","end"].indexOf(k)>=0)return k.toUpperCase()
  if(/^[a-z0-9]$/i.test(text)||/^f\d{1,2}$/i.test(text))return text.toUpperCase()
  return text
}
function keys(text,hold) {
  var list=(Array.isArray(text)?text:String(text)==="+"?["plus"]:String(text).split(/[+\s]+/)).filter(Boolean).map(keyName)
  if(!list.length)throw Error("Enter a key or chord")
  return {type:"keyssequence",keysmap:{"0":[numeric(hold===undefined?40:hold,1,600000,"Hold"),list]},keysSeqId:"",loop:1}
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
function expression(text) {
  var source=String(text).toLowerCase().replace(/\s/g,""),at=0,depth=0
  if(source.length>512)throw Error("Expression too long")
  function atom(){if(++depth>32)throw Error("Expression too deep");var v,c=source[at];if(c==="+"||c==="-"){at++;v=(c==="-"?-1:1)*atom()}else if(c==="("){at++;v=sum();if(source[at++]!==")")throw Error("Missing closing parenthesis")}else if(source.slice(at,at+2)==="pi"){at+=2;v=Math.PI}else{var m=source.slice(at).match(/^(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?/);if(!m)throw Error("Number expected");at+=m[0].length;v=Number(m[0])}depth--;return v}
  function product(){var v=atom();while(at<source.length){var c=source[at];if(c==="*"||c==="/"){at++;var n=atom();v=c==="*"?v*n:v/n}else if(c==="("||c==="p")v*=atom();else break}return v}
  function sum(){var v=product();while(source[at]==="+"||source[at]==="-"){var c=source[at++],n=product();v=c==="+"?v+n:v-n}return v}
  var v=sum();if(at!==source.length||!isFinite(v))throw Error("Invalid numeric expression");return v
}
function vector(text,origin) {
  var parts=String(text).toLowerCase().replace(/\s/g,"").split("@"),v=parts.pop(),dx=0,dy=0
  if(parts.length>1)throw Error("Use radius @ i+j")
  if(v.indexOf(",")>=0){var xy=v.split(",");if(xy.length!==2)throw Error("Use x,y or i+j");dx=expression(xy[0]);dy=-expression(xy[1])}
  else {var rx=/([+-]?)(\d+(?:\.\d*)?|\.\d+)?([ij])/g,m,at=0;while((m=rx.exec(v))){if(m.index!==at)throw Error("Invalid vector");var n=(m[1]==="-"?-1:1)*Number(m[2]||1);if(m[3]==="i")dx+=n;else dy+=n;at=rx.lastIndex}if(!at||at!==v.length)throw Error("Use i+j vector terms")}
  var length=Math.hypot(dx,dy)
  if(!length||!isFinite(length))throw Error("Direction must be finite and nonzero")
  var radius=numeric(parts.length?expression(parts[0]):length,0.000001,100000,"Radius")
  dx=dx/length*radius;dy=dy/length*radius
  return {radius:radius,angle:Math.atan2(dy,dx),target:[Math.round(origin[0]+dx),Math.round(origin[1]-dy)]}
}
function pointValue(text,state){var raw=String(text).trim().replace(/^\$/,""),p=/^(?:point|pt)?\s*(\d+)$/i.exec(raw);if(p){var saved=state.points[numeric(p[1],1,16,"Point")-1];if(!saved)throw Error("Save point first");return copy(saved)}var a=raw.split(/[,\s]+/).filter(Boolean);if(a.length!==2)throw Error("Use x,y or a point number");return a.map(function(n){return Math.round(numeric(n,-100000,100000,"Coordinate"))})}
function zoneValue(text,state){var raw=String(text).trim(),slot=raw.toUpperCase().replace(/^ZONE[\s_-]*/,"");if(/^[ZXCV]$/.test(slot)){if(!state.zones[slot])throw Error("Save zone first");return copy(state.zones[slot])}var a=raw.split(/[,\s]+/).filter(Boolean);if(a.length===2)return pointValue(a[0],state).concat(pointValue(a[1],state));if(a.length!==4)throw Error("Use rectangle, two point numbers or zone slot");return a.map(function(n){return Math.round(numeric(n,-100000,100000,"Coordinate"))})}
function clearState(original){var state=copy(original);state.script="";state.registry="";state.shells={X:"",C:"sh",G:"",P:"pwsh",T:""};state.vars={A:"",S:"",D:"",F:""};state.points=Array(16).fill(null);state.paths={import:"",export:""};return state}
function pasteSequence(slots,state){if(!slots.length||slots.length>1000)throw Error("Choose 1–1000 buffers");slots.forEach(function(k){if(!/^[ZXCV]$/.test(k))throw Error("Unknown buffer")});slots.forEach(function(k){if(!state.buffers[k])throw Error("Buffer "+k+" is empty")});var chosen=slots;var actions=[wait(100),keys("LEFT_MOUSE"),wait(80)];chosen.forEach(function(k,i){if(i)actions.push(wait(80));actions.push(paste(state.buffers[k]))});actions.push(wait(100));return actions}
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
  if(state.shells.P===undefined)state.shells.P="pwsh"
  if(state.shells.T===undefined)state.shells.T=""
  Object.keys(data).forEach(function(section){
    if(["script","registry","shells","vars","buffers","zones","points","view"].indexOf(section)<0)throw Error("Unknown preset section")
    if(section==="script")return
    if(["shells","vars","buffers","zones","view"].indexOf(section)>=0){Object.keys(data[section]).forEach(function(key){if(!Object.prototype.hasOwnProperty.call(state[section],key))throw Error("Unknown preset setting");state[section][key]=copy(data[section][key])})}
    else state[section]=copy(data[section])
  })
  return state
}
// Token boundaries depend only on brace depth, never on the payload's language.
function registryTokens(source) {
  source=String(source)
  var out=[],depth=0,start=0
  for(var i=0;i<source.length;i++){
    var c=source[i]
    if(c==="{"){if(!depth)start=i;depth++}
    else if(c==="}"){
      if(!depth)throw Error("Unexpected closing brace")
      if(!--depth){var match=[source.slice(start,i+1),source.slice(start+1,i)];match.index=start;match.end=i+1;out.push(match)}
    }else if(!depth&&!/\s/.test(c))throw Error("Use {type text} for literal text")
  }
  if(depth)throw Error("Missing closing brace")
  return out
}
// Decode a complete key argument once, after tokenization. Never used for text.
function pressKey(raw) {
  var names={amp:"ampersand",AMP:"ampersand",lt:"less",LT:"less",gt:"greater",GT:"greater",quot:"quotedbl",QUOT:"quotedbl",apos:"apostrophe",lbrace:"braceleft",lcub:"braceleft",rbrace:"braceright",rcub:"braceright",lbrack:"bracketleft",lsqb:"bracketleft",rbrack:"bracketright",rsqb:"bracketright",sol:"slash",bsol:"backslash",vert:"bar",semi:"semicolon",colon:"colon",excl:"exclam",num:"numbersign",dollar:"dollar",percnt:"percent",lpar:"parenleft",rpar:"parenright",ast:"asterisk",plus:"plus",comma:"comma",period:"period",quest:"question",commat:"at",grave:"grave",Tab:"TAB",NewLine:"ENTER"}
  var entity=/^&([A-Za-z][A-Za-z0-9]*);$/.exec(raw)
  if(!entity){if(raw[0]==="&"&&raw.slice(-1)===";")throw Error("Unsupported press entity: "+raw);return keyName(raw)}
  if(!Object.prototype.hasOwnProperty.call(names,entity[1]))throw Error("Unsupported press entity: "+raw)
  return names[entity[1]]
}
function press(text) {
  function modifier(key){return /^(CONTROL_LEFT|CONTROL_RIGHT|SHIFT_LEFT|SHIFT_RIGHT|ALT_LEFT|ALT_GR|WINDOWS)$/.test(key)}
  var args=String(text)==="+"?["+"]:String(text).split(/[+,\s]+/).filter(Boolean),count=1
  if(args.length>1&&args.slice(0,-1).some(function(key){return !modifier(keyName(key))})&&/^\d+$/.test(args[args.length-1]))count=numeric(args.pop(),1,1000,"Repeat count")
  if(!args.length)throw Error("Enter a key or chord")
  var list=args.map(pressKey),mods=[],targets=[]
  list.forEach(function(key){var into=modifier(key)?mods:targets;if(into.indexOf(key)<0)into.push(key)})
  if(!targets.length)return keys(mods)
  var map={}
  if(mods.length)map["0"]=[(count-1)*120+42,mods]
  for(var i=0;i<count;i++)map[String(i*120+1)]=[40,targets]
  return {type:"keyssequence",keysmap:map,keysSeqId:"",loop:1}
}
function waitCount(source,context,ancestry) {
  ancestry=ancestry||[]
  var count=0,tokens=registryTokens(source)
  for(var t=0;t<tokens.length;t++){
    var match=tokens[t]
    var raw=match[1].trim()
    if(/^wait\s/i.test(raw))count++
    else if(/^profile\s/i.test(raw)&&ancestry.length<16){
      try{var name=presetName(raw.slice(8).trim(),context.profiles);if(ancestry.indexOf(name)<0)count+=waitCount(context.profiles[name].script||"",context,ancestry.concat([name]))}catch(e){}
    }
  }
  return count
}
function validateStructure(source) {
  var tokens=registryTokens(source),at=0,branches=[]
  for(var t=0;t<tokens.length;t++){
    var match=tokens[t]
    if(source.slice(at,match.index).trim())throw Error("Use {type text} for literal text")
    at=match.end;var op=match[1].trim().split(/\s+/)[0].toLowerCase()
    if(op==="if"){if(branches.length>=16)throw Error("Conditions nested too deeply");branches.push(false)}
    else if(op==="else"){if(!branches.length||branches[branches.length-1])throw Error("Unexpected else");branches[branches.length-1]=true}
    else if(op==="endif"){if(!branches.length)throw Error("Unexpected endif");branches.pop()}
  }
  if(source.slice(at).trim())throw Error("Use {type text} for literal text")
  if(branches.length)throw Error("Missing endif")
}
function remainingWaits(next,context) {
  if(!next)return 0
  return next.frames.reduce(function(sum,frame){return sum+waitCount(frame.source,context,frame.ancestry)},0)
}
function compile(source,original,context,resume) {
  var state=copy(original),steps=[],stack=resume?copy(resume.stack):[],enabled=stack.length?stack[stack.length-1].parent&&stack[stack.length-1].value!==stack[stack.length-1].otherwise:true,
      focusClick=resume?resume.focusClick:false,live=resume?copy(resume.live):{},actions=resume?resume.actions:0,paused=null,leadingWait=!!resume
  if(context.deferred&&!resume)validateStructure(String(source))
  function emit(action) { var last=steps[steps.length-1];if(!last||last.kind!=="input"){last={kind:"input",actions:[]};steps.push(last)}last.actions.push(action);if(++actions>10000)throw Error("Too many actions") }
  function local(step){steps.push(step)}
  function buffers(text){return text.split(/[,\s]+/).filter(Boolean).map(function(k){return reference(k.toUpperCase(),state)}).join("")}
  function scan(source,ancestry,depth){
  if(source.length>1000000)throw Error("Expanded workflow exceeds 1 MiB")
  var tokens=registryTokens(source),at=0,initialDepth=depth===undefined?stack.length:depth,expanded=""
  function append(text){if(expanded.length+text.length>1000000)throw Error("Expanded workflow exceeds 1 MiB");expanded+=text}
  for(var t=0;t<tokens.length;t++){
    var match=tokens[t]
    if(source.slice(at,match.index).trim())throw Error("Use {type text} for literal text")
    at=match.end
    // Split only the command name. The payload is literal text, not words to
    // join back together: preserve its line breaks, tabs and repeated spaces.
    var raw=match[1].trim(),head=/^(\S+)(?:\s([\s\S]*))?$/.exec(raw),op=head?head[1].toLowerCase():"",arg=head&&head[2]!==undefined?head[2]:""
    if(op==="if"){if(stack.length>=16)throw Error("Conditions nested too deeply");var value=enabled?condition(arg,state,context):false;stack.push({parent:enabled,value:value,otherwise:false});enabled=enabled&&value;append(match[0]);continue}
    if(op==="else"){if(stack.length<=initialDepth||stack[stack.length-1].otherwise)throw Error("Unexpected else");var frame=stack[stack.length-1];frame.otherwise=true;enabled=frame.parent&&!frame.value;append(match[0]);continue}
    if(op==="endif"){if(stack.length<=initialDepth)throw Error("Unexpected endif");enabled=stack.pop().parent;append(match[0]);continue}
    if(!enabled){append(match[0]);continue}
    if(op==="wait"&&context.deferred&&!leadingWait){
      paused={frames:[{source:source.slice(match.index),ancestry:ancestry,depth:initialDepth}],stack:copy(stack),focusClick:focusClick,live:copy(live),actions:actions}
      return expanded
    }
    var rawArg=arg;arg=expand(arg,state)
    if(op==="profile"){
      if(context.profileError)throw Error(context.profileError)
      var name=presetName(arg,context.profiles),data=context.profiles[name]
      if(ancestry.indexOf(name)>=0||ancestry.length>=16)throw Error("Recursive preset: "+name)
      state=mergePreset(state,data)
      ;["vars","buffers"].forEach(function(section){Object.keys(data[section]||{}).forEach(function(k){delete live[k]})})
      if(context.deferred)validateStructure(String(data.script||""))
      append(scan(String(data.script||""),ancestry.concat([name])))
      if(paused){paused.frames.push({source:source.slice(at),ancestry:ancestry,depth:initialDepth});return expanded}
      continue
    }
    if(op==="wait"&&context.deferred){
      leadingWait=false
    }
    append("{"+op+(arg?" "+arg:"")+"}")
    if(op==="set"){
      var setting=arg.match(/^(?:(var|paste|buffer|zone|point|shell)\s+)?([^=\s]+)\s*(?:=\s*|\s+)([\s\S]*)$/i)
      if(!setting)throw Error("Use {set A value}, {set paste Z value}, {set point 1 x,y}")
      var kind=(setting[1]||"").toLowerCase(),k=setting[2].toUpperCase(),v=setting[3]
      if((!kind||kind==="paste"||kind==="buffer")&&/^[ZXCV]BUF$/.test(k))k=k[0]
      if((!kind||kind==="var")&&/^[ASDF]$/.test(k)){state.vars[k]=v;delete live[k]}
      else if((!kind||kind==="paste"||kind==="buffer")&&/^[ZXCV]$/.test(k)){state.buffers[k]=v;delete live[k]}
      else if(kind==="shell"&&/^[XCGPT]$/.test(k))state.shells[k]=v
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
    else if(op==="press")emit(press(arg))
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
    else if(op==="shell"||["terminal","pwsh","powershell","bash","cmd","python","py","regedited","regbin"].indexOf(op)>=0){
      var alias=op==="shell"?arg.toUpperCase():op==="bash"?"X":op==="cmd"?"C":op==="pwsh"||op==="powershell"?"P":"G"
      if(!/^[XCGP]$/.test(alias))throw Error("Shell alias must be X, C, G or P")
      var terminal=state.shells.T?keys(state.shells.T,100):context.shortcuts&&context.shortcuts.terminal
      if(!terminal)throw Error("Choose a terminal shortcut in Workflow or the plugin's shortcuts setting")
      emit(copy(terminal));emit(wait(700))
      var command=op==="terminal"?"":state.shells[alias]===undefined&&alias==="P"?"pwsh":state.shells[alias]||""
      if(/[\r\n\x00-\x1f\x7f]/.test(command))throw Error("Shell command must be a single line")
      if(command){emit({type:"type",text:String(command),interval_ms:0});emit(keys("ENTER"))}
    }else if(["exec","system","sys"].indexOf(op)>=0)throw Error("No implicit system commands; use an explicitly permitted Omarsket task")
    else emit(keys(raw))
  }
  if(source.slice(at).trim())throw Error("Use {type text} for literal text")
  if(stack.length!==initialDepth)throw Error("Missing endif")
  return expanded
  }
  var expanded="",frames=resume?resume.frames:[{source:String(source),ancestry:[],depth:0}]
  for(var f=0;f<frames.length;f++){
    expanded+=scan(frames[f].source,frames[f].ancestry,frames[f].depth)
    if(paused){paused.frames=paused.frames.concat(frames.slice(f+1));break}
  }
  return {state:state,steps:steps,expanded:expanded,next:paused}
}
