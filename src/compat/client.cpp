#include "client.h"
#include "spacing.h"
#include "resize.h"
#include "grid.h"
#include "config.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <stdexcept>
#include <cmath>
namespace kwabai::compat {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
QString checked(const Call &call,const QStringList &args) {
    auto reply=call(args);if(reply["status"].toInt()!=0)throw std::runtime_error(reply["stderr"].toString().trimmed().toStdString());return reply["stdout"].toString();
}
QJsonObject object(const Call &call,const QStringList &args) {auto doc=QJsonDocument::fromJson(checked(call,args).toUtf8());need(doc.isObject(),"invalid daemon reply");return doc.object();}
void wait(const Call &call,QJsonObject receipt,bool focus) {
    auto placements=receipt["confirmed-placements"].toArray();
    const bool transfer=(receipt["kind"]==QJsonValue("output-transfer")||receipt["kind"]==QJsonValue("desktop-transfer")),desktop=receipt["kind"]==QJsonValue("desktop-move");
    const auto desktopId=receipt.value("confirmed-desktop").toString();
    const auto outputName=receipt["confirmed-output"].toString(),outputWindow=receipt["confirmed-window-id"].toString();
    const auto focusId=receipt["confirmed-focus-id"].toString(),layerId=receipt["confirmed-layer-id"].toString();
    const bool layer=receipt["kind"]==QJsonValue("layer"),scratchpad=receipt["kind"]==QJsonValue("scratchpad");
    QElapsedTimer timer;timer.start();
    while(!receipt["completed"].toBool()) {
        need(timer.elapsed()<(transfer?20000:5000),"command outcome is uncertain; inspect native transaction status before retrying");
        QThread::msleep(20);receipt=object(call,transfer?QStringList{"context","transfer-status",receipt["id"].toString()}:QStringList{"bridge",scratchpad?"scratchpad-status":layer?"layer-status":desktop?"desktop-transaction":focus?"focus-status":"transaction",receipt["id"].toString()});
    }
    if(receipt.contains("confirmed-placements"))placements=receipt["confirmed-placements"].toArray();
    if(!receipt["success"].toBool()||!receipt["acknowledged"].toBool())throw std::runtime_error((receipt["state"].toString()+": "+receipt["error"].toString()+(transfer?"; inspect context transfer-status "+receipt["id"].toString():desktop?"; inspect bridge desktop-transaction "+receipt["id"].toString():QString{})).toStdString());
    if(scratchpad&&!receipt["window"].toString().isEmpty())for(;;){
        auto snapshot=object(call,{"bridge","windows"});bool found=false;
        for(auto v:snapshot["windows"].toArray()){auto w=v.toObject();if(w["uuid"]==receipt["window"]&&w["scratchpad"]==receipt["scratchpad"]&&w["scratchpad-hidden"]==receipt["scratchpad-hidden"])found=true;}
        if(found&&!receipt["scratchpad"].toString().isEmpty()){
            auto query=QJsonDocument::fromJson(checked(call,{"compat-windows"}).toUtf8());bool floating=false;
            for(auto v:query.array()){auto w=v.toObject();if(w["scratchpad"]==receipt["scratchpad"]&&w["is-floating"].toBool())floating=true;}
            found=floating;
        }
        if(found)break;
        need(timer.elapsed()<5000,"scratchpad acknowledged but snapshot is not current");QThread::msleep(20);
    }
    if(layer)for(;;){auto doc=QJsonDocument::fromJson(checked(call,{"compat-windows"}).toUtf8());bool found=false;for(auto v:doc.array()){auto w=v.toObject();if(w["id"].toDouble()==layerId.toDouble()&&w["layer"]==receipt["layer"])found=true;}if(found)break;need(timer.elapsed()<5000,"layer acknowledged but snapshot is not current");QThread::msleep(20);}
    if(!desktopId.isEmpty())for(;;) {
        auto snapshot=object(call,{"bridge","windows"});bool found=false;
        for(auto v:snapshot["windows"].toArray()){auto w=v.toObject();if(w["id"]==QJsonValue(outputWindow)&&w["desktops"]==QJsonValue(QJsonArray{desktopId}))found=true;}
        if(found)break;
        need(timer.elapsed()<(transfer?20000:5000),"desktop movement was acknowledged but its snapshot is not current; inspect desktop-transaction before retrying");QThread::msleep(20);
    }
    // A following script command must see the acknowledged layout in the daemon snapshot.
    if(!placements.isEmpty()||!focusId.isEmpty()||!outputName.isEmpty())for(;;) {
        auto doc=QJsonDocument::fromJson(checked(call,{"compat-windows"}).toUtf8());need(doc.isArray(),"invalid window query reply");bool matches=true;
        for(auto value:placements) {
            auto placement=value.toObject();QJsonObject actual;
            for(auto window:doc.array())if(window.toObject()["id"].toDouble()==placement["id"].toString().toDouble())actual=window.toObject();
            if(actual.isEmpty()){matches=false;break;}
            for(auto field:{"x","y","w","h"})if(std::abs(actual["frame"].toObject()[field].toDouble()-placement["frame"].toObject()[field].toDouble())>1)matches=false;
        }
        if(!focusId.isEmpty()) {
            int focused=0;bool target=false;
            for(auto value:doc.array()){auto window=value.toObject();if(window["has-focus"].toBool()){++focused;target=window["id"].toDouble()==focusId.toDouble();}}
            matches=matches&&focused==1&&target;
        }
        if(!outputName.isEmpty()) {
            bool outputMatches=false;for(auto value:doc.array()){auto w=value.toObject();if(w["id"].toDouble()==outputWindow.toDouble()&&w["kwabai-output"]==QJsonValue(outputName))outputMatches=true;}
            matches=matches&&outputMatches;
        }
        if(matches)break;
        need(timer.elapsed()<(transfer?20000:5000),"command was acknowledged but its snapshot is not current; inspect context status before retrying");QThread::msleep(20);
    }
}
QJsonObject selected(const QJsonArray &windows,const QString &selector) {
    bool numeric=false;auto id=selector.toULongLong(&numeric);if(selector!="focused")need(numeric&&id>0&&id<=9007199254740991ULL&&QString::number(id)==selector,"only numeric window IDs are supported for this selector");
    QJsonObject found;int count=0;for(auto v:windows){auto w=v.toObject();if((selector=="focused"&&w["has-focus"].toBool())||(selector!="focused"&&w["id"].toDouble()==double(id))){found=w;++count;}}
    need(count==1,"window selector is missing or ambiguous");return found;
}
}
Result run(const QStringList &a,const Call &call) {
    try {
        need(a.size()>=2,"expected -m DOMAIN COMMAND");auto domain=a[0];
        if(domain=="config") {
            need((a.size()==2||a.size()==3)&&(isPreviewSetting(a[1])||isLayoutSetting(a[1])),"supported config: float_popups, layout, top/bottom/left/right_padding, window_gap, insert_feedback_color, window_border_radius; window_shadow is controlled by KDE");
            auto args=a;if(a.size()==3&&isPreviewSetting(a[1]))args[2]=a[1]=="window_border_radius"?QString::number(windowBorderRadius(a[2])):insertFeedbackColor(a[2]);
            return {0,checked(call,args),{}};
        }
        if(domain=="query") {
            need(a[1]=="--windows","this query domain is not implemented yet");
            QString properties,selector;bool single=false;int index=2;
            if(index<a.size()&&!a[index].startsWith("--"))properties=a[index++];
            if(index<a.size()){need(a[index++]=="--window","this query filter is not implemented yet");single=true;selector=index<a.size()?a[index++]:"focused";}
            need(index==a.size(),"unexpected query arguments");
            auto doc=QJsonDocument::fromJson(checked(call,{"compat-windows"}).toUtf8());need(doc.isArray(),"invalid window query reply");auto windows=doc.array();for(int i=0;i<windows.size();++i){auto w=windows[i].toObject();w.remove("kwabai-output");windows[i]=w;}QJsonValue result=single?QJsonValue(selected(windows,selector)):QJsonValue(windows);
            if(!properties.isEmpty()) {
                const QStringList supported{"is-modal","is-transient","id","app","title","frame","has-focus","is-floating","is-minimized","is-native-fullscreen","has-fullscreen-zoom","layer","sub-layer","scratchpad","scratchpad-hidden"};auto fields=properties.split(',');for(auto field:fields)need(supported.contains(field),"unsupported window query property");
                auto filter=[&](QJsonObject w){QJsonObject out;for(auto field:fields)out[field]=w[field];return out;};
                if(single)result=filter(result.toObject());else {QJsonArray out;for(auto v:windows)out.append(filter(v.toObject()));result=out;}
            }
            return {0,QString::fromUtf8((single?QJsonDocument(result.toObject()):QJsonDocument(result.toArray())).toJson()),{}};
        }
        if(domain=="space") {
            const auto command=a[1];QString value;
            if(command=="--rotate") {need(a.size()==3&&(a[2]=="90"||a[2]=="180"||a[2]=="270"),"expected space --rotate 90|180|270");value=a[2];}
            else if(command=="--mirror") {need(a.size()==3&&(a[2]=="x-axis"||a[2]=="y-axis"),"expected space --mirror x-axis|y-axis");value=a[2];}
            else if(command=="--equalize") {need(a.size()==2||(a.size()==3&&(a[2]=="x-axis"||a[2]=="y-axis")),"expected space --equalize [x-axis|y-axis]");value=a.size()==2?"both":a[2];}
            else if(command=="--gap"||command=="--padding") {need(a.size()==3,"expected space --gap abs|rel:INTEGER or --padding abs|rel:TOP:BOTTOM:LEFT:RIGHT");parseSpacing(a[2],command=="--gap"?1:4);value=a[2];}
            else throw std::invalid_argument("this space command or selector is not implemented yet");
            wait(call,object(call,{"compat-space",command.mid(2),value}),false);return {};
        }
        if(domain=="window") {
            int index=1;QString source="focused";
            if(index<a.size()&&!a[index].startsWith("--"))source=a[index++];
            need(index<a.size(),"window command is missing");auto command=a[index++];
            if(command=="--scratchpad"||(command=="--toggle"&&index+1==a.size()&&!QStringList{"float","split","zoom-fullscreen"}.contains(a[index]))) {
                need(index==a.size()||index+1==a.size(),"expected window [ID] --scratchpad [LABEL|recover] or window --toggle LABEL");
                auto value=index<a.size()?a[index]:QString{};
                const auto op=command=="--toggle"?QString("toggle"):value=="recover"?QString("recover"):value.isEmpty()?QString("clear"):QString("assign");
                if(op=="toggle"||op=="assign")need(!value.isEmpty()&&value.size()<=256&&!value.contains(QChar(0))&&!QStringList{"recover","float","sticky","pip","shadow","split","zoom-parent","zoom-fullscreen","windowed-fullscreen","native-fullscreen","expose"}.contains(value),"invalid or reserved scratchpad label");
                bool valid=false;auto number=source.toULongLong(&valid);need(source=="focused"||(valid&&number>0&&number<=9007199254740991ULL&&QString::number(number)==source),"scratchpad selector must be a numeric window ID");
                QElapsedTimer queued;queued.start();
                for(;;){
                    auto response=call({"compat-scratchpad",op,source,value});
                    if(response["status"].toInt()!=0){
                        if(response["stderr"].toString().contains("another scratchpad request is pending")&&queued.elapsed()<5000){QThread::msleep(20);continue;}
                        throw std::runtime_error(response["stderr"].toString().trimmed().toStdString());
                    }
                    auto document=QJsonDocument::fromJson(response["stdout"].toString().toUtf8());need(document.isObject(),"invalid scratchpad receipt");auto receipt=document.object();
                    while(!receipt["completed"].toBool()){
                        need(queued.elapsed()<5000,"scratchpad outcome uncertain; inspect bridge scratchpad-status before retrying");QThread::msleep(20);receipt=object(call,{"bridge","scratchpad-status",receipt["id"].toString()});
                    }
                    if(receipt["state"]=="rejected"&&receipt["error"]=="scratchpad layout release pending"&&queued.elapsed()<5000){QThread::msleep(20);continue;}
                    wait(call,receipt,false);break;
                }return {};
            }
            if(command=="--grid"){
                need(index+1==a.size(),"expected window [ID] --grid ROWS:COLS:X:Y:WIDTH:HEIGHT");parseGrid(a[index]);
                bool ok=false;auto id=source.toULongLong(&ok);need(source=="focused"||(ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"grid selector must be a numeric window ID");
                wait(call,object(call,{"compat-grid",source,a[index]}),false);return {};
            }
            if(command=="--layer"||command=="--sub-layer") {
                bool ok=false;auto id=source.toULongLong(&ok);need(source=="focused"||(ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"this layer selector is not implemented yet");
                need(index+1==a.size()&&QStringList{"above","normal","below","auto"}.contains(a[index]),"expected window [ID] --sub-layer below|normal|above|auto");
                QElapsedTimer queued;queued.start();QJsonObject reply;
                for(;;){reply=call({"compat-layer",source,a[index]});if(reply["status"].toInt()==0)break;
                    if(!reply["stderr"].toString().contains("another layer request is pending")||queued.elapsed()>=5000)throw std::runtime_error(reply["stderr"].toString().trimmed().toStdString());
                    QThread::msleep(20);}
                auto doc=QJsonDocument::fromJson(reply["stdout"].toString().toUtf8());need(doc.isObject(),"invalid layer receipt");wait(call,doc.object(),false);return {};
            }
            if(command=="--space") {
                need(index+1==a.size(),"expected window [ID] --space KDE_DESKTOP_INDEX");bool ok=false;auto target=a[index].toUInt(&ok);need(ok&&target>0&&QString::number(target)==a[index],"only positive KDE desktop indexes are supported");
                auto id=source.toULongLong(&ok);need(source=="focused"||(ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"only numeric window IDs are supported for desktop movement");
                wait(call,object(call,{"context","transfer-desktop",source,a[index]}),false);return {};
            }
            if(command=="--display") {
                need(index+1==a.size()&&(a[index]=="north"||a[index]=="east"||a[index]=="south"||a[index]=="west"),"expected window [ID] --display north|east|south|west; other display selectors are not implemented yet");
                bool ok=false;auto id=source.toULongLong(&ok);need(source=="focused"||(ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"only numeric window IDs are supported for output movement");
                wait(call,object(call,{"context","transfer-output",source,"direction:"+a[index]}),false);return {};
            }
            if(command=="--resize") {
                need(index+1==a.size(),"expected window [ID] --resize HANDLE:DX:DY");
                bool ok=false;auto id=source.toULongLong(&ok);need(source=="focused"||(ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"this resize source selector is not implemented yet");
                parseEdgeResize(a[index]);wait(call,object(call,{"compat-resize",source,a[index]}),false);return {};
            }
            if(command=="--ratio") {
                need(index+1==a.size(),"expected window [ID] --ratio abs:VALUE|rel:DELTA");
                bool numeric=false;auto id=source.toULongLong(&numeric);
                need(source=="focused"||(numeric&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"this ratio source selector is not implemented yet");
                auto parts=a[index].split(':');bool valid=false;double value=parts.size()==2?parts[1].toDouble(&valid):0;
                need(parts.size()==2&&(parts[0]=="abs"||parts[0]=="rel")&&valid&&std::isfinite(value),"expected abs:VALUE or rel:DELTA with a finite number");
                wait(call,object(call,{"compat-ratio",source,a[index]}),false);return {};
            }
            if(command=="--swap") {
                need(index+1==a.size(),"expected window [ID] --swap DIRECTION|ID");
                auto numeric=[](const QString &value){bool ok=false;auto id=value.toULongLong(&ok);return ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==value;};
                auto target=a[index];
                need(source=="focused"||numeric(source),"this swap source selector is not implemented yet");
                need(numeric(target)||target=="north"||target=="east"||target=="south"||target=="west","this swap target selector is not implemented yet");
                wait(call,object(call,{"compat-swap",source,target}),false);return {};
            }
            if(command=="--focus") {
                auto numeric=[](const QString &value){bool ok=false;auto id=value.toULongLong(&ok);return ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==value;};
                need(source=="focused"||numeric(source),"this focus source selector is not implemented yet");
                need(index==a.size()||(source=="focused"&&index+1==a.size()),"expected window --focus [ID|DIRECTION] or window ID --focus");
                auto target=index<a.size()?a[index]:source;
                const bool directional=target=="north"||target=="east"||target=="south"||target=="west";
                need(directional||target=="focused"||numeric(target),"this focus selector is not implemented yet");
                wait(call,object(call,{"context",directional?"focus":"focus-window","focused",target}),true);return {};
            }
            if(command=="--toggle"&&index+1==a.size()&&(a[index]=="split"||a[index]=="zoom-fullscreen")) {
                bool ok=false;auto id=source.toULongLong(&ok);need(source=="focused"||(ok&&id>0&&id<=9007199254740991ULL&&QString::number(id)==source),"this toggle source selector is not implemented yet");
                wait(call,object(call,{a[index]=="split"?"compat-split":"compat-zoom",source,a[index]}),false);return {};
            }
            need(command=="--toggle"&&index+1==a.size()&&a[index]=="float","this window command is not implemented yet");
            auto doc=QJsonDocument::fromJson(checked(call,{"compat-windows"}).toUtf8());need(doc.isArray(),"invalid window query reply");auto w=selected(doc.array(),source);
            auto receipt=object(call,{"context","float",w["kwabai-output"].toString(),QString::number(quint64(w["id"].toDouble())),w["is-floating"].toBool()?"off":"on"});
            if(receipt["unchanged"].toBool()&&w["is-floating"].toBool())receipt=object(call,{"context","apply",w["kwabai-output"].toString()});
            if(receipt.contains("completed"))wait(call,receipt,false);
            return {};
        }
        if(domain=="rule") {
            if(a==QStringList{"rule","--list"})return {0,checked(call,{"rule","list-posix"}),{}};
            if(a.size()==3&&a[1]=="--remove"){checked(call,{"rule","remove-posix",a[2]});return {};}
            need(a[1]=="--add"&&a.size()>=3,"this rule operation is not implemented yet");
            QMap<QString,QString> fields;for(int i=2;i<a.size();++i){auto eq=a[i].indexOf('=');need(eq>0,"rule properties must be key=value");auto key=a[i].left(eq);if(key=="sub-layer")key="layer";need(key=="app"||key=="title"||key=="manage"||key=="label"||key=="layer"||key=="scratchpad"||key=="grid","unsupported rule property");need(!fields.contains(key),"duplicate rule property");fields[key]=a[i].mid(eq+1);}
            need(!fields.contains("manage")||fields["manage"]=="on"||fields["manage"]=="off","manage must be on or off");
            need(fields.contains("manage")||fields.contains("layer")||fields.contains("scratchpad")||fields.contains("grid"),"a manage, layer, scratchpad or grid action is required");
            need(!fields.contains("layer")||QStringList{"below","normal","above","auto"}.contains(fields["layer"]),"invalid rule layer");
            QStringList rule{"rule","add-posix",fields.value("label"),fields.contains("manage")?(fields["manage"]=="on"?"tile":"float"):"inherit",fields.value("app"),fields.value("title"),fields.value("layer")};
            if(fields.contains("scratchpad")){need(!fields["scratchpad"].isEmpty(),"scratchpad rule label cannot be empty");rule.append(fields["scratchpad"]);}
            if(fields.contains("grid")){parseGrid(fields["grid"]);if(!fields.contains("scratchpad"))rule.append("");rule.append(fields["grid"]);}checked(call,rule);return {};
        }
        throw std::invalid_argument("this yabai command domain is not implemented yet");
    }catch(const std::exception &e){return {1,{},QString::fromUtf8(e.what())+"\n"};}
}
}
