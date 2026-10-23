import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import "model/Workflow.js" as Workflow
import "model/KeyDisplay.js" as KeyDisplay
import "model/Dialogs.js" as DialogState
import "model/Control.js" as Control

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
  readonly property var board: Workflow.board(cursor,monitors)
  property var monitors: []
  readonly property real coordinateScale: Workflow.monitorScale(monitors)
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
  property var dispatch: null
  property var pending: null
  property var pickerCallback: null
  property var activePanel: null
  property var clipboardCallback: null
  property int clipboardSerial: 0
  property double dialogCaptureUntil: 0
  property var dialogBlink: null
  property string controlAddress: ""
  property var controlRun: null
  property var controlLast: null
  property int controlPending: 0
  readonly property var quickCircle: dialogs.circlePreview
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
  function change(section,key,value){var next=Workflow.copy(environment);if(section==="view"){var patch={};patch[key]=value;next.view=Workflow.viewPatch(next.view,patch)}else if(section){if(!next[section])next[section]={};next[section][key]=value}else next[key]=value;environment=next;persist.restart()}
  function save(){if(ready&&!(controlRun&&controlRun.noReplace))call({method:"save-state",state:environment})}
  function refresh(){
    if(refreshing)return
    refreshing=true
    http("GET","/health",{},function(value,failure){root.connected=!failure;if(failure){root.refreshing=false;return}root.http("GET","/tasks",{},function(v,e){if(!e)root.tasks=v.tasks||[];root.http("GET","/status",{},function(v,e){root.refreshing=false;if(!e)root.status=v})})})
  }
  function panel(name){if(["Workflow","Pointer","Keys","Buffers","Zones","Tasks","Presets","View","Help"].indexOf(name)<0)return;page=name;activePanel=null;showPanel()}
  function showDialog(kind){dialogs.openDialog(kind)}
  function blinkDialog(task){
    // Freeze the same physical coordinate snapshot used by actions and the HUD.
    var lease={point:[Math.round(physicalCursor.x),Math.round(physicalCursor.y)],revision:dialogs.revision,ready:false,restore:false}
    dialogBlink=lease;dialogs.working=true
    http("POST","/run",{task:task},function(v,e){
      if(root.dialogBlink!==lease)return
      if(e){root.dialogBlink=null;if(lease.revision===dialogs.revision)dialogs.working=false;return}
      dialogWarp.lease=lease;dialogWarp.restart()
    })
  }
  function dialogClosed(restore,keep){
    var lease=dialogBlink;if(!lease)return
    if(keep){lease.revision=dialogs.revision;return}
    if(!restore){dialogBlink=null;dialogWarp.stop();return}
    lease.restore=true;if(lease.ready)restoreDialogPointer(lease)
  }
  function restoreDialogPointer(lease){
    if(dialogBlink!==lease)return
    dialogBlink=null
    if(!ready)return
    var p=lease.point,name="Blink to "+p[0]+"x"+p[1]+" (last location)"
    // Same native move shape as the startup center-bottom blink. cleanup=true
    // runs it in memory through the daemon, without saving an .omscht file.
    http("POST","/temp-task",{name:name,run:true,cleanup:true,delay:0,task:{id:name,actions:[{type:"move",x:p[0],y:p[1],duration_ms:20}],allow_system:false,auto_start:false,delay_seconds:0,loop_times:1}},function(){})
  }
  function finishDialogBlink(lease){
    if(dialogBlink!==lease)return
    lease.ready=true
    if(lease.restore){restoreDialogPointer(lease);return}
    if(lease.revision===dialogs.revision&&dialogs.dialogOpen){dialogs.working=false;dialogs.reanchor()}
  }
  function clearWorkflow(){if(busy||localBusy||pending){error="Stop the current operation first";return}generation++;environment=Workflow.clearState(environment);resolvedScript="";save();message="Workflow cleared; paste buffers and zones retained"}
  function vector(text){var v=Workflow.vector(text,circleOrigin);radius=v.radius;angle=v.angle;unit="radians"}
  function exitOverlay(){cancelWork();hidePanel();if(!disablePlugin.running)disablePlugin.running=true}
  function cancelWork(){generation++;if(typeof runtime!=="undefined"&&runtime.running)call({method:"rgd-cancel"});dispatch=null;dialogs.closeDialog();steps=[];stepIndex=0;currentJob=null;busy=false;localBusy=false;pending=null;hidden=false;recordingPath=false;clipboardCallback=null;startDelay.stop();poll.stop();clipboardTimeout.stop();if(controlRun)finishControl("cancelled")}
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
  function later(actions){if(localBusy||pending||(busy&&!dispatch)){error="An operation is already pending";return}hidePanel();if(dispatch){queue(actions);return}pending=function(){root.queue(actions)};startDelay.restart()}
  function pointer(){if(!ready)return;var p=[Math.round(physicalCursor.x),Math.round(physicalCursor.y)];change("points",pointIndex,p);remember("Point "+(pointIndex+1)+": "+p.join(", "))}
  function corner(){var p=[Math.round(physicalCursor.x),Math.round(physicalCursor.y)];if(!zoneStart){zoneStart=p;message="First corner saved"}else{change("zones",zoneSlot,zoneStart.concat(p));zoneStart=null;message="Zone "+zoneSlot+" saved"}}
  function togglePath(){recordingPath=!recordingPath;if(recordingPath){path=[[0,0,Math.round(physicalCursor.x),Math.round(physicalCursor.y)]];pathTime=Date.now();message="Recording pointer path"}else message="Path: "+path.length+" points"}
  function recordPathPoint(){
    if(!recordingPath)return
    if(path.length>=10000){recordingPath=false;error="Path limit reached";return}
    var now=Date.now();path=path.concat([[0,Math.max(1,now-pathTime),Math.round(physicalCursor.x),Math.round(physicalCursor.y)]]);pathTime=now
  }
  function click(button){later([Workflow.keys(button)])}
  function runSaved(name){if(busy||localBusy||pending){error="Stop the current operation first";return}localBusy=true;var token=generation;hidePanel();http("POST","/run",{task:name},function(v,e){if(token!==root.generation)return;root.localBusy=false;if(!e){root.change("","last_task",v);root.message="Scheduled #"+v.task_number}})}
  function assemble(context,token,build,fail){
    if(token!==generation)return
    context.monitors=root.monitors
    try{build()}
    catch(e){
      if(!e.rgd){fail(e);return}
      var ref=e.rgd
      call({method:"rgd-read",file:ref.file,reference:ref.reference},function(value,error){
        if(token!==root.generation)return
        if(error||!value||typeof value.text!=="string"){fail(Error("rgd "+ref.reference+": "+(error||"expected text")));return}
        if(!context.rgd)context.rgd={}
        context.rgd[ref.key]=value.text
        root.assemble(context,token,build,fail)
      })
    }
  }
  function play(source,origin){
    if(busy||localBusy||pending){error="Stop the current operation first";return}
    var token=++generation;busy=true;error=""
    refreshContext(function(context,failure){
      if(token!==root.generation)return
      if(failure){root.busy=false;if(root.controlRun)root.controlRun.failure=failure;return}
      root.assemble(context,token,function(){context.deferred=true;var plan=Workflow.compile(source,root.environment,context);if(origin)plan.steps.unshift({kind:"input",actions:[Workflow.move(origin,0)]});root.dispatch={next:plan.next,index:1,total:1+Workflow.remainingWaits(plan.next,context),preparing:false};root.resolvedScript=plan.expanded;root.environment=plan.state;root.save();root.steps=plan.steps;root.stepIndex=0;root.hidePanel();root.pending=function(){root.nextStep(token)};startDelay.restart()},function(e){root.busy=false;root.dispatch=null;root.error=e.message;if(root.controlRun)root.controlRun.failure=e.message})
    })
  }
  function nextDispatch(token){
    if(token!==generation||!busy||!dispatch||dispatch.preparing)return
    var next=dispatch.next;dispatch.preparing=true
    refreshContext(function(context,failure){
      if(token!==root.generation||!root.busy||!root.dispatch)return
      if(failure){root.busy=false;root.dispatch=null;if(root.controlRun)root.controlRun.failure=failure;return}
      root.assemble(context,token,function(){
        context.deferred=true
        var plan=Workflow.compile("",root.environment,context,next)
        var index=root.dispatch.index+1
        root.dispatch={next:plan.next,index:index,total:index+Workflow.remainingWaits(plan.next,context),preparing:false}
        root.environment=plan.state;root.resolvedScript+=plan.expanded;root.save();root.steps=plan.steps;root.stepIndex=0;root.nextStep(token)
      },function(e){root.busy=false;root.dispatch=null;root.error=e.message;if(root.controlRun)root.controlRun.failure=e.message})
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
      if(failure){root.localBusy=false;if(root.controlRun)root.controlRun.failure=failure;return}
      root.assemble(context,token,function(){var plan=Workflow.compile(root.environment.script,root.environment,context);if(plan.steps.some(function(s){return s.kind!=="input"}))throw Error("Clipboard/registry steps run through Play. Save the workflow with Export environment.");var actions=[];plan.steps.forEach(function(s){actions=actions.concat(s.actions)});root.queue(actions,function(v,e){root.localBusy=false;if(e&&root.controlRun)root.controlRun.failure=e;if(!e){root.message="Saved "+name;root.refresh()}},1,name,true)},function(e){root.localBusy=false;root.error=e.message;if(root.controlRun)root.controlRun.failure=e.message})
    })
  }
  function nextStep(token){
    if(token!==generation||!busy)return
    if(stepIndex>=steps.length){if(dispatch&&dispatch.next){nextDispatch(token);return}dispatch=null;busy=false;hidden=false;message="Finished";save();refresh();return}
    var step=steps[stepIndex++]
    function done(value,error){if(token!==root.generation)return;if(error){root.busy=false;root.dispatch=null;root.hidden=false;if(root.controlRun)root.controlRun.failure=error;if(root.connected)root.error=error;return}if(step.slot&&value&&value.text!==undefined)root.change(/^[ASDF]$/.test(step.slot)?"vars":"buffers",step.slot,step.singleLine?value.text.replace(/[\r\n]+/g," ").trim():value.text);root.nextStep(token)}
    if(step.kind==="input"){hidden=!!step.capture;if(dispatch){message=dispatch.index+"/"+dispatch.total+" sequence is running";console.info("Macrohelp: "+message)}queue(Workflow.resolve(step.actions,environment),function(v,e){if(token!==root.generation)return;if(e){done(null,e);return}root.currentJob=v.task;poll.restart()},0,dispatch?"Macrohelp sequence "+dispatch.index+" of "+dispatch.total:undefined)}
    else if(step.kind==="clipboard")readClipboard(function(v,e){if(token!==root.generation)return;root.hidePanel();root.pending=function(){done(v,e)};startDelay.restart()})
    else if(step.kind==="regedited")call({method:"regedited",action:step.action,file:step.file,reference:step.reference},done)
  }
  function finishClipboard(value,failure){var callback=clipboardCallback;clipboardCallback=null;clipboardTimeout.stop();localBusy=false;if(callback)callback(value,failure)}
  function readClipboard(callback){if(localBusy){if(callback)callback(null,"Clipboard operation already active");return}localBusy=true;var token=generation,request=++clipboardSerial;clipboardCallback=callback;clipboardTimeout.restart();var complete=function(v,e){if(token===root.generation&&request===root.clipboardSerial&&root.clipboardCallback)root.finishClipboard(v,e)};if(activePanel&&activePanel.opened)clipboardRequested(complete);else dialogs.readClipboard(complete)}
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
  function appendActions(name,actions,callback){
    call({method:"append-actions",name:name,actions:actions},function(v,e){if(!e)root.message="Saved "+v.count+" actions · "+v.path;if(callback)callback(v,e)})
  }
  function captureZone(rect,method,target){
    if(busy||localBusy||pending){error="An operation is already pending";return}
    var token=++generation;busy=true;error=""
    refreshContext(function(context,failure){
      if(token!==root.generation)return
      if(failure){root.busy=false;if(root.controlRun)root.controlRun.failure=failure;return}
      try {
        root.steps=[{kind:"input",actions:Workflow.zone(rect,method,context.shortcuts),capture:true}]
        if(target)root.steps.push({kind:"clipboard",slot:target,singleLine:method==="C"})
        root.stepIndex=0;root.hidePanel();root.pending=function(){root.nextStep(token)};startDelay.restart()
      }catch(e){root.busy=false;root.error=e.message;if(root.controlRun)root.controlRun.failure=e.message}
    })
  }
  function controlSnapshot(){
    return {api_version:1,ready:ready,connected:connected,busy:busy||localBusy||!!pending||dialogs.working||!!controlRun,
      run:controlRun?controlRun.id:null,last_run:controlLast,environment:Workflow.copy(environment),sequence:dispatch?{index:dispatch.index,total:dispatch.total}:null,
      dialog:dialogs.dialogOpen?{menu:dialogs.dialogKind,stage:dialogs.dialogState.stage,state:Workflow.copy(dialogs.dialogState)}:null,
      last_task:environment.last_task||null,error:error||null}
  }
  function finishControl(failure){
    if(!controlRun)return
    var run=controlRun
    controlLast={id:run.id,no_replace:run.noReplace,error:failure||run.failure||error||null,environment:Workflow.copy(environment),last_task:environment.last_task||null}
    controlRun=null;controlPending=0
    if(run.noReplace){persist.stop();environment=run.saved;if(run.hadPersist)persist.restart()}
    else save()
  }
  function controlEffect(effect){
    if(effect.kind==="queue"){
      if(busy||localBusy||pending)throw Error("An operation is already pending")
      var token=++generation;busy=true;steps=[{kind:"input",actions:effect.actions}];stepIndex=0
      hidePanel();pending=function(){root.nextStep(token)};startDelay.restart()
    }else if(effect.kind==="play")play(environment.script,effect.origin)
    else if(effect.kind==="zone")captureZone(effect.rect,effect.method,effect.target)
    else if(effect.kind==="stop")stop()
    else if(effect.kind==="save_schedule")saveSchedule(effect.name)
    else if(effect.kind==="export"||effect.kind==="export_state"||effect.kind==="import_state"){
      controlPending++
      var token=controlRun.id
      var request=effect.kind==="export"?{method:"append-actions",name:effect.name,actions:effect.actions}:effect.kind==="export_state"?{method:"export",path:effect.path,state:Workflow.copy(environment)}:{method:"import",path:effect.path}
      call(request,function(value,failure){
        if(!root.controlRun||root.controlRun.id!==token)return
        root.controlPending--
        if(failure){root.error=failure;root.controlRun.failure=failure}
        else if(effect.kind==="import_state"){root.environment=value;root.save()}
      })
    }else throw Error("Unsupported control effect")
  }
  function handleControl(event){
    if(event.expires&&Date.now()>event.expires)return
    function answer(value,failure){root.call({method:"control-reply",ticket:event.ticket,result:value,error:failure})}
    var applied=false
    try {
      if(event.path==="/v1/health"){answer({api_version:1,service:"macrohelp-overlay",ready:ready,backend:"omarsket",connected:connected});return}
      if(event.path==="/v1/state"){answer(controlSnapshot());return}
      if(event.path==="/v1/help"){answer({platform:"wayland",topic:"registry_hub",text:DialogState.help()});return}
      if(event.path==="/v1/coords"){if(!ready){answer(null,{status:503,message:"Overlay not ready"});return}answer({x:Math.round(physicalCursor.x),y:Math.round(physicalCursor.y)});return}
      if(event.path==="/v1/board"){if(!ready){answer(null,{status:503,message:"Overlay not ready"});return}answer(board);return}
      if(event.path==="/v1/stop"){stop();answer({stop_requested:true,state:controlSnapshot()});return}
      var request=event.body
      if(request.menu===7){stop();answer(controlSnapshot());return}
      if(!ready){answer(null,{status:503,message:"Overlay not ready"});return}
      if(controlRun&&request.run!==controlRun.id){answer(null,{status:409,message:"Control run "+controlRun.id+" is active"});return}
      if(!controlRun&&request.run){answer(null,{status:404,message:"Control run is no longer active"});return}
      if(busy||localBusy||pending||dialogs.working||recordingPath||(activePanel&&activePanel.captureAction)||(!controlRun&&dialogs.dialogOpen)){answer(null,{status:409,message:"Overlay is busy; inspect state or stop first"});return}
      if(controlRun&&request.no_replace!==undefined&&request.no_replace!==controlRun.noReplace){answer(null,{status:409,message:"no_replace is fixed for the active run"});return}
      var plan=Control.prepare(request,environment,physicalCursor,controlRun?dialogs.dialogState:null,Workflow,DialogState,monitors)
      if(plan.effects.filter(function(e){return ["queue","play","zone","save_schedule"].indexOf(e.kind)>=0}).length>1)throw Error("Use one executable flow per control run")
      var before=Workflow.copy(environment)
      if(!controlRun){controlRun={id:String(Date.now())+"-"+(++serial),noReplace:!!request.no_replace,saved:before,hadPersist:persist.running};if(controlRun.noReplace)persist.stop()}
      applied=true
      error="";environment=plan.environment;save()
      if(plan.state&&!plan.closed){dialogs.openDialog(plan.state.kind);dialogs.dialogState=plan.state;dialogs.focusInput()}
      else dialogs.closeDialog()
      plan.effects.forEach(function(effect){root.controlEffect(effect)})
      answer(controlSnapshot())
    }catch(failure){if(applied&&controlRun){controlRun.failure=failure.message;dialogs.closeDialog()}answer(null,{status:400,message:failure.message})}
  }
  Timer {
    interval:100;repeat:true;running:root.controlRun!==null
    onTriggered:if(!root.busy&&!root.localBusy&&!root.pending&&!root.dialogsBusy()&&root.controlPending===0)root.finishControl()
  }
  function dialogsBusy(){return dialogs.dialogOpen||dialogs.working}
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
      if(e){root.busy=false;root.dispatch=null;root.hidden=false;root.currentJob=null;if(root.controlRun)root.controlRun.failure=e;return}
      if(v.session!==job.session){root.busy=false;root.dispatch=null;root.hidden=false;root.currentJob=null;root.error="Omarsket restarted; workflow stopped";if(root.controlRun)root.controlRun.failure=root.error;return}
      if(v.state==="scheduled"||v.state==="running"){poll.restart();return}
      root.currentJob=null;root.hidden=false
      if(v.state!=="finished"){root.busy=false;root.dispatch=null;root.error=v.error||v.state;if(root.controlRun)root.controlRun.failure=root.error;return}
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
    onStarted:root.call({method:"hello"},function(value,failure){if(failure)return;root.environment=value.state;root.monitors=value.monitors;root.endpoint=value.endpoint;root.ready=true;root.refresh();root.call({method:"control-start"},function(v,e){if(!e&&v.enabled)root.controlAddress=v.address;else if(e)root.error=e});root.call({method:"heatmap",action:"snapshot"},function(v,e){if(!e)root.heatmapState=v;else root.heatmapError=e})})
    stdout:SplitParser {onRead:function(line){
      var r;try{r=JSON.parse(line)}catch(e){root.error="Invalid controller response";return}
      if(r.event==="control"){root.call({method:"control-live",ticket:r.ticket},function(live,e){if(live&&!e)root.handleControl(r)});return}
      if(r.event==="cursor"){
        root.cursor=r.cursor
        if(r.physical)root.physicalCursor=r.physical
        root.recordPathPoint()
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
        if(dialogs.capturing){root.dialogCaptureUntil=Date.now()+200;dialogs.receiveKeys(r.sample)}
        root.keyFeedAvailable=!!r.sample.available
        root.keyClock=Date.now()
        if(!root.keysEnabled||r.sample.locked||!root.keyFeedAvailable)root.keyDisplay=KeyDisplay.create()
        else root.keyDisplay=KeyDisplay.accept(root.keyDisplay,r.sample.events||[],r.sample.held||[],root.keyClock)
        return
      }
      if(r.event==="dialog"){
        if(dialogs.capturing||Date.now()<root.dialogCaptureUntil)return
        var kinds={"0":"save_cursor","1":"paste_buffers","2":"registry_hub","3":"circle_placer","4":"click_left","5":"click_right","6":"click_middle","7":"stop_all","8":"view_toggles","9":"record_keys"}
        var kind=kinds[r.key]
        if(!kind)return
        if(root.controlRun&&!(root.dispatch&&(kind==="save_cursor"||kind==="circle_placer"))){if(kind==="stop_all")root.stop();else root.error="A control run is active; finish or stop it first";return}
        root.showDialog(kind)
        // Circle begins at the real pointer, with keyboard focus and no warp.
        if(kind==="circle_placer")return
        var blink=null;for(var i=0;i<root.tasks.length;i++){if(root.tasks[i].name.indexOf("Blink to ")===0&&root.tasks[i].name.indexOf("(center bottom)")>0){blink=root.tasks[i].name;break}}
        if(blink)root.blinkDialog(blink)
        return
      }
      var callback=root.callbacks[r.id];delete root.callbacks[r.id]
      if(r.offline){root.connected=false;root.error="";if(callback)callback(null,"offline");return}
      else if(!r.ok)root.error=r.error
      else root.error=""
      if(callback)callback(r.result,r.ok?null:r.error)
    }}
    stderr:SplitParser{onRead:function(line){root.error=line}}
    onExited:function(code){dialogs.closeDialog();root.ready=false;root.connected=false;root.refreshing=false;root.cancelWork();persist.stop();var all=root.callbacks;root.callbacks=({});for(var id in all)all[id](null,"Controller stopped");if(code!==0)root.error="Controller exited ("+code+")"}
  }
  Timer {id:dialogWarp;interval:200;property var lease:null;onTriggered:root.finishDialogBlink(lease)}
  Hud {service:root}
  Dialogs {id:dialogs;service:root;width:parent.width;height:parent.height}
}
