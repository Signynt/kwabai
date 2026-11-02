#include "client.h"
#include "spacing.h"
#include "resize.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai::compat;
void check(bool ok){if(!ok)throw std::runtime_error("compatibility assertion failed");}
QJsonObject reply(QJsonDocument doc){return {{"status",0},{"stdout",QString::fromUtf8(doc.toJson())},{"stderr",""}};}
int main(){try{
 int calls=0;QStringList last,submitted;bool fail=false,floating=false;
 Call backend=[&](const QStringList &a){++calls;last=a;
  if(a[0]=="compat-windows")return reply(QJsonDocument(QJsonArray{QJsonObject{{"id",7},{"app","test"},{"title","title"},{"frame",QJsonObject{}},{"has-focus",true},{"is-floating",floating},{"kwabai-output","DP-1"}}}));
  if(a[0]=="compat-layer")return reply(QJsonDocument(QJsonObject{{"completed",true},{"success",true},{"acknowledged",true}}));
  if(a[0]=="bridge")return reply(QJsonDocument(QJsonObject{{"id","request"},{"state",fail?"failed":"focused"},{"completed",true},{"success",!fail},{"acknowledged",!fail},{"error",fail?"denied":""}}));
  if(a[0]=="context"&&(a[1]=="focus"||a[1]=="focus-window")){submitted=a;return reply(QJsonDocument(QJsonObject{{"id","request"},{"completed",false},{"success",false},{"confirmed-focus-id","7"}}));}
  if(a[0]=="context"||a[0]=="compat-swap"||a[0]=="compat-ratio"||a[0]=="compat-space"||a[0]=="compat-resize"||a[0]=="compat-split"||a[0]=="compat-zoom")return reply(QJsonDocument(QJsonObject{{"id","request"},{"completed",false},{"success",false}}));
  return reply(QJsonDocument(QJsonObject{}));
 };
 auto color=run({"config","insert_window_border_color","0x80D75F5F"},backend);check(color.status==0&&last==QStringList({"config","insert_window_border_color","0x80d75f5f"}));
 check(run({"config","insert_feedback_color"},backend).status==0&&last==QStringList({"config","insert_feedback_color"}));
 check(run({"config","window_border_radius","20"},backend).status==0&&last==QStringList({"config","window_border_radius","20"}));check(run({"config","window_border_radius"},backend).status==0);
 int beforeConfig=calls;for(auto invalid:{"-1","21","1.5","nan",""})check(run({"config","window_border_radius",invalid},backend).status==1);for(auto args:std::vector<QStringList>{{"config","insert_window_border_color","#d75f5f"},{"config","insert_window_border_color","0xffzzzzzz"},{"config","insert_window_border_color","0x123"},{"config","insert_window_border_color","0x100000000"},{"config","insert_window_border_color","-1"},{"config","insert_window_border_color","0xffd75f5f","extra"},{"config","unknown","on"}})check(run(args,backend).status==1);check(calls==beforeConfig);
 for(auto spelling:{"--layer","--sub-layer"})for(auto value:{"above","normal","below","auto"}){auto result=run({"window","7",spelling,value},backend);check(result.status==0&&result.output.isEmpty()&&last==QStringList({"compat-layer","7",value}));}
 check(run({"window","--layer","sideways"},backend).status==1);check(run({"window","mouse","--layer","above"},backend).status==1);
 auto query=run({"query","--windows"},backend);check(query.status==0);auto windows=QJsonDocument::fromJson(query.output.toUtf8()).array();check(windows[0].toObject()["id"].isDouble());check(!windows[0].toObject().contains("kwabai-output"));
 query=run({"query","--windows","id,app","--window"},backend);auto one=QJsonDocument::fromJson(query.output.toUtf8()).object();check(query.status==0&&one.size()==2&&one["id"]==QJsonValue(7));
 auto result=run({"window","--focus","east"},backend);check(result.status==0&&result.output.isEmpty()&&last==QStringList({"compat-windows"}));fail=true;result=run({"window","--focus","west"},backend);check(result.status==1&&result.output.isEmpty()&&result.error.contains("denied"));fail=false;
 result=run({"window","7","--toggle","float"},backend);check(result.status==0&&result.output.isEmpty());
 result=run({"window","7","--swap","east"},backend);check(result.status==0&&result.output.isEmpty()&&last==QStringList({"bridge","transaction","request"}));
 fail=true;check(run({"window","--swap","8"},backend).status==1);fail=false;
 result=run({"window","7","--ratio","rel:0.1"},backend);check(result.status==0&&result.output.isEmpty());
 fail=true;check(run({"window","--ratio","abs:0.5"},backend).status==1);fail=false;
 for(auto args:std::vector<QStringList>{{"space","--rotate","90"},{"space","--mirror","x-axis"},{"space","--equalize"},{"space","--equalize","y-axis"}}){auto done=run(args,backend);check(done.status==0&&done.output.isEmpty());}
 for(auto args:std::vector<QStringList>{{"window","--focus","7"},{"window","7","--focus"}}){auto done=run(args,backend);check(done.status==0&&done.output.isEmpty()&&submitted==QStringList({"context","focus-window","focused","7"}));}
 check(run({"window","--focus"},backend).status==0&&submitted==QStringList({"context","focus-window","focused","focused"}));
 int snapshots=0;Call delayed=[&](const QStringList &a){if(a[0]=="context")return reply(QJsonDocument(QJsonObject{{"completed",true},{"success",true},{"acknowledged",true},{"confirmed-focus-id","7"}}));++snapshots;return reply(QJsonDocument(QJsonArray{QJsonObject{{"id",7},{"has-focus",snapshots>1}},QJsonObject{{"id",8},{"has-focus",snapshots==1}}}));};
 check(run({"window","--focus","7"},delayed).status==0&&snapshots==2);
 for(auto args:std::vector<QStringList>{{"space","--gap","abs:20"},{"space","--gap","rel:-5"},{"space","--padding","abs:1:2:3:4"},{"space","--padding","rel:-1:0:+2:3"}}){auto done=run(args,backend);check(done.status==0&&done.output.isEmpty());}
 auto relative=parseSpacing("rel:-20:5:10:-100",4);check(resolveSpacing(relative,{10,20,30,40})==QList<double>({0,25,40,0}));
 check(resolveSpacing(parseSpacing("rel:2",1),{10.25})==QList<double>({12.25}));check(resolveSpacing(parseSpacing("abs:+0010",1),{10})==QList<double>({10}));
 bool overflow=false;try{resolveSpacing(parseSpacing("rel:2147483647",1),{10});}catch(const std::invalid_argument &){overflow=true;}check(overflow);
 for(auto args:std::vector<QStringList>{{"window","--resize","right:50:0"},{"window","7","--resize","bottom_left:-20:30"}}){auto done=run(args,backend);check(done.status==0&&done.output.isEmpty());}
 auto resize=parseEdgeResize("top_right:+12.5:-20");check(resize.dx==12.5&&resize.dy==-20);
 for(auto args:std::vector<QStringList>{{"window","--toggle","split"},{"window","7","--toggle","split"},{"window","--toggle","zoom-fullscreen"},{"window","7","--toggle","zoom-fullscreen"}}){auto done=run(args,backend);check(done.status==0&&done.output.isEmpty());}
 int before=calls;for(auto args:std::vector<QStringList>{{"window","mouse","--toggle","split"},{"window","--toggle","split","extra"},{"window","--toggle","sticky"},{"window","--resize","abs:500:500"},{"window","--resize","right:nan:0"},{"window","--resize","bottom:0:inf"},{"window","--resize","right:1:2:3"},{"window","mouse","--resize","left:10:0"},{"space","--gap","abs:-1"},{"space","--gap","rel:2147483648"},{"space","--gap","rel:1.5"},{"space","--gap","abs:1e2"},{"space","--gap","rel:"},{"space","--gap","wat:1"},{"space","--padding","abs:1:2:3"},{"space","--padding","abs:1:-2:3:4"},{"space","--padding","rel:1:2:3:4:5"},{"window","--focus","0"},{"window","--focus","007"},{"window","mouse","--focus"},{"window","7","--focus","8"},{"window","--focus","next"},{"window","--focus","7","extra"},{"space","--rotate","45"},{"space","2","--rotate","90"},{"space","--mirror","x"},{"space","--equalize","z-axis"},{"window","--ratio","rel:nan"},{"window","--ratio","abs:0.5:extra"},{"window","mouse","--ratio","rel:0.1"},{"window","--ratio","junk:0.5"},{"window","--swap","recent"},{"space","--balance"},{"signal","--add","event=window_created","action=echo ignored"},{"rule","--add","app=.*","manage=off","sticky=on"},{"rule","--add","app=.*","manage=off","manage=on"}}){check(run(args,backend).status==1);check(calls==before);}
 result=run({"rule","--add","app=^systemsettings$","manage=off","label=Settings float"},backend);check(result.status==0&&result.output.isEmpty());check(last==QStringList({"rule","add-posix","Settings float","float","^systemsettings$","",""}));
 Call denied=[](const QStringList &){return QJsonObject{{"status",3},{"stdout",""},{"stderr","control disabled"}};};check(run({"window","--focus","east"},denied).status==1);
 std::cout<<"PASS compatible grammar, numeric query IDs, filtered output, quiet mutations, confirmed errors and unsupported-command rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
