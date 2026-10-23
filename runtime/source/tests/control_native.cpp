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
