// Included after the native dialog/compiler definitions. All state below is
// owned by the Win32 message thread; HTTP workers only exchange request objects.
static unsigned long long g_controlSerial=0;
namespace native_control {
using control::Json;
using Clock = std::chrono::steady_clock;
struct Ticket {
    std::mutex mutex; std::condition_variable wake;
    bool done = false, cancelled = false;
    Clock::time_point expires = Clock::now() + std::chrono::seconds(15);
    std::string path; Json body, reply;
};
static std::mutex mutex;
static std::deque<std::shared_ptr<Ticket>> tickets;
static bool closing = false;
static HWND window = nullptr;
static std::unique_ptr<control::Server> server;
static Json run = nullptr, last = nullptr;
static std::string error;
static POINT origin = {};
static int task = 0, captureSlot = -1;
static bool captureSingleLine = false, manual = false, connected = false;
static std::future<Json> poll;
static int pollTask=0;
static Clock::time_point nextPoll;
constexpr UINT Request = WM_APP + 501;

static Json environment() {
    Json env = {{"script",WStringToUtf8(g_registryScript)},{"registry",WStringToUtf8(g_registryFilePath)},
        {"vars",Json::object()},{"buffers",Json::object()},{"zones",Json::object()},{"points",Json::array()},
        {"shells",{{"X",WStringToUtf8(g_registryPowerShellPath)},{"C",WStringToUtf8(g_registryCmdPath)},{"G",WStringToUtf8(g_regeditedExePath)},{"P",WStringToUtf8(g_registryPwshPath)},{"T",WStringToUtf8(g_registryTerminalPath)}}},
        {"paths",{{"import",WStringToUtf8(g_registryImportPath)},{"export",WStringToUtf8(g_registryExportPath)}}},
        {"view",{{"crosshair",g_crosshairVisible},{"grid",g_gridVisible},{"coordinates",g_coordinatesVisible},{"panel",g_coordPanelVisible},{"keys",g_keyDisplayVisible},{"coordinates_only",g_coordinatesOnlyMode}}}};
    for (int i = 0; i < 4; ++i) {
        env["vars"][std::string(1,"ASDF"[i])] = WStringToUtf8(g_registryVars[i]);
        auto key = std::string(1,"ZXCV"[i]); env["buffers"][key] = WStringToUtf8(g_pasteBuffers[i]);
        auto z = g_zoneBuffers[i]; env["zones"][key] = z.set ? Json::array({z.start.x,z.start.y,z.end.x,z.end.y}) : Json(nullptr);
    }
    for (int i = 0; i < 16; ++i) env["points"].push_back(g_registryPointSet[i] ? Json::array({g_registryPoints[i].x,g_registryPoints[i].y}) : Json(nullptr));
    return env;
}
static Json merge(Json env, const Json& patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.key() == "script" || it.key() == "registry") env[it.key()] = it.value();
        else if (it.key() == "points") for (auto p = it.value().begin(); p != it.value().end(); ++p) env["points"][std::stoi(p.key())-1] = p.value();
        else for (auto p = it.value().begin(); p != it.value().end(); ++p) env[it.key()][p.key()] = p.value();
    }
    control::require(env.dump().size() <= control::Limit, "Merged environment exceeds 1 MiB", 413);
    return env;
}
static void apply(const Json& env) {
    auto wide = [](const Json& v) { return Utf8ToWString(v.get<std::string>()); };
    g_registryScript = wide(env["script"]); g_registryFilePath = wide(env["registry"]);
    g_registryPowerShellPath = wide(env["shells"]["X"]); g_registryCmdPath = wide(env["shells"]["C"]); g_regeditedExePath = wide(env["shells"]["G"]);
    g_registryPwshPath = wide(env["shells"]["P"]); g_registryTerminalPath = wide(env["shells"]["T"]);
    g_registryImportPath = wide(env["paths"]["import"]); g_registryExportPath = wide(env["paths"]["export"]);
    auto view = env["view"]; g_crosshairVisible = view["crosshair"]; g_gridVisible = view["grid"]; g_coordinatesVisible = view["coordinates"];
    g_coordPanelVisible = view["panel"]; g_keyDisplayVisible = view["keys"]; g_coordinatesOnlyMode = view["coordinates_only"];
    for (int i = 0; i < 4; ++i) {
        g_registryVars[i] = wide(env["vars"][std::string(1,"ASDF"[i])]);
        g_pasteBuffers[i] = wide(env["buffers"][std::string(1,"ZXCV"[i])]);
        auto z = env["zones"][std::string(1,"ZXCV"[i])]; g_zoneBuffers[i] = {};
        if (!z.is_null()) g_zoneBuffers[i] = {true,{z[0].get<LONG>(),z[1].get<LONG>()},{z[2].get<LONG>(),z[3].get<LONG>()}};
    }
    for (int i = 0; i < 16; ++i) {
        auto p = env["points"][i]; g_registryPointSet[i] = !p.is_null();
        if (!p.is_null()) g_registryPoints[i] = {p[0].get<LONG>(),p[1].get<LONG>()};
    }
    if (g_hwndCrosshair) RefreshCrosshairOverlayVisibility();
    if (g_hwndCoordPanel) ShowWindow(g_hwndCoordPanel,g_coordPanelVisible ? SW_SHOW : SW_HIDE);
    if (g_hwndKeyDisplay) ShowWindow(g_hwndKeyDisplay,g_keyDisplayVisible ? SW_SHOW : SW_HIDE);
    g_fastLastCursor = {LONG_MIN,LONG_MIN};
}
static HWND dialog(int* field = nullptr, std::string* menu = nullptr, std::string* stage = nullptr) {
    HWND h = nullptr; int id = 0; std::string m, s;
    if (g_hwndSaveDlg) { h=g_hwndSaveDlg; id=IDC_EDIT_NAME; m="save_cursor"; s=g_saveCursorState.stage == SaveCursorStage::Name ? "Name" : "Command"; }
    else if (g_hwndRecordDlg) { h=g_hwndRecordDlg; id=g_recordKeysState.stage == RecordKeysStage::Paste ? IDC_EDIT_PASTE_TEXT : IDC_EDIT_SEQ_NAME; m="record_keys"; s=manual ? "Manual" : g_recordKeysState.stage == RecordKeysStage::Name ? "Name" : g_recordKeysState.stage == RecordKeysStage::Paste ? "Paste" : "Command"; }
    else if (g_hwndRegistryHubDlg) { h=g_hwndRegistryHubDlg; id=IDC_EDIT_REGISTRY_HUB_VALUE; m="registry_hub"; s=RegistryHubStageName(g_registryHubState.stage); }
    else if (g_hwndCircleDlg) { h=g_hwndCircleDlg; id=IDC_EDIT_CIRCLE_VALUE; m="circle_placer"; s=std::vector<std::string>{"Idle","Distance","Mode","Angle","Confirm","ZoneAction"}.at(static_cast<int>(g_circlePlacement.stage)); }
    else if (g_hwndPasteBuffersDlg) { h=g_hwndPasteBuffersDlg; id=IDC_EDIT_PASTE_BUFFER_VALUE; m="paste_buffers"; s=std::vector<std::string>{"Command","Edit","PasteSelect","PasteSequence","ZoneCommand","ZoneEdit","ZonePlaySelect","ZonePlayFlow","ZoneCaptureZoneSelect","ZoneCaptureFlow","ZoneCaptureTarget"}.at(static_cast<int>(g_pasteBufferState.stage)); }
    else if (g_hwndClickDlg) { h=g_hwndClickDlg; id=IDC_EDIT_CLICK_VALUE; m=control::Menus.at(4+g_clickActionState.buttonIndex); s=g_clickActionState.stage == ClickActionStage::Command ? "Command" : "HoldMs"; }
    else if (g_hwndViewTogglesDlg) { h=g_hwndViewTogglesDlg; id=IDC_EDIT_VIEW_VALUE; m="view_toggles"; s="Command"; }
    if (field) *field=id; if (menu) *menu=m; if (stage) *stage=s;
    return h && IsWindow(h) ? h : nullptr;
}
static Json snapshot() {
    int field; std::string menu,stage; HWND h=dialog(&field,&menu,&stage);
    Json d=nullptr;
    if (h) d={{"menu",menu},{"stage",stage},{"state",{{"text",WStringToUtf8(DialogText(h,field))},{"origin",{origin.x,origin.y}},{"capturing",g_waitingForKey},{"status",WStringToUtf8(g_registryHubState.status)}}}};
    if(h) {
        auto& state=d["state"];state["kind"]=menu;state["stage"]=stage;
        if(menu=="record_keys"){state["steps"]=g_recordedSteps;state["delay"]=g_recordKeysState.stepDelay;state["name"]=WStringToUtf8(g_recordKeysState.name);state["manual"]=manual;}
        if(menu=="save_cursor"){state["points"]=g_saveCursorState.coords;state["name"]=WStringToUtf8(g_saveCursorState.name);}
        if(menu=="paste_buffers"){state["slot"]=g_pasteBufferState.activeSlot;state["zoneSlot"]=g_pasteBufferState.activeZoneSlot;state["flow"]=g_pasteBufferState.activeZoneFlow;}
        if(menu=="registry_hub"){state["variable"]=g_registryHubState.activeVar;state["point"]=g_registryHubState.activePoint;state["slot"]=g_registryHubState.activePasteSlot;state["zoneSlot"]=g_registryHubState.activeZoneSlot;}
        if(menu=="circle_placer"){state["radius"]=g_circlePlacement.radius;state["target"]=g_circlePlacement.targetValid?Json::array({g_circlePlacement.target.x,g_circlePlacement.target.y}):Json(nullptr);state["zoneStart"]={g_circlePlacement.zoneStart.x,g_circlePlacement.zoneStart.y};state["zoneMode"]=g_circlePlacement.zoneMode;}
    }
    return {{"api_version",1},{"ready",true},{"connected",connected},{"busy",!run.is_null()||h!=nullptr||task>0||RegistryDispatchActive()},{"sequence",RegistryDispatchStatus()},{"run",run.is_null()?Json(nullptr):run["id"]},
        {"last_run",last},{"environment",environment()},{"dialog",d},{"last_task",{{"id",g_lastTasketTaskNumber},{"state",WStringToUtf8(g_lastTasketTaskState)},{"message",WStringToUtf8(g_lastTasketTaskMessage)}}},{"error",error.empty()?Json(nullptr):Json(error)}};
}
static void closeDialogs() {
    if(g_hwndRegistryHubDlg)CloseRegistryHubDialog(g_hwndRegistryHubDlg);
    if(g_hwndSaveDlg)CloseSaveCursorDialog(g_hwndSaveDlg);
    if(g_hwndRecordDlg)CloseRecordKeysDialog(g_hwndRecordDlg);
    if(g_hwndCircleDlg)ResetCirclePlacement(true);
    if(g_hwndPasteBuffersDlg)ClosePasteBuffersDialog(g_hwndPasteBuffersDlg);
    if(g_hwndClickDlg)CloseClickActionDialog(g_hwndClickDlg);
    if(g_hwndViewTogglesDlg)CloseViewTogglesDialog(g_hwndViewTogglesDlg);
    manual=false;
}
static void finish(const std::string& why = "") {
    if(run.is_null())return;
    auto saved=run["saved"]; bool temporary=run["no_replace"];
    last={{"id",run["id"]},{"no_replace",temporary},{"environment",environment()},{"error",why.empty()?Json(nullptr):Json(why)}};
    run=nullptr; task=0; captureSlot=-1; manual=false;g_controlBufferReturns=0;
    if(temporary)apply(saved);
    g_controlTemporary=false;
    if(!temporary)SaveRegistryHubState();
}
static void submit(const std::string& text) {
    int field; HWND h=dialog(&field);
    control::require(h!=nullptr,"Choose a menu");
    if(manual) {
        if(text.empty()) { control::require(!g_currentStepKeys.empty(),"Choose keys for the step"); g_recordedSteps.push_back(g_currentStepKeys); AddRecordStepToList(h,g_currentStepKeys); g_currentStepKeys.clear(); manual=false; }
        else g_currentStepKeys.push_back(text);
        return;
    }
    // The API already includes Enter; suppress native EN_CHANGE shortcuts so
    // setting a one-letter command cannot execute it twice.
    SendDlgItemMessageW(h,field,EM_SETLIMITTEXT,control::Limit,0);
    SetDlgItemTextW(h,field,Utf8ToWString(text).c_str());
    if(h==g_hwndRecordDlg && g_recordKeysState.stage==RecordKeysStage::Command && (CompactLower(Utf8ToWString(text))=="x" || CompactLower(Utf8ToWString(text))=="manual")) { manual=true;g_waitingForKey=false;g_currentStepKeys.clear();return; }
    SendMessageW(h,WM_COMMAND,IDOK,0);
}
static void open(int menu) {
    if(menu==8)StartViewToggles();
    else if(menu==7){ControlStop();return;}
    else {
        const int hotkeys[]={IDH_SAVE_CURSOR,IDH_PASTE_BUFFERS,IDH_REGISTRY_HUB,IDH_CIRCLE_PLACER,IDH_CLICK_LEFT,IDH_CLICK_RIGHT,IDH_CLICK_MIDDLE,IDH_STOP_ALL_TASKET,IDH_VIEW_TOGGLES,IDH_RECORD_KEYS};
        SendMessageW(g_hwndCrosshair,WM_HOTKEY,hotkeys[menu],0);
    }
    if(g_hwndSaveDlg){g_savedCursorPos=origin;g_saveCursorState.coords={{0,1000,origin.x,origin.y}};}
    if(g_hwndCircleDlg){g_circlePlacement.origin=origin;g_circlePlacement.target=origin;}
    control::require(dialog()!=nullptr,"Could not open native menu",500);
}
static void action(const Json& input) {
    if(input.is_string()){submit(input.get<std::string>());return;}
    auto name=input.at("action").get<std::string>(); auto value=input.value("value","");
    int field; std::string menu,stage; HWND h=dialog(&field,&menu,&stage);
    control::require(h!=nullptr,"Choose a menu");
    if(name=="cancel"){closeDialogs();return;}
    if(name=="submit"||name=="accept"){submit(name=="accept"?WStringToUtf8(DialogText(h,field)):value);return;}
    if(name=="add_point"||name=="save_click") {
        control::require(menu=="save_cursor","Save Cursor required");
        if(name=="add_point")g_saveCursorState.coords.push_back({0,1000,origin.x,origin.y});
        else {if(stage=="Name")submit(input.contains("value")?value:WStringToUtf8(DialogText(h,field)));submit("c");} return;
    }
    if(name=="add_step"||name=="capture"||name=="manual"||name=="add_paste"||name=="replay") {
        control::require(menu=="record_keys","Record Keys required");
        if(stage=="Name")submit(input.contains("value")?value:WStringToUtf8(DialogText(h,field)));
        control::require(g_recordKeysState.stage==RecordKeysStage::Command,"Finish the current key-recording input first");
        if(name=="add_step") {
            control::require(input.contains("keys"),"Choose keys for the step");
            auto keys=input["keys"].get<std::vector<std::string>>();g_recordedSteps.push_back(keys);AddRecordStepToList(h,keys);g_waitingForKey=false;manual=false;
            if(input.contains("delay"))g_recordKeysState.stepDelay=input["delay"];
        } else if(name=="replay") {
            FlushCurrentRecordStep(h);control::require(!g_recordedSteps.empty()||!g_recordExtraActions.empty(),"No recorded steps");
            std::vector<std::string> actions;
            actions.push_back(BuildCursorMovementActionJson("macrohelp_record_origin",{{0,0,origin.x,origin.y}}));
            if(!g_recordedSteps.empty())actions.push_back(BuildKeysSequenceActionJson("macrohelp_record_replay",g_recordedSteps,g_recordKeysState.stepDelay));
            actions.insert(actions.end(),g_recordExtraActions.begin(),g_recordExtraActions.end());
            std::wstring status;control::require(ScheduleTasketTempTask("macrohelp_record_replay",BuildTasketDocumentJson("Replay recorded keys",actions,1),0,&status),WStringToUtf8(status),503);CloseRecordKeysDialog(h);
        } else submit(name=="capture"?"z":name=="manual"?"x":"c"); return;
    }
    if(name=="paste"||name=="paste_enter") {
        control::require(menu=="paste_buffers"&&g_pasteBufferState.stage==PasteBufferStage::Edit,"Open a paste-buffer editor first");
        if(input.contains("value"))SetDlgItemTextW(h,field,Utf8ToWString(value).c_str());
        // Native editor buttons follow the same task path as interactive use.
        std::wstring text=DialogText(h,field);int slot=g_pasteBufferState.activeSlot;g_pasteBuffers[slot]=text;
        std::vector<std::string> actions={BuildCursorMovementActionJson("macrohelp_paste_origin",{{0,0,origin.x,origin.y}}),BuildPasteActionJson("macrohelp_buffer",WStringToUtf8(text))};
        if(name=="paste_enter")actions.push_back(BuildKeysSequenceActionJson("macrohelp_buffer_enter",{{"ENTER"}},200,100));
        std::wstring status;bool accepted=ScheduleTasketTempTask("macrohelp_buffer",BuildTasketDocumentJson("Paste buffer",actions,1),0,&status);control::require(accepted,WStringToUtf8(status),503);ClosePasteBuffersDialog(h);return;
    }
    control::require(menu=="registry_hub","Registry Hub required");
    control::require(g_registryHubState.stage==RegistryHubStage::Command||g_registryHubState.stage==RegistryHubStage::Script,"Finish the current Registry Hub input first");
    if(g_registryHubState.stage==RegistryHubStage::Script)g_registryScript=DialogText(h,field);
    std::wstring status; bool ok=false;
    if(name=="clear"){ClearRegistryHubState();g_registryHubState.stage=RegistryHubStage::Command;UpdateRegistryHubDialogText(h);return;}
    if(name=="play")ok=ScheduleTasketRegistryHubScript(&status);
    else if(name=="export")ok=ExportRegistryHubEnvironment(&status);
    else if(name=="import")ok=ImportRegistryHubEnvironment(&status);
    else if(name=="save_schedule") {
        control::require(!control::trim(value).empty(),"Schedule name required");
        std::string compiled;int count=0;bool compiledOk=CompileRegistryHubScriptToTaskJson(&compiled,&count,&status);
        control::require(compiledOk,WStringToUtf8(status));
        ok=ScheduleTasketTempTask(value,compiled,0,&status,false,false);
    } else throw control::Error(400,"Unknown input action");
    control::require(ok,WStringToUtf8(status),503);CloseRegistryHubDialog(h);
}
static Json handle(const std::string& path,const Json& body) {
    if(path=="/v1/help")return control::success({{"platform","windows"},{"topic","registry_hub"},{"text",WStringToUtf8(RegistryHubHelpText())}});
    if(path=="/v1/coords"){
        POINT point={};control::require(GetCursorPos(&point)!=0,"Cursor position is unavailable",503);
        return control::success({{"x",point.x},{"y",point.y}});
    }
    if(path=="/v1/health"){
        std::string response;std::wstring status;connected=HttpGetLocalTasket(L"/health",&response,&status);
        return control::success({{"api_version",1},{"service","macrohelp-overlay"},{"ready",true},{"backend","tasket"},{"connected",connected}});
    }
    if(path=="/v1/state")return control::success(snapshot());
    if(path=="/v1/stop"){ControlStop();return control::success({{"stop_requested",true},{"state",snapshot()}});}
    if(body.contains("menu")&&body["menu"]==7){ControlStop();return control::success(snapshot());}
    control::require(run.is_null()||body.value("run","")==run["id"],"Another control run is active",409);
    control::require(!run.is_null()||!body.contains("run"),"Control run is no longer active",404);
    control::require(!RegistryDispatchActive()&&task==0&&!g_waitingForKey&&(!dialog()||IsWindowEnabled(dialog()))&&(ControlActive()||dialog()==nullptr),"Overlay is busy; inspect state or stop first",409);
    if(!run.is_null()&&body.contains("no_replace"))control::require(body["no_replace"]==run["no_replace"],"no_replace is fixed for the active run",409);
    auto before=environment();auto merged=merge(before,body.at("config"));
    if(run.is_null()) {
        // Query only at admission. Never infer ownership from another task's ID.
        std::string response;std::wstring status;
        if(HttpGetLocalTasket(L"/status",&response,&status)) {
            connected=true;auto all=Json::parse(response);auto data=all.value("data",all);
            control::require(data.value("running_count",0)==0&&data.value("scheduled_count",0)==0,"Backend has active work; inspect or stop first",409);
        } else connected=false;
        run={{"id",std::to_string(GetTickCount64())+"-"+std::to_string(++g_controlSerial)},{"no_replace",body.value("no_replace",false)},{"saved",before}};
        GetCursorPos(&origin);
    }
    if(body.contains("origin"))origin={body["origin"][0].get<LONG>(),body["origin"][1].get<LONG>()};
    g_controlTemporary=run["no_replace"];g_controlFeeding=true;g_controlNativeError.clear();error.clear();
    try {
        if(body.contains("menu"))closeDialogs();
        apply(merged);
        if(body.contains("menu"))open(body["menu"].get<int>());
        else if(dialog()&&!body["config"].empty()) {
            auto patch=body["config"];bool refresh=false;
            if(g_hwndRegistryHubDlg) {
                auto stage=g_registryHubState.stage;
                refresh=(stage==RegistryHubStage::Script&&patch.contains("script"))||(stage==RegistryHubStage::RegistryPath&&patch.contains("registry"));
                auto contains=[&](const char* section,std::string key){return patch.contains(section)&&patch[section].contains(key);};
                if(stage==RegistryHubStage::VarEdit)refresh=contains("vars",std::string(1,static_cast<char>(RegistryVarKey(g_registryHubState.activeVar))));
                if(stage==RegistryHubStage::PointEdit)refresh=contains("points",std::to_string(g_registryHubState.activePoint+1));
                if(stage==RegistryHubStage::ZoneEdit)refresh=contains("zones",std::string(1,static_cast<char>(PasteBufferSlotKey(g_registryHubState.activeZoneSlot))));
                if(stage==RegistryHubStage::PasteBufferEdit)refresh=contains("buffers",std::string(1,static_cast<char>(PasteBufferSlotKey(g_registryHubState.activePasteSlot))));
                if(stage==RegistryHubStage::ExportPath||stage==RegistryHubStage::ImportPath)refresh=contains("paths",stage==RegistryHubStage::ExportPath?"export":"import");
                if(stage==RegistryHubStage::PowerShellPath||stage==RegistryHubStage::CmdPath||stage==RegistryHubStage::RegeditedPath)refresh=contains("shells",stage==RegistryHubStage::PowerShellPath?"X":stage==RegistryHubStage::CmdPath?"C":"G");
                if(refresh)UpdateRegistryHubDialogText(g_hwndRegistryHubDlg);
            }
            if(g_hwndPasteBuffersDlg&&g_pasteBufferState.stage==PasteBufferStage::Edit&&patch.contains("buffers")&&patch["buffers"].contains(std::string(1,static_cast<char>(PasteBufferSlotKey(g_pasteBufferState.activeSlot)))))
                SetDlgItemTextW(g_hwndPasteBuffersDlg,IDC_EDIT_PASTE_BUFFER_VALUE,g_pasteBuffers[g_pasteBufferState.activeSlot].c_str());
        }
        for(const auto& input:body["inputs"]) {
            control::require(task==0&&!RegistryDispatchActive(),"The preceding input started a task; wait for completion");
            action(input);
            control::require(g_controlNativeError.empty(),WStringToUtf8(g_controlNativeError));
        }
        g_controlFeeding=false;
        if(!g_controlTemporary)SaveRegistryHubState();
        if(!dialog()&&task==0&&!RegistryDispatchActive())finish();
        return control::success(snapshot());
    } catch(const std::exception& e) {
        g_controlFeeding=false;error=e.what();closeDialogs();
        // A task already accepted by the backend retains ownership until it
        // finishes. Never restore its temporary context while it is running.
        if(task==0&&!RegistryDispatchActive())finish(error); else run["failure"]=error;
        throw;
    }
}
static void tick() {
    if(run.is_null())return;
    if(task&&Clock::now()>=nextPoll) {
        if(!poll.valid()) {
            int id=task;
            pollTask=id;
            poll=std::async(std::launch::async,[id]{std::string data;std::wstring status;if(!QueryTasketTaskStatus(id,data,&status))return Json{{"poll_error",WStringToUtf8(status)}};try{return Json::parse(data);}catch(...){return Json{{"poll_error","Invalid backend status"}};}});
        } else if(poll.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            Json reply=poll.get();nextPoll=Clock::now()+std::chrono::milliseconds(400);
            if(pollTask!=task)return;
            if(reply.contains("poll_error")){connected=false;error=reply["poll_error"].get<std::string>();return;}
            connected=true;auto data=reply.value("data",reply);auto state=data.value("state","");
            g_lastTasketTaskState=Utf8ToWString(state);g_lastTasketTaskMessage=Utf8ToWString(data.value("message",""));
            if(state=="finished"||state=="stopped"||state=="failed") {
                if(state!="finished")error=state+": "+data.value("message","");
                if(state=="finished"&&captureSlot>=0) {
                    if(OpenClipboard(window)) {
                        HANDLE h=GetClipboardData(CF_UNICODETEXT);const wchar_t* text=h?static_cast<const wchar_t*>(GlobalLock(h)):nullptr;
                        if(text){std::wstring v=text;if(captureSingleLine)for(auto& c:v)if(c==L'\r'||c==L'\n')c=L' ';g_pasteBuffers[captureSlot]=v;GlobalUnlock(h);}else error="Clipboard does not contain text";
                        CloseClipboard();
                    }else error="Clipboard is unavailable";
                }
                task=0;captureSlot=-1;
            }
        }
    }
    if(!task&&!dialog()&&!RegistryDispatchActive())finish(error.empty()?(!g_controlNativeError.empty()?WStringToUtf8(g_controlNativeError):run.value("failure","")):error);
}
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==Request) {
        std::shared_ptr<Ticket> t;
        {std::lock_guard<std::mutex> lock(mutex);if(tickets.empty())return 0;t=tickets.front();tickets.pop_front();}
        std::unique_lock<std::mutex> lock(t->mutex);
        if(t->cancelled||Clock::now()>t->expires)return 0;
        lock.unlock();Json reply;
        try{reply=handle(t->path,t->body);}catch(const control::Error& e){reply=control::failure(e.status,e.what());}catch(const std::exception& e){reply=control::failure(500,e.what());}
        lock.lock();t->reply=reply;t->done=true;t->wake.notify_all();return 0;
    }
    if(message==WM_TIMER){tick();return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
static Json dispatch(const std::string& path,const Json& body) {
    auto t=std::make_shared<Ticket>();t->path=path;t->body=body;
    {std::lock_guard<std::mutex> lock(mutex);control::require(!closing,"Overlay is stopping",503);control::require(tickets.size()<8,"Control queue is full",429);tickets.push_back(t);PostMessageW(window,Request,0,0);}
    std::unique_lock<std::mutex> lock(t->mutex);
    if(!t->wake.wait_until(lock,t->expires,[&]{return t->done;})){t->cancelled=true;throw control::Error(504,"Overlay control timed out; inspect state before retrying");}
    return t->reply;
}
}
static bool ControlActive(){return !native_control::run.is_null();}
static int ControlMessageBox(HWND hwnd,LPCWSTR text,LPCWSTR title,UINT flags) {
    if(g_controlFeeding){g_controlNativeError=text?text:L"Native action failed";return IDCANCEL;}
    return (MessageBoxW)(hwnd,text,title,flags);
}
static void ControlTaskScheduled(int id){if(ControlActive()&&!RegistryDispatchActive()){native_control::task=id;native_control::nextPoll=native_control::Clock::now();}}
static void ControlRetainTask(int id){if(ControlActive()&&id>0){native_control::task=id;native_control::nextPoll=native_control::Clock::now();}}
static void ControlCaptureBuffer(int slot,bool single){native_control::captureSlot=slot;native_control::captureSingleLine=single;}
static std::string ControlPrepareTask(const std::string& document){
    auto task=control::Json::parse(document);
    auto p=native_control::origin;
    auto move=control::Json::parse(StripTrailingActionComma(BuildCursorMovementActionJson("macrohelp_control_origin",{{0,0,p.x,p.y}})));
    task.at("actions").insert(task["actions"].begin(),move);
    return task.dump();
}
static void ControlStop(){
    std::wstring status;bool stopped=StopAllTasketTasks(&status);
    if(ControlActive()) {
        native_control::closeDialogs();
        // If stop failed while a task is active, keep the context until polling
        // confirms completion; a disconnected daemon is not a successful stop.
        if(stopped||!native_control::task)native_control::finish("cancelled");
        else native_control::error=WStringToUtf8(status);
    }
}
static void StartControlServer() {
    try {
        LoadRegistryHubState();native_control::apply(native_control::environment());auto settings=control::Settings::load();if(!settings.enabled)return;
        WNDCLASSW wc={};wc.hInstance=g_hInst;wc.lpfnWndProc=native_control::procedure;wc.lpszClassName=L"MacrohelpOverlayControl";RegisterClassW(&wc);
        native_control::window=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,g_hInst,nullptr);
        control::require(native_control::window!=nullptr,"Cannot create control dispatcher",500);
        SetTimer(native_control::window,1,100,nullptr);
        native_control::server=std::make_unique<control::Server>(settings,native_control::dispatch);
    }catch(const std::exception& e){OutputDebugStringA((std::string("Macrohelp control: ")+e.what()+"\n").c_str());}
}
static void StopControlServer() {
    using namespace native_control;
    {std::lock_guard<std::mutex> lock(mutex);closing=true;for(auto t:tickets){std::lock_guard<std::mutex> ticketLock(t->mutex);t->reply=control::failure(503,"Overlay is stopping");t->done=true;t->wake.notify_all();}tickets.clear();}
    server.reset();if(ControlActive())ControlStop();
    if(poll.valid())poll.wait();
    if(window){KillTimer(window,1);DestroyWindow(window);window=nullptr;}
}
