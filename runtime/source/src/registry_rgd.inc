// Run Command owns the process; the overlay only stages/reads ephemeral output.
#include <filesystem>
#include <atomic>
#include <thread>
#include <sddl.h>
static bool g_registryRgdReading=false;
static std::shared_ptr<std::atomic<bool>> g_registryRgdCancel;
static void CancelRegistryRgd(){if(g_registryRgdCancel)g_registryRgdCancel->store(true);}
static bool RegistryRgdReference(const std::string& reference) {
    if(reference.size()<4||reference[0]!='i')return false;
    size_t at=1;while(at<reference.size()&&isdigit(static_cast<unsigned char>(reference[at])))++at;
    if(at==1||at>=reference.size()||(reference[at]!='s'&&reference[at]!='z'))return false;
    size_t tail=++at;while(at<reference.size()&&isdigit(static_cast<unsigned char>(reference[at])))++at;
    return at==reference.size()&&at>tail;
}
struct RegistryRgdOutput {
    std::filesystem::path directory;
    explicit RegistryRgdOutput(const std::filesystem::path& schedules) {
        control::require(schedules.is_absolute()&&std::filesystem::is_directory(schedules),"The controller schedule directory is unavailable",503);
        GUID id;control::require(SUCCEEDED(CoCreateGuid(&id)),"Cannot name rgd output",500);
        wchar_t name[40];StringFromGUID2(id,name,40);
        directory=schedules/(L".macrohelp-rgd-"+std::wstring(name));
        HANDLE token=nullptr;control::require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"Cannot read process identity",500);
        DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<unsigned char> user(size);
        bool valid=GetTokenInformation(token,TokenUser,user.data(),size,&size)!=0;CloseHandle(token);
        control::require(valid,"Cannot read process identity",500);
        LPWSTR sid=nullptr;control::require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid)!=0,"Cannot secure rgd output",500);
        auto dacl=std::wstring(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;")+sid+L")";LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        control::require(ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0,"Cannot secure rgd output",500);
        SECURITY_ATTRIBUTES attributes={sizeof(attributes),descriptor,FALSE};
        valid=CreateDirectoryW(directory.c_str(),&attributes)!=0;LocalFree(descriptor);
        control::require(valid,"Cannot create private output in the schedule directory",500);
    }
    ~RegistryRgdOutput(){
        // A cancelled writer may be closing a file. Never recurse through a
        // user directory: only these owned files and their unique directory.
        for(int attempt=0;attempt<50;++attempt){
            for(const auto* name:{L"output.tmp",L"error.tmp",L"status.tmp",L"done"})DeleteFileW((directory/name).c_str());
            if(RemoveDirectoryW(directory.c_str())||GetLastError()==ERROR_PATH_NOT_FOUND)return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    std::string read(const wchar_t* name) const {
        auto path=directory/name;auto attributes=GetFileAttributesW(path.c_str());
        control::require(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"Missing or unsafe rgd output",500);
        control::require(std::filesystem::file_size(path)<=1024*1024,"rgd output exceeds 1 MiB",413);
        std::ifstream in(path,std::ios::binary);control::require(in.good(),"Cannot read rgd output",500);
        std::string text(1024*1024+1,'\0');in.read(text.data(),text.size());text.resize(static_cast<size_t>(in.gcount()));
        control::require(text.size()<=1024*1024,"rgd output exceeds 1 MiB",413);return text;
    }
};
// Windows argv quoting, distinct from the surrounding PowerShell literals.
static std::wstring RegistryRgdArgument(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(auto ch:value){if(ch==L'\\'){++slashes;continue;}result.append(slashes*(ch==L'"'?2:1)+(ch==L'"'?1:0),L'\\');slashes=0;result+=ch;}
    result.append(slashes*2,L'\\');return result+L"\"";
}
static control::Json RegistryRgdTask(const RegistryRgdOutput& output,const std::wstring& file,const std::string& reference) {
    auto args=L"rg "+RegistryRgdArgument(file)+L" "+RegistryRgdArgument(Utf8ToWString(reference));
    std::wstring script=L"$ErrorActionPreference='Stop';$d="+QuotePowerShellLiteral(output.directory.wstring())+L";"
        L"$utf8=[Text.UTF8Encoding]::new($false);$code=1;$p=$null;try{"
        L"$p=[Diagnostics.Process]::new();$p.StartInfo.FileName='rgd';$p.StartInfo.Arguments="+QuotePowerShellLiteral(args)+L";"
        L"$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;"
        L"$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true;"
        L"$p.StartInfo.StandardOutputEncoding=$utf8;$p.StartInfo.StandardErrorEncoding=$utf8;"
        L"$null=$p.Start();$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();"
        L"if(!$p.WaitForExit(12000)){$p.Kill();throw 'rgd timed out'};"
        L"[IO.File]::WriteAllText($d+'/output.tmp',$out.GetAwaiter().GetResult(),$utf8);"
        L"[IO.File]::WriteAllText($d+'/error.tmp',$err.GetAwaiter().GetResult(),$utf8);$code=$p.ExitCode"
        L"}catch{[IO.File]::WriteAllText($d+'/error.tmp',$_.Exception.Message,$utf8)}finally{if($p){$p.Dispose()}};"
        L"[IO.File]::WriteAllText($d+'/status.tmp',[string]$code,$utf8);[IO.File]::Move($d+'/status.tmp',$d+'/done')";
    auto document=BuildTasketDocumentJson("Registry include",{BuildSystemCommandActionJson("powershell.exe","-NoLogo -NoProfile -NonInteractive -EncodedCommand "+EncodePowerShellCommand(script))},0);
    return {{"name",WStringToUtf8(output.directory.filename().wstring())},{"run",true},{"cleanup",true},{"delay",0},{"loop",1},{"task",control::Json::parse(document)}};
}
static std::string RegistryRgdReadOutput(const std::wstring& file,const std::string& reference,const std::shared_ptr<std::atomic<bool>>& cancelled) {
    std::string response;std::wstring status;
    bool accepted=HttpGetLocalTasket(L"/health",&response,&status);
    control::require(accepted,WStringToUtf8(status),503);
    auto reply=control::Json::parse(response);auto data=reply.value("data",reply);
    control::require(data.contains("tasks_dir")&&data["tasks_dir"].is_string(),"Controller did not return its schedule directory",502);
    RegistryRgdOutput output(Utf8ToWString(data["tasks_dir"].get<std::string>()));
    control::require(!cancelled->load(),"rgd read cancelled",409);
    auto body=RegistryRgdTask(output,file,reference).dump();
    accepted=HttpPostLocalTasket(L"/temp-task",body,&response,&status);
    control::require(accepted,WStringToUtf8(status),503);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    for(;;){
        control::require(!cancelled->load(),"rgd read cancelled",409);
        if(std::filesystem::exists(output.directory/L"done")){
            if(TrimAscii(output.read(L"done"))!="0")throw control::Error(400,"rgd failed: "+TrimAscii(output.read(L"error.tmp")));
            return output.read(L"output.tmp");
        }
        control::require(std::chrono::steady_clock::now()<deadline,"rgd Run Command did not finish within 15 seconds",504);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
static std::string ReadRegistryRgd(const std::wstring& file,const std::string& reference) {
    control::require(!g_registryRgdReading,"A Registry Hub include is already being read",409);
    g_registryRgdReading=true;g_registryRgdCancel=std::make_shared<std::atomic<bool>>(false);
    struct Reset{~Reset(){g_registryRgdReading=false;g_registryRgdCancel.reset();}}reset;
    auto cancelled=g_registryRgdCancel;
    auto result=std::async(std::launch::async,[=]{return RegistryRgdReadOutput(file,reference,cancelled);});
    bool quit=false;int quitCode=0;
    while(result.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready){
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){quit=true;quitCode=static_cast<int>(message.wParam);cancelled->store(true);break;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
    }
    if(quit)PostQuitMessage(quitCode);
    control::require(!cancelled->load(),"rgd read cancelled",409);
    return result.get();
}
// Injectable for native compiler checks; production always uses the daemon.
static decltype(&ReadRegistryRgd) g_registryRgdReader=ReadRegistryRgd;
struct RegistrySourceToken {
    std::string text,source;
    std::vector<std::string> ancestry;
};
static void ValidateRegistryStructure(const std::vector<std::string>& tokens) {
    std::vector<bool> branches;
    for(const auto& token:tokens) {
        auto lower=LowerAscii(TrimAscii(token));
        if(lower.rfind("if ",0)==0){control::require(branches.size()<16,"Conditions nested too deeply");branches.push_back(false);}
        else if(lower=="else"){control::require(!branches.empty()&&!branches.back(),"Unexpected {else}.");branches.back()=true;}
        else if(lower=="endif"||lower=="end if"){control::require(!branches.empty(),"Unexpected {endif}.");branches.pop_back();}
    }
    control::require(branches.empty(),"Registry Hub script has an unclosed {if ...}.");
}
static std::vector<RegistrySourceToken> RegistryRgdTokens(const RegistrySourceToken& token,size_t& expandedBytes) {
    auto reference=LowerAscii(TrimAscii(ExpandRegistryHubValueReferences(TrimAscii(token.text.substr(3)))));
    control::require(RegistryRgdReference(reference),"Use {rgd i6s6} or {rgd i6z6}");
    auto file=TrimWide(g_registryFilePath);
    control::require(!file.empty()&&file[0]!=L'-',"Set the Registry Hub file path before using {rgd ...}");
    auto key=WStringToUtf8(file)+"\n"+reference;
    control::require(token.ancestry.size()<16&&std::find(token.ancestry.begin(),token.ancestry.end(),key)==token.ancestry.end(),"rgd "+reference+": Recursive rgd include");
    try {
        auto text=g_registryRgdReader(file,reference);
        control::require(text.size()<=1024*1024-expandedBytes,"Expanded workflow exceeds 1 MiB",413);
        expandedBytes+=text.size();
        auto raw=ExtractRegistryHubTokens(Utf8ToWString(text));ValidateRegistryStructure(raw);
        auto ancestry=token.ancestry;ancestry.push_back(key);
        std::vector<RegistrySourceToken> result;for(const auto& item:raw)result.push_back({item,reference,ancestry});
        return result;
    }catch(const std::exception& error){throw control::Error(400,"rgd "+reference+": "+error.what());}
}
