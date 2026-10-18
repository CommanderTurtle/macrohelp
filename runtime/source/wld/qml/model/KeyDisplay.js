// Physical evdev identities; these do not translate keys into typed text.
var names = {
  1:"ESCAPE",2:"1",3:"2",4:"3",5:"4",6:"5",7:"6",8:"7",9:"8",10:"9",11:"0",
  12:"MINUS",13:"EQUAL",14:"BACKSPACE",15:"TAB",16:"Q",17:"W",18:"E",19:"R",20:"T",21:"Y",22:"U",23:"I",24:"O",25:"P",
  26:"BRACKET_LEFT",27:"BRACKET_RIGHT",28:"ENTER",29:"CONTROL_LEFT",30:"A",31:"S",32:"D",33:"F",34:"G",35:"H",36:"J",37:"K",38:"L",
  39:"SEMICOLON",40:"APOSTROPHE",41:"GRAVE",42:"SHIFT_LEFT",43:"BACKSLASH",44:"Z",45:"X",46:"C",47:"V",48:"B",49:"N",50:"M",
  51:"COMMA",52:"PERIOD",53:"SLASH",54:"SHIFT_RIGHT",55:"NUMPAD_STAR",56:"ALT_LEFT",57:"SPACEBAR",58:"CAPS_LOCK",
  59:"F1",60:"F2",61:"F3",62:"F4",63:"F5",64:"F6",65:"F7",66:"F8",67:"F9",68:"F10",69:"NUM_LOCK",70:"SCROLL_LOCK",
  71:"NUMPAD_7",72:"NUMPAD_8",73:"NUMPAD_9",74:"NUMPAD_MINUS",75:"NUMPAD_4",76:"NUMPAD_5",77:"NUMPAD_6",78:"NUMPAD_PLUS",
  79:"NUMPAD_1",80:"NUMPAD_2",81:"NUMPAD_3",82:"NUMPAD_0",83:"NUMPAD_DOT",86:"BACKSLASH",87:"F11",88:"F12",
  96:"NUMPAD_ENTER",97:"CONTROL_RIGHT",98:"NUMPAD_SLASH",99:"PRINT_SCREEN",100:"ALT_GR",102:"HOME",103:"UP_ARROW",104:"PAGE_UP",
  105:"LEFT_ARROW",106:"RIGHT_ARROW",107:"END",108:"DOWN_ARROW",109:"PAGE_DOWN",110:"INSERT",111:"DELETE",
  113:"VOLUME_MUTE",114:"VOLUME_DOWN",115:"VOLUME_UP",116:"POWER",119:"PAUSE",125:"SUPER_LEFT",126:"SUPER_RIGHT",127:"MENU",
  163:"MEDIA_NEXT",164:"MEDIA_PLAY_PAUSE",165:"MEDIA_PREVIOUS",166:"MEDIA_STOP",464:"FN",
  272:"LEFT_MOUSE",273:"RIGHT_MOUSE",274:"MIDDLE_MOUSE",275:"XBUTTON_1",276:"XBUTTON_2"
}
var glyphs = {
  CONTROL_LEFT:0x244d,CONTROL_RIGHT:0x244e,SHIFT_LEFT:0x244f,SHIFT_RIGHT:0x2450,ALT_LEFT:0x244b,ALT_GR:0x244a,
  SUPER_LEFT:0x242a,SUPER_RIGHT:0x242a,FN:0x2426,LEFT_MOUSE:0x278b,RIGHT_MOUSE:0x278a,MIDDLE_MOUSE:0x278c,XBUTTON_1:0x278d,XBUTTON_2:0x278e,
  UP_ARROW:0x23f6,DOWN_ARROW:0x23f7,LEFT_ARROW:0x23f4,RIGHT_ARROW:0x23f5,TAB:0x242b,CAPS_LOCK:0x242c,BACKSPACE:0x242d,ENTER:0x242e,
  ESCAPE:0x242f,PRINT_SCREEN:0x2430,SCROLL_LOCK:0x2431,NUM_LOCK:0x2433,INSERT:0x2434,HOME:0x2435,PAGE_UP:0x2436,
  DELETE:0x2437,END:0x2438,PAGE_DOWN:0x2439,SPACEBAR:0x243a,NUMPAD_DOT:0x247e,NUMPAD_ENTER:0x247f,
  NUMPAD_MINUS:0x2480,NUMPAD_PLUS:0x2481,NUMPAD_SLASH:0x2482,NUMPAD_STAR:0x2483,COMMA:0xff0c
}
function name(code) { return names[code] || "KEY_" + code }
function glyph(key) {
  if (/^[A-Z]$/.test(key)) return String.fromCharCode(0xff21+key.charCodeAt(0)-65)
  if (/^[0-9]$/.test(key)) return String.fromCharCode(0xff10+Number(key))
  if (/^F([1-9]|1[0-2])$/.test(key)) return String.fromCharCode(0x245f+Number(key.slice(1)))
  if (/^NUMPAD_[0-9]$/.test(key)) return String.fromCharCode(key==="NUMPAD_0"?0x247d:0x2473+Number(key.slice(-1)))
  return glyphs[key] ? String.fromCharCode(glyphs[key]) : key.replace(/_/g," ")
}
function modifier(code) { return [29,42,54,56,97,100,125,126].indexOf(code)>=0 }
function create() { return {held:{},entries:[],modifiersUsed:false} }
function push(state,codes,now) {
  var keys=codes.map(name), signature=keys.join("+")
  var previous=state.entries[state.entries.length-1]
  if(previous&&previous.signature===signature&&now-previous.time<80)return
  state.entries.push({keys:keys,signature:signature,time:now})
  state.entries=state.entries.slice(-15)
}
function accept(state,events,held,now) {
  state={held:Object.assign({},state.held),entries:state.entries.slice(),modifiersUsed:state.modifiersUsed}
  events.forEach(function(event){
    var code=event[0],down=event[1]===1
    if(down){
      if(state.held[code])return
      state.held[code]=true
      if(!modifier(code)){
        push(state,Object.keys(state.held).map(Number).filter(function(k){return modifier(k)||k===code}).sort(function(a,b){return Number(modifier(b))-Number(modifier(a))||a-b}),now)
        state.modifiersUsed=true
      }
    }else{
      if(modifier(code)&&!state.modifiersUsed&&state.held[code])push(state,Object.keys(state.held).map(Number).filter(modifier),now)
      delete state.held[code]
      if(!Object.keys(state.held).some(function(k){return modifier(Number(k))}))state.modifiersUsed=false
    }
  })
  // Recover held state if the small compositor-side buffer was overrun.
  state.held={};held.forEach(function(code){state.held[code]=true})
  state.entries=state.entries.filter(function(e){return now-e.time<5000})
  return state
}
function rows(state,now) {
  return state.entries.filter(function(e){return now-e.time<5000}).slice(-11).map(function(e){return {keys:e.keys,opacity:Math.max(0,1-(now-e.time)/5000)}})
}
