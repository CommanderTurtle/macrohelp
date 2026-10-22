import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import "model/Workflow.js" as Workflow
import "model/KeyDisplay.js" as KeyDisplay

Item {
  id: root
  property var shell: null
  property var manifest: null
  property var settings: ({})
  property bool ready: false
  property bool connected: false
  property bool refreshing: false
  property bool hidden: false
  property bool busy: false
  property bool localBusy: false
  property string error: ""
  property string message: ""
  property string page: "Workflow"
  property string endpoint: ""
  property var environment: ({})
  property var cursor: ({x:0,y:0})
  property var physicalCursor: ({x:0,y:0})
  property var monitors: []
  property var tasks: []
  property var status: ({})
  property var history: []
  property var keyDisplay: KeyDisplay.create()
  property bool keyFeedAvailable: false
  property var heatmapState: ({version:1,active:null,sessions:[]})
  property string heatmapError: ""
  property var profileLibrary: ({presets:{},directory:""})
  property string profileError: ""
  property string resolvedScript: ""
  property double keyClock: Date.now()
  readonly property bool keysEnabled: ready&&!hidden&&!!environment.view&&environment.view.keys&&!environment.view.coordinates_only
  readonly property var keyRows: KeyDisplay.rows(keyDisplay,keyClock)
  property var path: []
  property bool recordingPath: false
  property double pathTime: 0
  property var circleOrigin: [0,0]
  property real radius: 100
  property real angle: 0
  property string unit: "degrees"
  property int pointIndex: 0
  property string zoneSlot: "Z"
  property var zoneStart: null
  property int serial: 0
  property int generation: 0
  property var callbacks: ({})
  property var steps: []
  property int stepIndex: 0
  property var currentJob: null
  property var pending: null
  property var pickerCallback: null
  property var activePanel: null
  property var clipboardCallback: null
  property int clipboardSerial: 0
  readonly property string helper: (Quickshell.env("XDG_DATA_HOME") || Quickshell.env("HOME")+"/.local/share")+"/macrohelp/overlay/bin/macrohelpctl"
  readonly property var helperCommand: [helper,"session"].concat(settings&&settings.envFile?["--env",String(settings.envFile)]:[])
  readonly property var circleTarget: Workflow.circle(circleOrigin,radius,angle,unit)
  signal showPanel()
  signal hidePanel()
  signal clipboardRequested(var callback)
  onKeysEnabledChanged: {
    keyDisplay=KeyDisplay.create()
    if(runtime.running)call({method:"keys",enabled:keysEnabled})
  }
  function call(request,callback) {
    if(!runtime.running){error="Build the overlay client, then reload the Macrohelp plugin.";if(callback)callback(null,error);return}
    request.id=++serial;if(callback)callbacks[request.id]=callback
    runtime.write(JSON.stringify(request)+"\n")
  }
  function http(verb,path,body,callback){call({method:"http",verb:verb,path:path,body:body||{}},callback)}
  function remember(text){history=history.concat([text]).slice(-6)}
  function change(section,key,value){var next=Workflow.copy(environment);if(section){if(!next[section])next[section]={};next[section][key]=value}else next[key]=value;environment=next;persist.restart()}
  function save(){if(ready)call({method:"save-state",state:environment})}
  function refresh(){
    if(refreshing)return
    refreshing=true
    http("GET","/health",{},function(value,failure){root.connected=!failure;if(failure){root.refreshing=false;return}root.http("GET","/tasks",{},function(v,e){if(!e)root.tasks=v.tasks||[];root.http("GET","/status",{},function(v,e){root.refreshing=false;if(!e)root.status=v})})})
  }
  function panel(name){if(["Workflow","Pointer","Keys","Buffers","Zones","Tasks","Presets","View","Help"].indexOf(name)<0)return;page=name;activePanel=null;showPanel()}
  function showDialog(kind){dialogs.openDialog(kind)}
  function clearWorkflow(){if(busy||localBusy||pending){error="Stop the current operation first";return}generation++;environment=Workflow.clearState(environment);resolvedScript="";save();message="Workflow cleared; paste buffers and zones retained"}
  function vector(text){var v=Workflow.vector(text,circleOrigin);radius=v.radius;angle=v.angle;unit="radians"}
  function exitOverlay(){cancelWork();hidePanel();if(!disablePlugin.running)disablePlugin.running=true}
  function cancelWork(){generation++;steps=[];stepIndex=0;currentJob=null;busy=false;localBusy=false;pending=null;hidden=false;recordingPath=false;clipboardCallback=null;startDelay.stop();poll.stop();clipboardTimeout.stop()}
  function stop(){cancelWork();if(!emergency.running)emergency.running=true;message="";remember("Stop requested")}
  function reconnect(){if(!runtime.running)runtime.running=true;else refresh()}
  function queue(actions,callback,delay,name,saveOnly){
    if(!actions.length){if(callback)callback(null,"No actions");return}
    var token=generation
    var id=name||("Macrohelp-"+Date.now()+"-"+(serial+1))
    http("POST","/temp-task",{name:id,delay:delay===undefined?1:delay,run:!saveOnly,cleanup:!saveOnly,task:{actions:actions}},function(v,e){
      if(token!==root.generation)return
      if(!e&&v.task){root.change("","last_task",v.task);root.remember(id+" #"+v.task.task_number)}
      if(callback)callback(v,e)
    })
  }
  function later(actions){if(busy||localBusy||pending){error="An operation is already pending";return}hidePanel();pending=function(){root.queue(actions)};startDelay.restart()}
  function pointer(){if(!ready)return;var p=[Math.round(cursor.x),Math.round(cursor.y)];change("points",pointIndex,p);remember("Point "+(pointIndex+1)+": "+p.join(", "))}
  function corner(){var p=[Math.round(cursor.x),Math.round(cursor.y)];if(!zoneStart){zoneStart=p;message="First corner saved"}else{change("zones",zoneSlot,zoneStart.concat(p));zoneStart=null;message="Zone "+zoneSlot+" saved"}}
  function togglePath(){recordingPath=!recordingPath;if(recordingPath){path=[[0,0,Math.round(cursor.x),Math.round(cursor.y)]];pathTime=Date.now();message="Recording pointer path"}else message="Path: "+path.length+" points"}
  function click(button){later([Workflow.keys(button)])}
  function runSaved(name){if(busy||localBusy||pending){error="Stop the current operation first";return}localBusy=true;var token=generation;hidePanel();http("POST","/run",{task:name},function(v,e){if(token!==root.generation)return;root.localBusy=false;if(!e){root.change("","last_task",v);root.message="Scheduled #"+v.task_number}})}
  function play(source){
    if(busy||localBusy||pending){error="Stop the current operation first";return}
    var token=++generation;busy=true;error=""
    refreshContext(function(context,failure){
      if(token!==root.generation)return
      if(failure){root.busy=false;return}
      try {var plan=Workflow.compile(source,root.environment,context);root.resolvedScript=plan.expanded;root.environment=plan.state;root.save();root.steps=plan.steps;root.stepIndex=0;root.hidePanel();root.pending=function(){root.nextStep(token)};startDelay.restart()}
      catch(e){root.busy=false;root.error=e.message}
    })
  }
  function loadProfiles(callback){call({method:"profile-list"},function(v,e){root.profileError=e||"";if(!e)root.profileLibrary=v;if(callback)callback(v,e)})}
  function saveProfile(name,fields,replace){call({method:"profile-save",name:name,fields:fields,replace:!!replace,state:environment},function(v,e){root.profileError=e||"";if(!e)root.profileLibrary=v})}
  function importProfile(name){if(busy||localBusy||pending){error="Stop the current operation first";return}var token=generation;call({method:"profile-import",name:name},function(v,e){if(token!==root.generation)return;if(!e){root.environment=v;root.save()}})}
  function refreshContext(callback){http("GET","/tasks",{},function(t,e){if(e){callback(null,e);return}root.http("GET","/status",{},function(s,e){if(e){callback(null,e);return}root.call({method:"bindings"},function(bindings,e){if(e){callback(null,e);return}var context;try{context={tasks:t.tasks,status:s,shortcuts:Workflow.desktopShortcuts(bindings,root.settings.shortcuts)}}catch(failure){root.error=failure.message;callback(null,failure.message);return}root.loadProfiles(function(v,e){context.profiles=v?v.presets:{};context.profileError=e||"";callback(context,null)})})})})}
  function saveSchedule(name){
    if(busy||localBusy||pending){error="Stop the current operation first";return}
    localBusy=true;var token=generation
    refreshContext(function(context,failure){
      if(token!==root.generation)return
      if(failure){root.localBusy=false;return}
      try{var plan=Workflow.compile(root.environment.script,root.environment,context);if(plan.steps.some(function(s){return s.kind!=="input"}))throw Error("Clipboard/registry steps run through Play. Save the workflow with Export environment.");var actions=[];plan.steps.forEach(function(s){actions=actions.concat(s.actions)});root.queue(actions,function(v,e){root.localBusy=false;if(!e){root.message="Saved "+name;root.refresh()}},1,name,true)}catch(e){root.localBusy=false;root.error=e.message}
    })
  }
  function nextStep(token){
    if(token!==generation||!busy)return
    if(stepIndex>=steps.length){busy=false;hidden=false;message="Finished";save();refresh();return}
    var step=steps[stepIndex++]
    function done(value,error){if(token!==root.generation)return;if(error){root.busy=false;root.hidden=false;if(root.connected)root.error=error;return}if(step.slot&&value&&value.text!==undefined)root.change(/^[ASDF]$/.test(step.slot)?"vars":"buffers",step.slot,value.text);root.nextStep(token)}
    if(step.kind==="input"){hidden=!!step.capture;queue(Workflow.resolve(step.actions,environment),function(v,e){if(token!==root.generation)return;if(e){done(null,e);return}root.currentJob=v.task;poll.restart()},0)}
    else if(step.kind==="clipboard")readClipboard(function(v,e){if(token!==root.generation)return;root.hidePanel();root.pending=function(){done(v,e)};startDelay.restart()})
    else if(step.kind==="regedited")call({method:"regedited",action:step.action,file:step.file,reference:step.reference},done)
  }
  function finishClipboard(value,failure){var callback=clipboardCallback;clipboardCallback=null;clipboardTimeout.stop();localBusy=false;if(callback)callback(value,failure)}
  function readClipboard(callback){if(localBusy){if(callback)callback(null,"Clipboard operation already active");return}localBusy=true;var token=generation,request=++clipboardSerial;page="Buffers";clipboardCallback=callback;showPanel();if(!activePanel){finishClipboard(null,"Open the Macrohelp bar panel to read the clipboard");return}clipboardTimeout.restart();clipboardRequested(function(v,e){if(token===root.generation&&request===root.clipboardSerial&&root.clipboardCallback)root.finishClipboard(v,e)})}
  function useZone(slot,method,target){play(target?"{zonebuf "+slot+","+method+","+target+"}":"{zone "+slot+","+method+"}")}
  function chooseFile(save,callback,filename){if(picker.running){error="File chooser already open";return}pickerCallback=callback;picker.command=["zenity","--file-selection","--title=Macrohelp"].concat(save?["--save"]:[]).concat(filename?["--filename="+filename]:[]);picker.running=true}
  function transferPath(kind){var paths=environment.paths||{};return String(paths[kind]||"").trim()}
  function browsePath(kind){var token=generation;chooseFile(kind==="export",function(path){if(token===root.generation)root.change("paths",kind,path)},transferPath(kind)|| (kind==="export"?"macrohelp-environment.json":""))}
  function exportState(format){
    var token=generation,snapshot=Workflow.copy(environment)
    function run(path){if(token!==root.generation)return;root.change("paths","export",path);snapshot.paths=Workflow.copy(root.environment.paths);root.call({method:"export",path:path,state:snapshot,format:format||"json"},function(v,e){if(token!==root.generation)return;if(!e)root.message="Exported "+path})}
    var path=transferPath("export");if(path)run(path);else chooseFile(true,run,format==="windows"?"macrohelp-environment.txt":"macrohelp-environment.json")
  }
  function importState(){
    if(busy||localBusy||pending){error="Stop the current operation first";return}
    var token=generation
    function run(path){if(token!==root.generation)return;root.localBusy=true;root.call({method:"import",path:path},function(v,e){if(token!==root.generation)return;root.localBusy=false;if(!e){root.environment=v;root.change("paths","import",path);root.save();root.message="Imported"}})}
    var path=transferPath("import");if(path)run(path);else chooseFile(false,run)
  }
  function exportActions(name,actions,append){
    if(!actions.length){error="No actions to export";return}
    var snapshot=Workflow.copy(actions),token=generation
    var filename=String(name||"actions").replace(/[^a-zA-Z0-9 _.-]/g,"_").slice(0,100)+".json"
    chooseFile(true,function(path){if(token!==root.generation)return;root.call({method:"export-actions",path:path,actions:snapshot,append:!!append},function(v,e){if(token!==root.generation)return;if(!e)root.message="Saved "+v.count+" actions · "+v.path})},filename)
  }
  Timer {id:persist;interval:400;onTriggered:root.save()}
  Timer {
    interval:100;repeat:true;running:root.keysEnabled&&root.keyDisplay.entries.length>0
    onTriggered:{root.keyClock=Date.now();root.keyDisplay=KeyDisplay.accept(root.keyDisplay,[],Object.keys(root.keyDisplay.held).map(Number),root.keyClock)}
  }
  Timer {id:clipboardTimeout;interval:3000;onTriggered:root.finishClipboard(null,"Clipboard panel unavailable")}
  Timer {id:startDelay;interval:250;onTriggered:{var f=root.pending;root.pending=null;if(f)f()}}
  Timer {id:poll;interval:120;onTriggered:{
    var token=root.generation,job=root.currentJob;if(!job)return
    root.http("GET","/check?id="+job.task_number,{},function(v,e){
      if(token!==root.generation)return
      if(e){root.busy=false;root.hidden=false;root.currentJob=null;return}
      if(v.session!==job.session){root.busy=false;root.hidden=false;root.currentJob=null;root.error="Omarsket restarted; workflow stopped";return}
      if(v.state==="scheduled"||v.state==="running"){poll.restart();return}
      root.currentJob=null;root.hidden=false
      if(v.state!=="finished"){root.busy=false;root.error=v.error||v.state;return}
      root.nextStep(token)
    })
  }}
  Process {id:picker;stdout:StdioCollector{id:picked;waitForEnd:true} onExited:function(code){var f=root.pickerCallback;root.pickerCallback=null;if(code===0&&f)f(String(picked.text).trim())}}
  Process {
    id:emergency
    command:[root.helper,"stop"].concat(root.settings&&root.settings.envFile?["--env",String(root.settings.envFile)]:[])
  }
  Process {id:disablePlugin;command:["omarchy","plugin","disable","io.github.commanderturtle.macrohelp"]}
  Process {
    id:runtime;command:root.helperCommand;stdinEnabled:true;running:true
    onStarted:root.call({method:"hello"},function(value,failure){if(failure)return;root.environment=value.state;root.monitors=value.monitors;root.endpoint=value.endpoint;root.ready=true;root.refresh();root.call({method:"heatmap",action:"snapshot"},function(v,e){if(!e)root.heatmapState=v;else root.heatmapError=e})})
    stdout:SplitParser {onRead:function(line){
      var r;try{r=JSON.parse(line)}catch(e){root.error="Invalid controller response";return}
      if(r.event==="cursor"){
        root.cursor=r.cursor
        if(r.physical)root.physicalCursor=r.physical
        if(root.recordingPath){if(root.path.length>=10000){root.recordingPath=false;root.error="Path limit reached"}else{var now=Date.now();root.path=root.path.concat([[0,Math.max(1,now-root.pathTime),Math.round(r.cursor.x),Math.round(r.cursor.y)]]);root.pathTime=now}}
        return
      }
      if(r.event==="monitors"){root.monitors=r.monitors;return}
      if(r.event==="heatmap-session"){
        var book=Object.assign({},root.heatmapState);book.active=r.active
        book.sessions=(book.sessions||[]).map(function(s){return s.id===r.active?r.session:s})
        root.heatmapState=book;return
      }
      if(r.event==="heatmap-error"){root.heatmapError=r.error;return}
      if(r.event==="keys"){
        root.keyFeedAvailable=!!r.sample.available
        root.keyClock=Date.now()
        if(!root.keysEnabled||r.sample.locked||!root.keyFeedAvailable)root.keyDisplay=KeyDisplay.create()
        else root.keyDisplay=KeyDisplay.accept(root.keyDisplay,r.sample.events||[],r.sample.held||[],root.keyClock)
        return
      }
      if(r.event==="dialog"){
        var kinds={"0":"save_cursor","1":"paste_buffers","2":"registry_hub","3":"circle_placer","4":"click_left","5":"click_right","6":"click_middle","7":"stop_all","9":"record_keys"}
        var kind=kinds[r.key]||"save_cursor"
        root.showDialog(kind)
        var blink=null;for(var i=0;i<root.tasks.length;i++){if(root.tasks[i].name.indexOf("Blink to ")===0&&root.tasks[i].name.indexOf("(center bottom)")>0){blink=root.tasks[i].name;break}}
        if(blink)root.http("POST","/run",{task:blink},function(v,e){})
        return
      }
      var callback=root.callbacks[r.id];delete root.callbacks[r.id]
      if(r.offline){root.connected=false;root.error="";if(callback)callback(null,"offline");return}
      else if(!r.ok)root.error=r.error
      else root.error=""
      if(callback)callback(r.result,r.ok?null:r.error)
    }}
    stderr:SplitParser{onRead:function(line){root.error=line}}
    onExited:function(code){root.ready=false;root.connected=false;root.refreshing=false;root.cancelWork();persist.stop();var all=root.callbacks;root.callbacks=({});for(var id in all)all[id](null,"Controller stopped");if(code!==0)root.error="Controller exited ("+code+")"}
  }
  Hud {service:root}
  Dialogs {id:dialogs;service:root;width:parent.width;height:parent.height}
}
