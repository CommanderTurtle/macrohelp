function categories(){return {
  "Commons":"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789".split("").concat(["SPACE","ENTER","TAB","BACKSPACE","ESCAPE"]),
  "Modifiers":["CONTROL_LEFT","CONTROL_RIGHT","SHIFT_LEFT","SHIFT_RIGHT","ALT_LEFT","ALT_GR","WINDOWS","FN"],
  "Mouse buttons":["LEFT_MOUSE","RIGHT_MOUSE","MIDDLE_MOUSE","XBUTTON_1","XBUTTON_2","MOUSE_WHEEL_UP","MOUSE_WHEEL_DOWN"],
  "NumPad":Array.from({length:10},function(_,i){return "NUMPAD_"+i}).concat(["+","-","*","/",".",",","NUMPAD_ENTER","NUM_LOCK"]),
  "Specials":["PRINT_SCREEN","PRINT","INSERT","DELETE"],
  "Navigations":["UP_ARROW","DOWN_ARROW","LEFT_ARROW","RIGHT_ARROW","HOME","END","PAGE_UP","PAGE_DOWN"],
  "Functions":Array.from({length:24},function(_,i){return "F"+(i+1)}),
  "Toggles":["CAPS_LOCK","NUM_LOCK","SCROLL_LOCK"],
  "Symbols":["+","-","*","/",".",",",";:","/?","`~","[{","\\|","]}","'\""],
  "Media":["VOLUME_UP","VOLUME_DOWN","VOLUME_MUTE","NEXT_TRACK","PREVIOUS_TRACK","PAUSE"]
}}
function append(flow,action,delay){var next=JSON.parse(JSON.stringify(flow));if(next.length+(next.length&&delay?2:1)>10000)throw Error("Flow limit reached");if(next.length&&delay)next.push({type:"wait",duration:delay/1000});next.push(JSON.parse(JSON.stringify(action)));return next}
function chord(keys){var list=keys.filter(Boolean);if(!list.length||list.length>3)throw Error("Choose 1–3 keys");if(list.indexOf("FN")>=0)throw Error("Fn is firmware-only; choose the key/chord your keyboard emits instead");return list}
