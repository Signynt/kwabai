#include "layers.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool b){if(!b)throw std::runtime_error("layer assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
int main(){try{
 // Public setters enforce forced KDE rules internally. Confirm flags and
 // restore the opposing flag when the requested flag is rejected.
 std::pair<bool,bool> flags{false,true};bool alive=true,forceAboveOff=true,forceBelowOn=false;int aboveWrites=0,belowWrites=0;
 auto readFlags=[&]()->std::optional<std::pair<bool,bool>>{if(!alive)return {};return flags;};
 auto setAbove=[&](bool value){++aboveWrites;if(!forceAboveOff||!value)flags.first=value;};
 auto setBelow=[&](bool value){++belowWrites;if(!forceBelowOn||value)flags.second=value;};
 check(!setLayerFlags("above",readFlags,setAbove,setBelow)&&flags==std::pair(false,true));check(aboveWrites==1&&belowWrites==2);
 forceAboveOff=false;forceBelowOn=true;aboveWrites=belowWrites=0;check(!setLayerFlags("above",readFlags,setAbove,setBelow)&&aboveWrites==0&&flags==std::pair(false,true));
 forceBelowOn=false;check(setLayerFlags("above",readFlags,setAbove,setBelow)&&flags==std::pair(true,false));check(setLayerFlags("normal",readFlags,setAbove,setBelow)&&flags==std::pair(false,false));check(setLayerFlags("below",readFlags,setAbove,setBelow)&&flags==std::pair(false,true));
 alive=false;aboveWrites=belowWrites=0;check(!setLayerFlags("above",readFlags,setAbove,setBelow)&&aboveWrites==0&&belowWrites==0);alive=true;
 auto destroyAbove=[&](bool){alive=false;};flags={false,false};check(!setLayerFlags("above",readFlags,destroyAbove,setBelow));alive=true;
 const QString uuid="11111111-1111-4111-8111-111111111111",instance="22222222-2222-4222-8222-222222222222";QString layer="normal";bool managed=false,blocked=false;int priority=0;int writes=0,sequence=0;
 LayerRequests layers([&](auto){return layer;},[&](auto,auto desired){++writes;if(blocked)return false;layer=desired;return true;},[&](auto){return managed;},[&](auto){return priority;});
 auto snapshot=[&]{return QJsonObject{{"instance",instance},{"windows",QJsonArray{QJsonObject{{"uuid",uuid},{"layer",layer},{"fullscreen",false}}}}};};
 auto request=[&](QString desired,bool reset=false){return QJsonObject{{"protocol",1},{"id",QString("33333333-3333-4333-8333-%1").arg(++sequence,12,10,QChar('0'))},{"request-sequence",QString::number(sequence)},{"bridge-instance",instance},{"window",uuid},{"layer",desired},{"expected-layer",layer},{"reset-auto",reset}};};
 auto automatic=request("auto");check(layers.submit("owner",automatic,snapshot())["success"].toBool()&&writes==0);managed=true;check(layers.tick()&&layer=="below"&&writes==1);managed=false;check(layers.tick()&&layer=="normal");
 // Zoom and scratchpad priority promote automatic windows; ordinary floats
 // remain normal and unzoom restores below while explicit choices still win.
 managed=true;check(layers.tick()&&layer=="below");priority=1;check(layers.tick()&&layer=="above");priority=2;check(!layers.tick()&&layer=="above");priority=0;check(layers.tick()&&layer=="below");managed=false;check(layers.tick()&&layer=="normal");
 auto explicitAbove=request("above");explicitAbove.remove("reset-auto");check(layers.submit("owner",explicitAbove,snapshot())["success"].toBool()&&layer=="above");const auto before=writes;managed=true;check(!layers.tick()&&layer=="above");check(layers.submit("owner",explicitAbove,snapshot())["success"].toBool()&&writes==before);rejects([&]{layers.submit("other",explicitAbove,snapshot());});
 auto reset=request("auto",true);check(layers.submit("owner",reset,snapshot())["success"].toBool()&&layer=="below");layers.release("owner");check(layer=="normal");
 layer="above";layers.submit("owner",request("auto"),snapshot());check(!layers.tick()&&layer=="above");layers.release("owner");check(layer=="above");
 layer="normal";managed=true;layers.submit("owner",request("auto"),snapshot());layer="above";check(layers.tick()&&layers.describe(uuid)["layer-mode"]=="external");layers.teardown();check(layer=="above");
 layer="normal";blocked=true;auto denied=request("above");denied.remove("reset-auto");check(layers.submit("owner",denied,snapshot())["state"]=="blocked"&&layer=="normal");blocked=false;
 auto stale=request("below");stale.remove("reset-auto");stale["expected-layer"]="above";rejects([&]{layers.submit("owner",stale,snapshot());});auto hidden=snapshot();auto w=hidden["windows"].toArray()[0].toObject();w["fullscreen"]=true;hidden["windows"]=QJsonArray{w};auto invalid=request("above");invalid.remove("reset-auto");rejects([&]{layers.submit("owner",invalid,hidden);});
 auto keep=request("above");keep.remove("reset-auto");layers.submit("owner",keep,snapshot());layers.release("owner");check(layer=="above");
 layer="normal";managed=true;layers.submit("owner",request("auto"),snapshot());check(layer=="below");layers.teardown();check(layer=="normal");
 std::cout<<"PASS automatic transitions, explicit layers, reset, rule refusal, stale requests, external changes and cleanup\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
