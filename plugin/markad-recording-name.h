#pragma once
#include <string>
#include <algorithm>
// Same naming rules as VDR cRecording, without mutating the timer.
inline std::string MarkadRecordingName(const std::string &file, bool single,
        bool useSubtitle, const char *title, const char *subtitle, const char *channel) {
    std::string name=file;
    bool macros=name.find("TITLE")!=std::string::npos || name.find("EPISODE")!=std::string::npos;
    auto replace=[&](const std::string &key,const char *value) {
        size_t p=0; std::string text=value ? value : "";
        while ((p=name.find(key,p))!=std::string::npos) {name.replace(p,key.size(),text);p+=text.size();}
    };
    if (macros) {
        replace("TITLE",title && *title ? title : channel);
        replace("EPISODE",subtitle && *subtitle ? subtitle : " ");
        while (name.size()>2 && name.back()==' ' && name[name.size()-2]!='~') name.pop_back();
    } else if (!single && useSubtitle) name += std::string("~")+(subtitle && *subtitle ? subtitle : " ");
    std::replace(name.begin(),name.end(),'\n',' ');
    return name;
}
