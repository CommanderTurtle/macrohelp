// Registry Hub continuation on the UI thread. Only explicit user waits split
// native tasks; waits emitted inside shell/capture helpers remain untouched.
struct RegistryHubDispatch {
    std::vector<RegistrySourceToken> tokens;
    size_t expandedBytes=0;
    std::vector<RegistryHubBranchFrame> branches;
    size_t at=0;
    int index=1,total=1,task=0;
    bool leadingWait=false,focusClick=false,active=false;
    std::string state="idle",error;
};
static RegistryHubDispatch g_registryDispatch;
static bool g_registryDispatchSubmitting=false;
static unsigned long long g_registryDispatchGeneration=0;
static std::future<control::Json> g_registryDispatchPoll;
static unsigned long long g_registryDispatchPollGeneration=0;
static int g_registryDispatchPollTask=0;
static auto g_registryDispatchNextPoll=std::chrono::steady_clock::now();

static bool RegistryWaitToken(const std::string& token) {
    auto text=LowerAscii(TrimAscii(token));
    return text.size()>4&&text.compare(0,4,"wait")==0&&isspace(static_cast<unsigned char>(text[4]));
}
static RegistryHubDispatch PrepareRegistryDispatch(const std::wstring& script) {
    RegistryHubDispatch result;auto tokens=ExtractRegistryHubTokens(script);
    result.expandedBytes=WStringToUtf8(script).size();
    control::require(result.expandedBytes<=1024*1024,"Workflow exceeds 1 MiB");
    for(const auto& token:tokens)result.tokens.push_back({token,"",{}});
    control::require(!result.tokens.empty(),"Registry Hub script has no {...} tokens.");
    ValidateRegistryStructure(tokens);
    for(const auto& token:tokens)if(RegistryWaitToken(token))++result.total;
    return result;
}
static bool CompileRegistryDispatchSegment(RegistryHubDispatch& cursor,std::string* document,std::wstring* status,bool splitWaits=true) {
    std::vector<std::string> actions;
    bool leading=cursor.leadingWait;cursor.leadingWait=false;
    while(cursor.at<cursor.tokens.size()) {
        const auto origin=cursor.tokens[cursor.at++];const auto& token=origin.text;auto lower=LowerAscii(TrimAscii(token));
        if(lower.rfind("if ",0)==0) {
            control::require(cursor.branches.size()<16,"Conditions nested too deeply");
            bool parent=RegistryHubBranchesActive(cursor.branches),condition=false;
            if(parent&&!EvaluateRegistryHubCondition(TrimAscii(token).substr(3),condition,status)){
                if(status&&!origin.source.empty())*status=Utf8ToWString("rgd "+origin.source+": ")+*status;
                return false;
            }
            cursor.branches.push_back({parent,condition,false});continue;
        }
        if(lower=="else"){cursor.branches.back().elseMode=true;continue;}
        if(lower=="endif"||lower=="end if"){cursor.branches.pop_back();continue;}
        if(!RegistryHubBranchesActive(cursor.branches)){if(RegistryWaitToken(token))--cursor.total;continue;}
        if(lower=="rgd"||(lower.size()>3&&lower.substr(0,3)=="rgd"&&isspace(static_cast<unsigned char>(lower[3])))) {
            auto included=RegistryRgdTokens(origin,cursor.expandedBytes);
            for(const auto& child:included)if(RegistryWaitToken(child.text))++cursor.total;
            cursor.tokens.insert(cursor.tokens.begin()+cursor.at,included.begin(),included.end());continue;
        }
        if(splitWaits&&RegistryWaitToken(token)) {
            if(!leading){--cursor.at;cursor.leadingWait=true;break;}
            leading=false;
        }
        auto input=RegistryWaitToken(token)?"wait "+TrimAscii(token.substr(4)):token;
        if(!CompileRegistryHubToken(input,actions,cursor.focusClick,status)){
            if(status&&!origin.source.empty())*status=Utf8ToWString("rgd "+origin.source+": ")+*status;
            return false;
        }
    }
    *document=actions.empty()?"":BuildTasketDocumentJson(splitWaits?"Macrohelp Registry Hub sequence "+std::to_string(cursor.index)+" of "+std::to_string(cursor.total):"Macrohelp Registry Hub compiled native Tasket flow.",actions,cursor.index==1?1:0);
    return true;
}
static bool RegistryDispatchActive(){return g_registryDispatch.active;}
static bool RegistryDispatchNeedsOrigin(){return !RegistryDispatchActive()||(g_registryDispatchSubmitting&&g_registryDispatch.index==1);}
static control::Json RegistryDispatchStatus(){
    if(g_registryDispatch.state=="idle")return nullptr;
    return {{"index",g_registryDispatch.index},{"total",g_registryDispatch.total},{"state",g_registryDispatch.state},{"task",g_registryDispatch.task},{"error",g_registryDispatch.error}};
}
static void RegistryDispatchMessage(const std::string& state) {
    auto text=std::to_string(g_registryDispatch.index)+"/"+std::to_string(g_registryDispatch.total)+" sequence is "+state;
    g_lastTasketTaskMessage=Utf8ToWString(text);
    OutputDebugStringA(("Macrohelp: "+text+"\n").c_str());
    std::clog<<"Macrohelp: "<<text<<'\n';
}
static void FinishRegistryDispatch(const std::string& error="") {
    g_registryDispatch.active=false;g_registryDispatch.task=0;
    g_registryDispatch.error=error;g_registryDispatch.state=error.empty()?"finished":"failed";
    if(!error.empty()&&ControlActive())g_controlNativeError=Utf8ToWString(error);
    if(g_hwndCrosshair)KillTimer(g_hwndCrosshair,4710);
    RegistryDispatchMessage(g_registryDispatch.state);
}
static void CancelRegistryDispatch(){
    CancelRegistryRgd();
    if(!RegistryDispatchActive())return;
    ControlRetainTask(g_registryDispatch.task);
    ++g_registryDispatchGeneration;g_registryDispatch.active=false;g_registryDispatch.task=0;g_registryDispatch.state="stopped";
    if(g_hwndCrosshair)KillTimer(g_hwndCrosshair,4710);
    RegistryDispatchMessage("stopped");
}
static bool SubmitRegistryDispatch(std::wstring* status) {
    try {
        while(RegistryDispatchActive()&&!g_registryDispatch.task) {
            std::string document;
            if(!CompileRegistryDispatchSegment(g_registryDispatch,&document,status)){FinishRegistryDispatch(status?WStringToUtf8(*status):"Compilation failed");return false;}
            SaveRegistryHubState();
            if(document.empty()) {
                if(g_registryDispatch.at==g_registryDispatch.tokens.size()){FinishRegistryDispatch();return true;}
                ++g_registryDispatch.index;continue;
            }
            auto name="Macrohelp sequence "+std::to_string(g_registryDispatch.index)+" of "+std::to_string(g_registryDispatch.total);
            g_registryDispatchSubmitting=true;
            bool ok=ScheduleTasketTempTask(name,document,g_registryDispatch.index==1?1:0,status);
            g_registryDispatchSubmitting=false;
            if(!ok){FinishRegistryDispatch(status?WStringToUtf8(*status):"Task submission failed");return false;}
            g_registryDispatch.task=g_lastTasketTaskNumber;g_registryDispatchNextPoll=std::chrono::steady_clock::now();
            g_registryDispatch.state="running";g_lastTasketTaskState=L"scheduled";RegistryDispatchMessage("running");
            if(g_hwndCrosshair)SetTimer(g_hwndCrosshair,4710,100,nullptr);
        }
        return true;
    }catch(const std::exception& e){g_registryDispatchSubmitting=false;if(status)*status=Utf8ToWString(e.what());if(g_registryDispatch.state!="stopped")FinishRegistryDispatch(e.what());return false;}
}
static void TickRegistryDispatch() {
    using namespace std::chrono;
    if(g_registryDispatchPoll.valid()) {
        if(g_registryDispatchPoll.wait_for(milliseconds(0))!=std::future_status::ready)return;
        auto reply=g_registryDispatchPoll.get();
        if(!RegistryDispatchActive()||g_registryDispatchPollGeneration!=g_registryDispatchGeneration||g_registryDispatchPollTask!=g_registryDispatch.task)return;
        g_registryDispatchNextPoll=steady_clock::now()+milliseconds(120);
        if(reply.contains("poll_error")){g_registryDispatch.error=reply["poll_error"];return;}
        auto data=reply.value("data",reply);auto state=data.value("state","");g_registryDispatch.error.clear();
        if(g_lastTasketTaskNumber==g_registryDispatch.task)g_lastTasketTaskState=Utf8ToWString(state);
        if(state=="finished") {
            g_registryDispatch.task=0;
            if(g_registryDispatch.at==g_registryDispatch.tokens.size()){FinishRegistryDispatch();return;}
            ++g_registryDispatch.index;std::wstring status;SubmitRegistryDispatch(&status);return;
        }
        if(state=="failed"||state=="stopped"){FinishRegistryDispatch(state+": "+data.value("message",""));return;}
    }
    if(!RegistryDispatchActive()||!g_registryDispatch.task||steady_clock::now()<g_registryDispatchNextPoll)return;
    int task=g_registryDispatch.task;g_registryDispatchPollTask=task;g_registryDispatchPollGeneration=g_registryDispatchGeneration;
    g_registryDispatchPoll=std::async(std::launch::async,[task]{
        std::string response;std::wstring status;
        if(!HttpGetLocalTasket(L"/check?id="+std::to_wstring(task),&response,&status))return control::Json{{"poll_error",WStringToUtf8(status)}};
        try{return control::Json::parse(response);}catch(const std::exception& e){return control::Json{{"poll_error",e.what()}};}
    });
}
static bool ScheduleTasketRegistryHubScript(std::wstring* status) {
    if(RegistryDispatchActive()||g_registryRgdReading){if(status)*status=L"A Registry Hub sequence is already running.";return false;}
    try {
        auto cursor=PrepareRegistryDispatch(g_registryScript);
        ++g_registryDispatchGeneration;g_registryDispatch=std::move(cursor);g_registryDispatch.active=true;
        return SubmitRegistryDispatch(status);
    }catch(const std::exception& e){if(status)*status=Utf8ToWString(e.what());if(g_controlFeeding)g_controlNativeError=Utf8ToWString(e.what());return false;}
}
