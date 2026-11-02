#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("hidden release assertion failed");}
template<class F>void rejects(F fn){bool failed=false;try{fn();}catch(const std::invalid_argument &){failed=true;}check(failed);}
QString id(int n){return QString("eeeeeeee-eeee-4eee-8eee-%1").arg(n,12,16,QChar('0'));}
QJsonObject rect(QRectF r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
int main(){try{
 for(bool fullscreen:{false,true})for(int scenario:{0,1,2}){
    const bool fail=scenario==1;
    const auto a=id(1),b=id(2);const QRectF originalA(100,100,600,500),originalB(800,200,700,600);QMap<QString,QRectF> frames{{a,originalA},{b,originalB}};QMap<QString,int> writes;bool hidden=false,refuse=false;
    GeometryTransactions engine([&](auto uuid)->std::optional<QRectF>{if(!frames.contains(uuid))return {};return frames[uuid];},[&](auto uuid,QRectF frame){++writes[uuid];if(uuid==a&&hidden)return false;if(refuse&&uuid==b&&frame.width()==2540)return false;frames[uuid]=frame;return true;});
    QJsonObject output{{"id","screen"},{"name","DP-1"},{"current-desktop",id(9)},{"current-activity","work"},{"work-area",rect({0,0,2560,1440})}};
    auto snapshot=[&]{QJsonArray windows;for(auto it=frames.begin();it!=frames.end();++it){bool invisible=hidden&&it.key()==a;windows.append(QJsonObject{{"uuid",it.key()},{"frame",rect(it.value())},{"candidate",!invisible},{"minimized",invisible&&!fullscreen},{"fullscreen",invisible&&fullscreen},{"output","DP-1"},{"desktops",QJsonArray{id(9)}},{"activities",QJsonArray{"work"}},{"minimum",QJsonObject{{"w",100},{"h",100}}}});}return QJsonObject{{"instance",id(8)},{"outputs",QJsonArray{output}},{"windows",windows}};};
    auto request=[&](int n,bool both,int prior){QJsonArray placements;int i=0;for(auto uuid:both?QStringList{a,b}:QStringList{b})placements.append(QJsonObject{{"uuid",uuid},{"expected-frame",rect(frames[uuid])},{"frame",rect(both?QRectF(10+1275*i++,10,1265,1420):QRectF(10,10,2540,1420))}});QJsonObject r{{"protocol",1},{"id",id(20+n)},{"request-sequence",QString::number(n)},{"bridge-instance",id(8)},{"context-id",id(7)},{"context-generation",QString::number(n)},{"context",QJsonObject{{"output-id","screen"},{"output","DP-1"},{"desktop",id(9)},{"activity","work"}}},{"work-area",output["work-area"]},{"placements",placements},{"lifecycle",true}};if(prior)r["supersedes"]=id(20+prior);return r;};
    auto confirm=[&](int now){engine.tick(now+16);engine.tick(now+32);engine.tick(now+48);};
    engine.apply("owner",request(1,true,0),snapshot(),0);confirm(0);hidden=true;if(fullscreen)frames[a]={0,0,2560,1440};
    auto release=request(2,false,1);release["release-mode"]="hidden";release["release"]=QJsonArray{QJsonObject{{"uuid",a},{"expected-frame",rect(frames[a])}}};const auto before=writes;
    rejects([&]{engine.apply("stranger",release,snapshot(),100);});auto bad=release;bad.remove("lifecycle");rejects([&]{engine.apply("owner",bad,snapshot(),100);});
    auto visible=snapshot();auto windows=visible["windows"].toArray();for(int i=0;i<windows.size();++i){auto w=windows[i].toObject();w["minimized"]=false;w["fullscreen"]=false;w["candidate"]=true;windows[i]=w;}visible["windows"]=windows;rejects([&]{engine.apply("owner",release,visible,100);});
    auto moved=snapshot();windows=moved["windows"].toArray();for(int i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]==QJsonValue(a)){w["output"]="DP-2";windows[i]=w;}}moved["windows"]=windows;rejects([&]{engine.apply("owner",release,moved,100);});check(writes==before);
    const auto endpoint=frames[a];refuse=fail;engine.apply("owner",release,snapshot(),100);
    if(scenario==2){engine.release("owner",110);confirm(110);engine.teardown();check(writes[a]==before.value(a)&&frames[a]==endpoint&&frames[b]==QRectF(10,10,2540,1420));continue;}
    confirm(100);check(writes[a]==before.value(a)&&frames[a]==endpoint);
    if(fail){check(engine.status("owner",id(22))["state"]=="rolled_back");check(engine.status("owner",id(21))["state"]=="applied");check(frames[b]==QRectF(1285,10,1265,1420));}
    else {
        check(engine.status("owner",id(22))["state"]=="applied"&&!engine.owns(a)&&engine.owns(b));
        // Readmission captures the current frame; the old BSP slot and original
        // floating baseline of the unmanaged window are deliberately discarded.
        hidden=false;frames[a]={400,300,900,700};const auto arrival=frames[a];engine.apply("owner",request(3,true,2),snapshot(),200);confirm(200);engine.forget("owner",id(23));confirm(300);check(frames[a]==QRectF(10,10,1265,1420)&&frames[b]==QRectF(1285,10,1265,1420));
    }
    // Owner loss after rollback also leaves the hidden predecessor untouched.
    if(fail){engine.release("owner",400);confirm(400);check(writes[a]==before.value(a));}
 }
 std::cout<<"PASS hidden release, scope/owner guards, rollback, baseline isolation and fresh admission\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
