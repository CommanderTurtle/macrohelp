// Exercise the real native state adapter without starting the overlay, hooks,
// windows, executing macros, or reading/writing a user's config.
#include "../src/main.cpp"
#include <iostream>
#include <filesystem>
namespace control::detail {std::string privateKeyFile(const std::filesystem::path&);}

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    using namespace control;
    try {
        if(argc==2&&std::string(argv[1])=="--schema"){std::cout<<schema().dump()<<'\n';return 0;}
        if(argc==2&&std::string(argv[1])=="--normalize") {
            std::string line;while(std::getline(std::cin,line)) {
                try{std::cout<<success(normalize(parse(line))).dump()<<'\n';}
                catch(const Error& e){std::cout<<failure(e.status,e.what()).dump()<<'\n';}
            }return 0;
        }
        check(argc==2,"Supply the shared fixture path");
        std::ifstream file(argv[1]);Json fixtures;file>>fixtures;
        for(const auto& test:fixtures) {
            bool valid=true;try{normalize(test.at("input"));}catch(const Error&){valid=false;}
            check(valid==test["valid"].get<bool>(),"Protocol fixture failed");
        }
        for(int i=0;i<10;++i)check(normalize({{"menu",Menus[i]}})["menu"]==i,"Menu mapping");
        auto before=native_control::environment();
        {
            std::wstring status;std::string document;
            auto cursor=PrepareRegistryDispatch(L"{type first}{wait 25}{point 3}{buf A}{wait 0}");
            check(cursor.total==3,"Explicit wait count");
            check(CompileRegistryDispatchSegment(cursor,&document,&status),"Initial segment compile");
            check(Json::parse(document)["actions"].size()==1&&cursor.leadingWait,"Future point expanded before dispatch");
            check(ApplySavedCursorVariable("point 3",{-12,345}),"Save Cursor point variable");
            check(ApplySavedCursorVariable("A",{700,800}),"Save Cursor text variable");
            ++cursor.index;
            check(CompileRegistryDispatchSegment(cursor,&document,&status),"Live segment compile");
            auto actions=Json::parse(document)["actions"];
            check(actions[0]==Json({{"type","wait"},{"duration",.025}}),"Next task does not start with literal wait");
            check(actions[1]["cursormovsmap"][0][2]==-12&&actions[1]["cursormovsmap"][0][3]==345,"Next task used stale point");
            check(std::any_of(actions.begin(),actions.end(),[](const Json& action){return action.value("content","")=="700,800";}),"Next task used stale variable");
            ++cursor.index;check(CompileRegistryDispatchSegment(cursor,&document,&status),"Trailing wait compile");
            check(Json::parse(document)["actions"][0]==Json({{"type","wait"},{"duration",0}})&&cursor.at==cursor.tokens.size(),"Trailing zero wait lost");
            g_registryVars[0]=L"yes";
            cursor=PrepareRegistryDispatch(L"{if A == yes}{click on}{wait 4}{type kept}{else}{type wrong}{endif}");
            check(CompileRegistryDispatchSegment(cursor,&document,&status)&&document.empty(),"Branch continuation initial segment");
            g_registryVars[0]=L"no";++cursor.index;
            check(CompileRegistryDispatchSegment(cursor,&document,&status),"Branch continuation compile");
            check(cursor.focusClick&&document.find("wrong")==std::string::npos,"Branch or focus mode reevaluated");
            native_control::apply(before);
            g_controlTemporary=true;native_control::run={{"id","dispatch-test"},{"no_replace",true},{"saved",before}};
            native_control::apply(native_control::merge(before,{{"vars",{{"A","temporary"}}}}));
            g_registryDispatch=PrepareRegistryDispatch(L"{wait 1}");g_registryDispatch.active=true;
            g_registryDispatchSubmitting=true;check(RegistryDispatchNeedsOrigin(),"Initial control origin lost");
            g_registryDispatch.index=2;check(!RegistryDispatchNeedsOrigin(),"Origin would precede the next task's wait");g_registryDispatchSubmitting=false;
            native_control::tick();check(ControlActive()&&g_controlTemporary,"Temporary config released before sequence ended");
            g_registryDispatch.task=123;
            auto generation=g_registryDispatchGeneration;CancelRegistryDispatch();
            check(!RegistryDispatchActive()&&g_registryDispatchGeneration>generation,"Stop did not invalidate pending dispatch");
            check(native_control::task==123&&g_controlTemporary,"Unconfirmed stop released running task context");
            native_control::task=0; // Simulate confirmed task completion, without a daemon call.
            native_control::tick();check(!ControlActive()&&native_control::environment()==before,"Finished sequence did not restore temporary config");
            g_registryDispatch=RegistryHubDispatch{};
        }
        auto stateBeforeHelp=native_control::snapshot();
        auto help=native_control::handle("/v1/help",Json::object());
        check(help["result"]["text"]==WStringToUtf8(RegistryHubHelpText()),"Help differs from native manual");
        check(help["result"]["platform"]=="windows"&&native_control::snapshot()==stateBeforeHelp,"Help mutated state");
        auto changed=native_control::merge(before,{{"vars",{{"A","temporary"}}},{"points",{{"16",{100,200}}}},{"zones",{{"C",{1,2,3,4}}}}});
        check(changed["vars"]["S"]==before["vars"]["S"],"Omitted fields lost");
        g_controlTemporary=true;native_control::run={{"id","test"},{"no_replace",true},{"saved",before}};
        native_control::apply(changed);
        check(native_control::environment()==changed,"Native config mapping");
        native_control::finish();
        check(native_control::environment()==before&&!g_controlTemporary,"Temporary config not restored");
        check(native_control::last["environment"]["vars"]["A"]=="temporary","Temporary result lost");
        native_control::origin={100,200};
        auto task=Json::parse(ControlPrepareTask(BuildTasketDocumentJson("test",{BuildWaitActionJson(.1)},0)));
        check(task["actions"][0]["cursormovsmap"][0]==Json::array({0,0,100,200}),"Origin not restored before backend input");
        Settings settings;settings.port=0;settings.key="native-control-test-key-0123456789";
        Server server(settings,[](const std::string& path,const Json& request){if(path=="/v1/control"&&request.value("run","")=="busy")return failure(409,"Busy");if(path=="/v1/coords")return success({{"x",-123},{"y",456}});return success({{"path",path},{"body",request}});});
        settings.port=server.port();
        check(http(settings,"GET","/v1/schema")["result"]==schema(),"HTTP schema mismatch");
        check(http(settings,"GET","/v1/coords")["result"]==Json({{"x",-123},{"y",456}}),"Coordinate route mismatch");
        auto reply=http(settings,"POST","/v1/control","2,Z,\"hello, world\",P,no-replace");
        check(reply["result"]["body"]["no_replace"]==true,"CSV HTTP lost no-replace");
        check(reply["result"]["body"]["inputs"][1]=="hello, world","CSV HTTP lost comma text");
        check(http(settings,"POST","/v1/control",{{"menu",99}})["error"]["status"]==400,"Bad menu accepted");
        check(http(settings,"POST","/v1/control",{{"run","busy"}})["error"]["status"]==409,"Busy status lost");
        check(http(settings,"POST","/v1/stop",Json::object())["ok"]==true,"Stop route failed");
        auto wrong=settings;wrong.key="wrong";
        check(http(wrong,"GET","/v1/state")["error"]["status"]==401,"Missing auth gate");
        auto tokenDir=std::filesystem::temp_directory_path()/("macrohelp-key-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        auto tokenPath=tokenDir/"control-key";
        auto token=detail::privateKeyFile(tokenPath);
        check(token.size()==64&&detail::privateKeyFile(tokenPath)==token,"Private Windows token did not round-trip");
        std::filesystem::remove(tokenPath);std::filesystem::remove(tokenDir);
        // Real cross-thread admission onto a hidden message-only window. No
        // desktop windows, input hooks, or key/mouse actions are started.
        WNDCLASSW wc={};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=native_control::procedure;wc.lpszClassName=L"MacrohelpControlTest";
        check(RegisterClassW(&wc)!=0,"Cannot register test dispatcher");
        native_control::window=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
        check(native_control::window!=nullptr,"Cannot create message-only dispatcher");
        auto pending=std::async(std::launch::async,[]{return native_control::dispatch("/v1/state",Json::object());});
        auto deadline=native_control::Clock::now()+std::chrono::seconds(2);
        while(pending.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready&&native_control::Clock::now()<deadline) {
            MSG message;while(PeekMessageW(&message,native_control::window,0,0,PM_REMOVE))DispatchMessageW(&message);Sleep(1);
        }
        check(pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,"UI dispatcher did not reply");
        check(pending.get()["result"]["environment"]==before,"UI dispatch changed state");
        auto expired=std::make_shared<native_control::Ticket>();expired->path="/v1/control";expired->body=normalize({{"config",{{"vars",{{"A","must not execute"}}}}}});expired->expires=native_control::Clock::now()-std::chrono::seconds(1);
        native_control::tickets.push_back(expired);SendMessageW(native_control::window,native_control::Request,0,0);
        check(native_control::environment()==before&&!expired->done,"Expired request executed");
        auto stopped=std::async(std::launch::async,[]{return native_control::dispatch("/v1/state",Json::object());});
        for(int i=0;i<100;++i) {bool queued;{std::lock_guard<std::mutex> lock(native_control::mutex);queued=!native_control::tickets.empty();}if(queued)break;Sleep(1);}
        StopControlServer();check(stopped.get()["error"]["status"]==503,"Shutdown did not release queued request");
        std::cout<<fixtures.size()<<" fixtures; native state/temporary restore/origin/HTTP checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
