#include "state.h"
#include "scratchpads.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("bridge state assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
QJsonObject window(const QString &uuid) {return {{"uuid",uuid},{"app","kitty"},{"title","test"},{"output","DP-1"},
    {"desktops",QJsonArray{"11111111-1111-4111-8111-111111111111"}},{"activities",QJsonArray{}},
    {"frame",QJsonObject{{"x",-1080},{"y",0},{"w",1080},{"h",1920}}},{"minimum",QJsonObject{{"w",100},{"h",100}}},
    {"minimized",false},{"fullscreen",false},{"focused",true},{"candidate",true}};}
int main(){try {
 BridgeState state;auto a=window("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),b=window("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
 QJsonObject s{{"protocol",1},{"instance","22222222-2222-4222-8222-222222222222"},{"sequence","1"},{"kwin-version","6.8.1"},{"capabilities",QJsonArray{"snapshot"}},{"windows",QJsonArray{a,b}}};
 auto accept=[&]{state.accept(QJsonDocument(s).toJson());};accept();check(state.connected());auto first=state.windows();accept();check(state.windows()==first);
 check(first["windows"].toArray()[0].toObject()["id"]==QJsonValue("1"));
 s["sequence"]="2";s["windows"]=QJsonArray{b};accept();check(state.windows()["windows"].toArray()[0].toObject()["id"]==QJsonValue("2"));
 s["sequence"]="1";rejects(accept);check(state.windows()["sequence"]==QJsonValue("2"));
 s["sequence"]="2";s["windows"]=QJsonArray{a};rejects(accept);
 s["sequence"]="3";s["windows"]=QJsonArray{a,a};rejects(accept);
 s["windows"]=QJsonArray{a};s["protocol"]=2;rejects(accept);
 s["protocol"]=1;auto bad=a;bad["minimum"]=QJsonObject{{"w",-1},{"h",10}};s["windows"]=QJsonArray{bad};rejects(accept);
 state.disconnect("lost connection");check(!state.connected());check(state.windows()["windows"].toArray().isEmpty());
 s["instance"]="33333333-3333-4333-8333-333333333333";s["sequence"]="1";s["windows"]=QJsonArray{a,b};accept();
 check(state.windows()["windows"].toArray()[0].toObject()["id"]==QJsonValue("1"));
 rejects([&]{state.accept(QByteArray(1024*1024+1,' '));});
 // Mouse snapshots are capability-gated and malformed authorizations cannot
 // partially replace a previously accepted window list.
 auto g=QJsonObject{{"id",a["uuid"]},{"transaction",b["uuid"]},{"kind","resize"},{"state","active"},{"start-frame",a["frame"]},{"frame",a["frame"]},{"pointer",QJsonObject{{"x",0},{"y",0}}}};
 s["capabilities"]=QJsonArray{"snapshot","mouse-output-drop"};s["sequence"]="2";rejects(accept);
 s["capabilities"]=QJsonArray{"snapshot","drop-preview"};s["sequence"]="2";rejects(accept);s["capabilities"]=QJsonArray{"snapshot"};
 a["interaction"]=g;s["sequence"]="2";s["windows"]=QJsonArray{a,b};rejects(accept);
 s["capabilities"]=QJsonArray{"snapshot","contexts","events","geometry","geometry-update","mouse","drop-preview"};s["event-sequence"]="0";
 s["outputs"]=QJsonArray{QJsonObject{{"id","out"},{"name","DP-1"},{"scale",1},{"geometry",a["frame"]},{"work-area",a["frame"]},{"current-desktop","11111111-1111-4111-8111-111111111111"},{"current-activity","work"}}};accept();check(state.hasMouse());check(state.hasDropPreview());auto validated=state.windows();
 s["sequence"]="3";g["kind"]="unknown";a["interaction"]=g;s["windows"]=QJsonArray{a};rejects(accept);check(state.windows()==validated);
 g["kind"]="move";g["pointer"]=QJsonObject{{"x","invalid"},{"y",0}};a["interaction"]=g;s["windows"]=QJsonArray{a};rejects(accept);check(state.windows()==validated);
 a.remove("interaction");a["requested-frame"]=a.value("frame");a["client-frame"]=a.value("frame");a["buffer-frame"]=a.value("frame");s["windows"]=QJsonArray{a,b};accept();check(state.windows()["windows"].toArray()[0].toObject()["requested-frame"]==a["frame"]);validated=state.windows();
 s["sequence"]="4";auto badBuffer=a["frame"].toObject();badBuffer["w"]=-1;a["buffer-frame"]=badBuffer;s["windows"]=QJsonArray{a};rejects(accept);check(state.windows()==validated);
 // Native scratchpad decoration must produce a startup-valid snapshot even
 // when no window has an interaction. Missing fields remain an atomic error.
 s["sequence"]="5";auto caps=s["capabilities"].toArray();caps.append("geometry-retention");caps.append("geometry-lifecycle");caps.append("geometry-release");caps.append("scratchpads");s["capabilities"]=caps;
 a["buffer-frame"]=a["frame"];s["windows"]=QJsonArray{a,b};bool missingMetadata=false;try{accept();}catch(const std::invalid_argument &error){missingMetadata=QString::fromUtf8(error.what())=="invalid scratchpad metadata";}check(missingMetadata&&state.windows()==validated);
 Scratchpads pads([](const QString &){return QJsonObject{};},[](const QString &,bool,bool){return true;},[](const QString &){return false;});
 s["windows"]=pads.decorate(s["windows"].toArray(),[](const QString &){return QJsonObject{};});accept();check(state.connected()&&state.hasScratchpads());
 for(auto value:state.windows()["windows"].toArray()){auto w=value.toObject();check(w["scratchpad"]==""&&w["scratchpad-hidden"]==false&&!w.contains("interaction"));}
 std::cout<<"PASS bridge validation, monotonic snapshots, atomic rejection and stable public IDs\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
