#include "state.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
QString uuid(const QJsonValue &v) {need(v.isString()&&!QUuid(v.toString()).isNull(),"invalid bridge UUID");return QUuid(v.toString()).toString(QUuid::WithoutBraces);}
void geometry(const QJsonValue &v,bool sizeOnly) {
    need(v.isObject(),"invalid bridge geometry");auto r=v.toObject();
    for(auto key:{"w","h"})need(r[key].isDouble()&&std::isfinite(r[key].toDouble())&&r[key].toDouble()>=0,"invalid bridge size");
    if(!sizeOnly) {
        for(auto key:{"x","y"})need(r[key].isDouble()&&std::isfinite(r[key].toDouble()),"invalid bridge origin");
        need(std::isfinite(r["x"].toDouble()+r["w"].toDouble())&&std::isfinite(r["y"].toDouble()+r["h"].toDouble()),"bridge bounds overflow");
    }
}
}
void BridgeState::accept(const QByteArray &json) {
    need(json.size()<=1024*1024,"bridge snapshot exceeds 1 MiB");
    QJsonParseError error;auto doc=QJsonDocument::fromJson(json,&error);
    need(error.error==QJsonParseError::NoError&&doc.isObject(),"invalid bridge JSON");auto s=doc.object();
    need(s.value("protocol")==QJsonValue(1),"unsupported bridge protocol");
    auto instance=uuid(s.value("instance"));
    bool ok=false;auto sequence=s.value("sequence").toString().toULongLong(&ok);
    need(s.value("sequence").isString()&&ok&&sequence>0&&QString::number(sequence)==s.value("sequence").toString(),"invalid bridge sequence");
    need(s.value("kwin-version").isString()&&!s.value("kwin-version").toString().isEmpty(),"missing KWin version");
    need(s.value("capabilities").isArray(),"invalid bridge capabilities");
    auto caps=s.value("capabilities").toArray();QSet<QString> uniqueCaps;
    for(auto v:caps){need(v.isString()&&(v.toString()=="geometry-reconcile"||v.toString()=="snapshot"||v.toString()=="floating-grid"||v.toString()=="scratchpads"||v.toString()=="layers"||v.toString()=="contexts"||v.toString()=="events"||v.toString()=="geometry"||v.toString()=="geometry-update"||v.toString()=="geometry-retention"||v.toString()=="geometry-lifecycle"||v.toString()=="notifications"||v.toString()=="focus"||v.toString()=="geometry-release"||v.toString()=="geometry-no-restore"||v.toString()=="geometry-zoom"||v.toString()=="geometry-hidden-release"||v.toString()=="output-move"||v.toString()=="desktop-move"||v.toString()=="activity-move"||v.toString()=="mouse"||v.toString()=="drop-preview"||v.toString()=="mouse-output-drop"||v.toString()=="geometry-forget"||v.toString()=="mouse-output-preview"||v.toString()=="mouse-output-swap")&&!uniqueCaps.contains(v.toString()),"unsupported or duplicate bridge capability");uniqueCaps.insert(v.toString());}
    need(!uniqueCaps.contains("scratchpads")||(uniqueCaps.contains("contexts")&&uniqueCaps.contains("events")&&uniqueCaps.contains("geometry-release")),"scratchpads require contexts, events and geometry-release");
    need(!uniqueCaps.contains("floating-grid")||uniqueCaps.contains("geometry-retention"),"floating-grid requires geometry-retention");
    need(!uniqueCaps.contains("geometry-zoom")||uniqueCaps.contains("geometry-update"),"geometry-zoom requires geometry-update");
    need(!uniqueCaps.contains("geometry-no-restore")||uniqueCaps.contains("geometry"),"geometry-no-restore requires geometry");
    need(!uniqueCaps.contains("geometry-hidden-release")||uniqueCaps.contains("geometry-release"),"geometry-hidden-release requires geometry-release");
    need(!uniqueCaps.contains("mouse")||(uniqueCaps.contains("geometry-update")&&uniqueCaps.contains("events")&&uniqueCaps.contains("contexts")),"mouse requires geometry-update, events and contexts");
    need(!uniqueCaps.contains("mouse-output-swap")||(uniqueCaps.contains("mouse-output-drop")&&uniqueCaps.contains("output-move")),"mouse-output-swap requires mouse-output-drop and output-move");
    need(!uniqueCaps.contains("mouse-output-preview")||(uniqueCaps.contains("drop-preview")&&uniqueCaps.contains("mouse-output-drop")),"mouse-output-preview requires drop-preview and mouse-output-drop");
    need(!uniqueCaps.contains("geometry-forget")||uniqueCaps.contains("geometry-retention"),"geometry-forget requires geometry-retention");
    need(!uniqueCaps.contains("mouse-output-drop")||(uniqueCaps.contains("mouse")&&uniqueCaps.contains("geometry-release")),"mouse-output-drop requires mouse and geometry-release");
    need(!uniqueCaps.contains("drop-preview")||uniqueCaps.contains("mouse"),"drop-preview requires mouse");
    need(!uniqueCaps.contains("layers")||(uniqueCaps.contains("contexts")&&uniqueCaps.contains("events")),"layers require contexts and events");
    need(uniqueCaps.contains("snapshot"),"snapshot capability missing");
    quint64 eventSequence=0;
    if(uniqueCaps.contains("events")) {
        bool valid=false;auto text=s.value("event-sequence").toString();eventSequence=text.toULongLong(&valid);
        need(s.value("event-sequence").isString()&&valid&&QString::number(eventSequence)==text,"invalid snapshot event cursor");
    }
    int geometryLimit=2;
    if(uniqueCaps.contains("geometry")&&s.contains("geometry-window-limit")){geometryLimit=s["geometry-window-limit"].toInt(-1);need(geometryLimit>=1&&geometryLimit<=20,"invalid geometry window limit");}
    need(!uniqueCaps.contains("geometry-update")||uniqueCaps.contains("geometry"),"geometry-update requires geometry capability");
    need(!uniqueCaps.contains("output-move")||uniqueCaps.contains("geometry-retention"),"output-move requires geometry-retention");
    need(!uniqueCaps.contains("geometry-release")||uniqueCaps.contains("geometry-lifecycle"),"geometry-release requires geometry-lifecycle");
    need(!uniqueCaps.contains("focus")||(uniqueCaps.contains("contexts")&&uniqueCaps.contains("events")),"focus requires contexts and events");
    need(!uniqueCaps.contains("notifications")||uniqueCaps.contains("events"),"notifications require events");
    need(!uniqueCaps.contains("geometry-lifecycle")||uniqueCaps.contains("geometry-retention"),"geometry-lifecycle requires geometry-retention");
    need(!uniqueCaps.contains("geometry-retention")||uniqueCaps.contains("geometry-update"),"geometry-retention requires geometry-update");
    QJsonArray activities;
    if(uniqueCaps.contains("activity-move")) {
        need(uniqueCaps.contains("geometry-release")&&uniqueCaps.contains("contexts")&&uniqueCaps.contains("events"),"activity-move requires release, contexts and events");
        need(s["activities"].isArray()&&s["activities"].toArray().size()<=64,"invalid Activity catalog");QSet<QString> ids;
        for(auto value:s["activities"].toArray()){need(value.isObject(),"invalid Activity entry");auto entry=value.toObject();auto id=uuid(entry["id"]);need(!ids.contains(id)&&(!entry.contains("name")||entry["name"].isString()),"duplicate Activity UUID or invalid name");ids.insert(id);entry["id"]=id;activities.append(entry);}
    }
    QJsonArray desktops;
    if(uniqueCaps.contains("desktop-move")) {
        need(uniqueCaps.contains("geometry-release")&&uniqueCaps.contains("contexts")&&uniqueCaps.contains("events"),"desktop-move requires release, contexts and events");
        need(s["desktops"].isArray()&&!s["desktops"].toArray().isEmpty()&&s["desktops"].toArray().size()<=64,"invalid desktop catalog");
        QSet<QString> ids;QSet<int> indexes;desktops=s["desktops"].toArray();
        for(qsizetype i=0;i<desktops.size();++i){need(desktops[i].isObject(),"invalid desktop entry");auto d=desktops[i].toObject();auto id=uuid(d["id"]);auto index=d["index"].toInt(-1);
            need(d["name"].isString()&&d["index"].isDouble()&&d["index"].toDouble()==index&&index>=1&&index<=desktops.size()&&!ids.contains(id)&&!indexes.contains(index),"invalid or duplicate desktop identity/index");ids.insert(id);indexes.insert(index);d["id"]=id;desktops[i]=d;
        }
    }
    QJsonArray outputs;QSet<QString> names,outputIds;
    if(uniqueCaps.contains("contexts")) {
        need(s.value("outputs").isArray()&&s.value("outputs").toArray().size()<=64,"invalid outputs");
        outputs=s.value("outputs").toArray();
        for(qsizetype i=0;i<outputs.size();++i) {
            auto value=outputs[i];
            need(value.isObject(),"invalid output");auto o=value.toObject();
            auto id=o.value("id").toString(),name=o.value("name").toString();
            need(!id.isEmpty()&&!name.isEmpty()&&!names.contains(name)&&!outputIds.contains(id),"duplicate or empty output identity");names.insert(name);outputIds.insert(id);
            geometry(o.value("geometry"),false);geometry(o.value("work-area"),false);
            need(o.value("scale").isDouble()&&std::isfinite(o.value("scale").toDouble())&&o.value("scale").toDouble()>0,"invalid output scale");
            o["current-desktop"]=uuid(o.value("current-desktop"));need(o.value("current-activity").isString(),"invalid current Activity");
            auto g=o.value("geometry").toObject(),w=o.value("work-area").toObject();
            need(g["w"].toDouble()>0&&g["h"].toDouble()>0&&w["w"].toDouble()>0&&w["h"].toDouble()>0,"empty output or work area");
            constexpr double epsilon=0.001;
            need(w["x"].toDouble()>=g["x"].toDouble()-epsilon&&w["y"].toDouble()>=g["y"].toDouble()-epsilon&&w["x"].toDouble()+w["w"].toDouble()<=g["x"].toDouble()+g["w"].toDouble()+epsilon&&w["y"].toDouble()+w["h"].toDouble()<=g["y"].toDouble()+g["h"].toDouble()+epsilon,"work area exceeds output geometry");
            outputs[i]=o;
        }
    }
    need(s.value("windows").isArray()&&s.value("windows").toArray().size()<=4096,"invalid window collection");
    const auto canonical=QJsonDocument(s).toJson(QJsonDocument::Compact);
    if(instance==instance_) {
        need(sequence>=sequence_,"stale bridge sequence");
        if(uniqueCaps.contains("events")&&capabilities_.contains("events"))need(eventSequence>=eventSequence_,"snapshot event cursor moved backward");
        if(sequence==sequence_)need(canonical==content_,"bridge changed snapshot without advancing sequence");
    }
    auto ids=ids_;auto next=nextId_;QSet<QString> seen;QJsonArray windows;
    for(auto value:s.value("windows").toArray()) {
        need(value.isObject(),"invalid bridge window");auto w=value.toObject();auto native=uuid(w.value("uuid"));
        need(!seen.contains(native),"duplicate bridge window UUID");seen.insert(native);
        for(auto key:{"app","title","output"})need(w.value(key).isString(),"invalid bridge window metadata");
        for(auto key:{"minimized","fullscreen","focused","candidate"})need(w.value(key).isBool(),"invalid bridge window flags");
        for(auto key:{"is-modal","is-transient"})if(w.contains(key))need(w[key].isBool(),"invalid popup metadata");
        if(uniqueCaps.contains("scratchpads"))need(w["scratchpad"].isString()&&w["scratchpad"].toString().size()<=256&&!w["scratchpad"].toString().contains(QChar(0))&&w["scratchpad-hidden"].isBool()&&(!w["scratchpad-hidden"].toBool()||!w["scratchpad"].toString().isEmpty()),"invalid scratchpad metadata");
        if(uniqueCaps.contains("layers"))need(w["layer"].isString()&&(w["layer"]=="above"||w["layer"]=="normal"||w["layer"]=="below"),"invalid window layer");
        if(uniqueCaps.contains("contexts"))need(names.contains(w.value("output").toString()),"window references missing output");
        geometry(w.value("frame"),false);geometry(w.value("minimum"),true);
        for(auto field:{"requested-frame","client-frame","buffer-frame"})if(w.contains(field))geometry(w.value(field),false);
        if(w.contains("interaction")) {
            need(uniqueCaps.contains("mouse")&&uniqueCaps.contains("geometry-update")&&w["interaction"].isObject(),"unsupported mouse interaction");auto g=w["interaction"].toObject();
            uuid(g["id"]);uuid(g["transaction"]);need((g["kind"]=="move"||g["kind"]=="resize")&&(g["state"]=="active"||g["state"]=="finished"),"invalid mouse interaction kind/state");
            geometry(g["start-frame"],false);geometry(g["frame"],false);auto pointer=g["pointer"].toObject();for(auto field:{"x","y"})need(pointer[field].isDouble()&&std::isfinite(pointer[field].toDouble()),"invalid mouse drop point");
        }
        need(w.value("desktops").isArray()&&w.value("activities").isArray(),"invalid window contexts");
        QJsonArray desktops;for(auto d:w.value("desktops").toArray())desktops.append(uuid(d));w["desktops"]=desktops;
        for(auto a:w.value("activities").toArray())need(a.isString(),"invalid Activity identity");
        if(!ids.contains(native)) {need(next<=9007199254740991ULL,"public window ID capacity exhausted");ids[native]=next++;}
        w["uuid"]=native;w["id"]=QString::number(ids[native]);windows.append(w);
    }
    ids_=std::move(ids);nextId_=next;windows_=windows;outputs_=outputs;desktops_=desktops;activities_=activities;capabilities_=caps;instance_=instance;sequence_=sequence;
    geometryLimit_=geometryLimit;eventSequence_=eventSequence;version_=s.value("kwin-version").toString();content_=canonical;connected_=true;enabled_=true;error_.clear();
}
void BridgeState::disconnect(const QString &reason) {enabled_=true;connected_=false;windows_=QJsonArray{};outputs_=QJsonArray{};desktops_=QJsonArray{};activities_=QJsonArray{};error_=reason;}
QJsonObject BridgeState::status() const {
    return {{"enabled",enabled_},{"connected",connected_},{"geometry-capable",hasGeometry()},{"geometry-window-limit",geometryLimit()},{"protocol",1},
        {"instance",instance_},{"sequence",QString::number(sequence_)},{"kwin-version",version_},
        {"window-count",windows_.size()},{"output-count",outputs_.size()},{"event-sequence",QString::number(eventSequence_)},{"error",error_},{"capabilities",connected_?capabilities_:QJsonArray{}}};
}
QJsonObject BridgeState::desktops() const {return {{"protocol",1},{"instance",instance_},{"sequence",QString::number(sequence_)},{"desktops",desktops_}};}
QJsonObject BridgeState::outputs() const {return {{"protocol",1},{"instance",instance_},{"sequence",QString::number(sequence_)},{"outputs",outputs_}};}
QJsonObject BridgeState::windows() const {return {{"protocol",1},{"instance",instance_},{"sequence",QString::number(sequence_)},{"windows",windows_}};}
}

QJsonObject kwabai::BridgeState::activities() const {return {{"protocol",1},{"instance",instance_},{"sequence",QString::number(sequence_)},{"activities",activities_}};}
