#pragma once
#include "control_protocol.hpp"
#include <cctype>
#include <sstream>

namespace control {
inline std::string skillName(const Json& value) {
    auto text=value.is_string()?trim(value.get<std::string>()):"";
    require(!text.empty()&&text.size()<=128&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<32||c==127;}),"Skill names must be 1–128 bytes on one line");
    return text;
}
inline Json skillRequest(const Json& value,bool query=false) {
    fields(value,query?std::vector<std::string>{"name"}:std::vector<std::string>{"name","rgd"});
    auto title=skillName(value.value("name",Json()));
    if(query)return {{"name",title}};
    auto lower=title;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    auto ref=skillName(value.value("rgd",Json()));
    if(lower!="remove") {
        std::transform(ref.begin(),ref.end(),ref.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        size_t at=1;while(at<ref.size()&&ref[at]>='0'&&ref[at]<='9')++at;
        bool valid=ref[0]=='i'&&at>1&&at<ref.size()&&(ref[at]=='s'||ref[at]=='z');
        size_t start=++at;while(at<ref.size()&&ref[at]>='0'&&ref[at]<='9')++at;
        require(valid&&at==ref.size()&&at>start,"Use an rgd reference such as i7s1 or i8z1");
    }
    return {{"name",title},{"rgd",ref}};
}
inline void validateSkills(const Json& map) {
    require(map.is_object(),"Skills must be an object");require(map.size()<=256,"At most 256 saved skills",413);
    for(auto it=map.begin();it!=map.end();++it){auto v=skillRequest({{"name",it.key()},{"rgd",it.value()}});auto lower=it.key();std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});require(v["name"]==it.key()&&v["rgd"]==it.value()&&lower!="remove","Invalid saved skill");}
}
inline Json skillsSnapshot(const Json& map) {
    std::string text="User saved skills:";for(auto it=map.begin();it!=map.end();++it)text+="\n"+it.key()+" → "+it.value().get<std::string>();
    if(map.empty())text+=" none";
    return {{"skills",map},{"text",text}};
}
inline std::string skillSummary(const std::string& output) {
    std::istringstream lines(output);std::string line;
    while(std::getline(lines,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("string ",0)==0){auto end=line.find(": ",7);if(end!=std::string::npos&&end>7&&std::all_of(line.begin()+7,line.begin()+end,[](unsigned char c){return c>='0'&&c<='9';}))line=line.substr(end+2);}
        if(line.rfind("Summary",0)==0)return line;
    }
    return "<none>";
}
}
