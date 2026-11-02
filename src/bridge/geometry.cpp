#include "../compat/color.h"
#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
QRectF frame(const QJsonValue &value) {
    need(value.isObject(),"invalid geometry frame");auto o=value.toObject();
    for(auto key:{"x","y","w","h"})need(o[key].isDouble()&&std::isfinite(o[key].toDouble()),"invalid geometry coordinate");
    QRectF r(o["x"].toDouble(),o["y"].toDouble(),o["w"].toDouble(),o["h"].toDouble());
    need(r.width()>0&&r.height()>0&&std::isfinite(r.right())&&std::isfinite(r.bottom()),"invalid geometry extent");return r;
}
bool close(const QRectF &a,const QRectF &b){return std::abs(a.x()-b.x())<=1&&std::abs(a.y()-b.y())<=1&&std::abs(a.width()-b.width())<=1&&std::abs(a.height()-b.height())<=1;}
bool terminal(const QString &s){return s=="forgotten"||s=="moved"||s=="applied"||s=="restored"||s=="rolled_back"||s=="restore_failed"||s=="superseded";}
}
bool GeometryTransactions::tiles(const QString &uuid) const {
    for(const auto &t:transactions_)if(!t.floatingFrame&&t.state!="restored"&&t.state!="rolled_back"&&t.state!="superseded"&&t.state!="moved"&&t.state!="forgotten")for(const auto &w:t.windows)if(w.uuid==uuid&&!w.released)return true;
    return false;
}
bool GeometryTransactions::zoomed(const QString &uuid) const {
    for(const auto &t:transactions_)if(!t.floatingFrame&&t.state!="restored"&&t.state!="rolled_back"&&t.state!="superseded"&&t.state!="moved"&&t.state!="forgotten")for(const auto &w:t.windows)if(w.uuid==uuid&&!w.released&&w.zoomed)return true;
    return false;
}
void GeometryTransactions::beginInteraction(const QString &uuid,const QString &kind,qint64 now) {
    interactions_.remove(uuid);
    if(pending()||(kind!="move"&&kind!="resize"))return;
    auto actual=read_(uuid);if(!actual)return;
    for(auto it=transactions_.cbegin();it!=transactions_.cend();++it)if(it->state=="applied")for(auto w:it->windows)if(w.uuid==uuid&&!w.released&&close(*actual,w.target)) {
        for(auto old=interactions_.begin();old!=interactions_.end();)if(!transactions_.contains(old->previous)||transactions_[old->previous].state!="applied"||(now-old->since>5000&&old->state=="finished"))old=interactions_.erase(old);else ++old;
        if(interactions_.size()>=64)return;
        Interaction gesture;gesture.id=QUuid::createUuid().toString(QUuid::WithoutBraces);gesture.previous=it.key();gesture.kind=kind;gesture.start=gesture.end=*actual;gesture.since=now;interactions_[uuid]=gesture;return;
    }
}
void GeometryTransactions::finishInteraction(const QString &uuid,const QPointF &pointer,qint64 now) {
    auto it=interactions_.find(uuid);if(it==interactions_.end()||it->state!="active")return;
    auto actual=read_(uuid);if(!actual){interactions_.erase(it);return;}it->end=*actual;it->pointer=pointer;it->state="finished";it->since=now;
}
QJsonObject GeometryTransactions::interaction(const QString &uuid,qint64 now) const {
    auto it=interactions_.constFind(uuid);if(it==interactions_.cend()||(it->state=="finished"&&now-it->since>5000))return {};
    auto prior=transactions_.constFind(it->previous);if(prior==transactions_.cend()||prior->state!="applied")return {};
    auto json=[](QRectF r){return QJsonObject{{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};};
    return {{"id",it->id},{"transaction",it->previous},{"kind",it->kind},{"state",it->state},{"start-frame",json(it->start)},{"frame",json(it->end)},{"pointer",QJsonObject{{"x",it->pointer.x()},{"y",it->pointer.y()}}}};
}
QJsonObject GeometryTransactions::dropPreview(const QJsonObject &snapshot,qint64 now) const {
    if(preview_.isEmpty()||now>=previewUntil_||preview_["bridge-instance"]!=snapshot["instance"])return {{"visible",false}};
    auto previous=transactions_.constFind(preview_["transaction"].toString());auto g=interaction(preview_["window"].toString(),now);
    if(previous==transactions_.cend()||previous->owner!=previewOwner_||previous->contextId!=preview_["context-id"].toString()||previous->state!="applied"||g["id"]!=preview_["interaction"]||g["state"]!="active"||g["kind"]!="move")return {{"visible",false}};
    auto context=preview_["context"].toObject();QJsonObject output;
    for(auto v:snapshot["outputs"].toArray())if(v.toObject()["id"]==context["output-id"])output=v.toObject();
    if(output.isEmpty()||output["name"]!=context["output"]||output["current-desktop"]!=context["desktop"]||output["current-activity"]!=context["activity"]||output["work-area"]!=preview_["work-area"])return {{"visible",false}};
    auto destination=preview_["preview-output"].toObject();
    if(!destination.isEmpty()){
        QJsonObject current;for(auto value:snapshot["outputs"].toArray())if(value.toObject()["id"]==destination["id"])current=value.toObject();
        if(current!=destination||destination["id"]==output["id"]||destination["current-desktop"]!=context["desktop"]||destination["current-activity"]!=context["activity"])return {{"visible",false}};
    }
    QMap<QString,QJsonObject> live;for(auto v:snapshot["windows"].toArray())live[v.toObject()["uuid"].toString()]=v.toObject();
    for(auto w:previous->windows)if(!w.released){auto current=live.value(w.uuid);auto ds=current["desktops"].toArray(),as=current["activities"].toArray();
        if(current.isEmpty()||(current["output"]!=context["output"]&&(w.uuid!=preview_["window"].toString()||destination.isEmpty()||current["output"]!=destination["name"]))||current["minimized"].toBool()||current["fullscreen"].toBool()||(!ds.isEmpty()&&!ds.contains(context["desktop"]))||(!as.isEmpty()&&!as.contains(context["activity"])))return {{"visible",false}};
        if(w.uuid==preview_["window"].toString()){auto size=preview_["frame"].toObject(),minimum=current["minimum"].toObject();if(size["w"].toDouble()+0.001<minimum["w"].toDouble()||size["h"].toDouble()+0.001<minimum["h"].toDouble())return {{"visible",false}};}
        auto actual=read_(w.uuid);if(!actual||(w.uuid!=preview_["window"].toString()&&(!current["candidate"].toBool()||!close(*actual,w.target))))return {{"visible",false}};
    }
    for(auto value:preview_["preview-peers"].toArray()){
        auto p=value.toObject(),w=live.value(p["uuid"].toString());auto ds=w["desktops"].toArray(),as=w["activities"].toArray();auto actual=read_(p["uuid"].toString());
        if(w.isEmpty()||!w["candidate"].toBool()||w["output"]!=destination["name"]||(!ds.isEmpty()&&!ds.contains(context["desktop"]))||(!as.isEmpty()&&!as.contains(context["activity"]))||!actual||!close(*actual,frame(p["frame"])))return {{"visible",false}};
    }
    auto result=preview_;result.remove("request-sequence");result.remove("bridge-instance");result["visible"]=true;return result;
}
QJsonObject GeometryTransactions::setDropPreview(const QString &owner,const QJsonObject &r,const QJsonObject &snapshot,qint64 now) {
    need(!owner.isEmpty()&&r["protocol"]==1&&r["bridge-instance"]==snapshot["instance"],"invalid preview caller or bridge instance");
    bool ok=false;auto text=r["request-sequence"].toString();auto sequence=text.toULongLong(&ok);need(ok&&sequence>0&&QString::number(sequence)==text&&sequence>previewSequences_.value(owner),"stale preview sequence");
    need(previewSequences_.contains(owner)||previewSequences_.size()<64,"preview owner capacity exceeded");
    if(r.value("clear")==QJsonValue(true)) {
        previewSequences_[owner]=sequence;
        if(previewOwner_==owner&&(r.value("context-id").toString().isEmpty()||r["context-id"]==preview_["context-id"])){preview_=QJsonObject{};previewOwner_.clear();}
        return dropPreview(snapshot,now);
    }
    need(!r.contains("clear")&&(r["operation"]=="swap"||r["operation"]=="insert"),"invalid drop preview operation");
    need(r["operation"]=="swap"?r.value("direction").toString().isEmpty():QStringList{"north","south","east","west"}.contains(r["direction"].toString()),"invalid drop preview direction");
    if(r.contains("radius")){auto radius=r["radius"];need(radius.isDouble()&&std::isfinite(radius.toDouble())&&std::floor(radius.toDouble())==radius.toDouble()&&radius.toDouble()>=0&&radius.toDouble()<=20,"invalid preview radius");}
    if(r.contains("color")){need(r["color"].isString(),"invalid preview color");compat::insertFeedbackColor(r["color"].toString());}
    if(r.contains("preview-output")){
        need(r["preview-output"].isObject()&&r["preview-peers"].isArray()&&r["preview-peers"].toArray().size()<=20,"invalid preview destination");
        auto destination=r["preview-output"].toObject();need(!destination.isEmpty(),"empty preview destination");(void)frame(destination["work-area"]);
        QSet<QString> seen;for(auto value:r["preview-peers"].toArray()){need(value.isObject(),"invalid preview peer");auto p=value.toObject();auto uuid=p["uuid"].toString();need(!QUuid(uuid).isNull()&&!seen.contains(uuid)&&uuid!=r["window"].toString(),"invalid preview peer identity");seen.insert(uuid);(void)frame(p["frame"]);}
    }else need(!r.contains("preview-peers"),"preview peers require a destination");
    auto area=frame(r.contains("preview-output")?r["preview-output"].toObject()["work-area"]:r["work-area"]),target=frame(r["frame"]);need(area.contains(target),"preview exceeds work area");
    auto old=preview_;auto oldOwner=previewOwner_;auto oldUntil=previewUntil_;preview_=r;previewOwner_=owner;previewUntil_=now+600;
    auto result=dropPreview(snapshot,now);
    if(!result["visible"].toBool()){preview_=old;previewOwner_=oldOwner;previewUntil_=oldUntil;throw std::invalid_argument("preview requires an active owned move in the current context");}
    previewSequences_[owner]=sequence;return result;
}
QJsonObject geometryRequest(const QJsonObject &plan,const QJsonObject &windows,const QString &id,const QJsonObject &active) {
    QMap<QString,QJsonObject> observed;for(auto v:windows["windows"].toArray()){auto w=v.toObject();observed[w["uuid"].toString()]=w;}
    QJsonArray placements;for(auto v:plan["placements"].toArray()){auto p=v.toObject();p["expected-frame"]=observed[p["uuid"].toString()]["frame"];placements.append(p);}
    QJsonObject request{{"protocol",1},{"id",id},{"bridge-instance",plan["bridge-instance"]},{"context",plan["context"]},{"work-area",plan["work-area"]},{"placements",placements}};
    if(plan.contains("zoom-frame"))request["zoom-frame"]=plan["zoom-frame"];
    if(plan.contains("context-generation")){request["context-generation"]=plan["context-generation"];request["context-id"]=plan["context-id"];}
    if(plan["reconcile"].toBool()){request["reconcile"]=true;request["lifecycle"]=true;}
    QSet<QString> selected,floating;for(auto v:placements)selected.insert(v.toObject()["uuid"].toString());for(auto v:plan["floating"].toArray())floating.insert(v.toString());
    QJsonArray release;bool hidden=false,normal=false;
    for(auto v:active["managed-windows"].toArray()){
        auto uuid=v.toString();auto w=observed.value(uuid);if(selected.contains(uuid)||w.isEmpty())continue;
        if(plan["release-hidden"].toBool()&&(w["minimized"].toBool()||w["fullscreen"].toBool())){hidden=true;release.append(QJsonObject{{"uuid",uuid},{"expected-frame",w["frame"]}});}
        else if((plan["reconcile"].toBool()&&!w["candidate"].toBool())||floating.contains(uuid)||!w["scratchpad"].toString().isEmpty()){normal=true;release.append(QJsonObject{{"uuid",uuid},{"expected-frame",w["frame"]}});}
    }
    need(plan["reconcile"].toBool()||!hidden||!normal,"hidden release cannot be mixed with floating release");
    if(hidden)request["release-mode"]="hidden";
    if(!release.isEmpty()){request["release"]=release;request["lifecycle"]=true;}
    return request;
}
QJsonObject outputDestination(const QJsonArray &outputs,const QJsonObject &source,const QString &selector) {
    QJsonObject found;int matches=0;
    if(!selector.startsWith("direction:")) {
        for(auto v:outputs){auto o=v.toObject();if(o["id"]==QJsonValue(selector)||o["name"]==QJsonValue(selector)){found=o;++matches;}}
        need(matches==1,"destination output is missing or ambiguous");return found;
    }
    const auto heading=selector.mid(10);need(heading=="north"||heading=="east"||heading=="south"||heading=="west","invalid output direction");
    auto origin=frame(source["geometry"]).center();double best=std::numeric_limits<double>::infinity();
    for(auto v:outputs){auto o=v.toObject();if(o["id"]==source["id"])continue;auto delta=frame(o["geometry"]).center()-origin;
        const double forward=heading=="east"?delta.x():heading=="west"?-delta.x():heading=="south"?delta.y():-delta.y();if(forward<=0)continue;
        const double distance=delta.x()*delta.x()+delta.y()*delta.y();
        if(distance<best||(distance==best&&o["id"].toString()<found["id"].toString())){best=distance;found=o;}
    }
    need(!found.isEmpty(),"no output in that direction");return found;
}
QJsonObject outputMoveRequest(const QJsonObject &w,const QJsonObject &source,const QJsonObject &destination,const QJsonObject &snapshot,const QString &id) {
    auto from=frame(source["work-area"]),to=frame(destination["work-area"]),old=frame(w["frame"]);
    const double width=std::min(old.width(),to.width()),height=std::min(old.height(),to.height());
    auto minimum=w["minimum"].toObject();need(width>=minimum["w"].toDouble()&&height>=minimum["h"].toDouble(),"destination is smaller than window minimum size");
    const auto fraction=[](double offset,double extent){return extent>0?std::clamp(offset/extent,0.0,1.0):0.5;};
    QJsonObject target{{"x",to.x()+fraction(old.x()-from.x(),from.width()-old.width())*(to.width()-width)},
        {"y",to.y()+fraction(old.y()-from.y(),from.height()-old.height())*(to.height()-height)},{"w",width},{"h",height}};
    return {{"protocol",1},{"id",id},{"bridge-instance",snapshot["instance"]},{"move-output",true},{"source-output",source},
        {"context",QJsonObject{{"output-id",destination["id"]},{"output",destination["name"]},{"desktop",destination["current-desktop"]},{"activity",destination["current-activity"]}}},
        {"work-area",destination["work-area"]},{"placements",QJsonArray{QJsonObject{{"id",w["id"]},{"uuid",w["uuid"]},{"visible",true},{"expected-frame",w["frame"]},{"frame",target}}}}};
}
void validateGeometryRequest(const QJsonObject &r,const QJsonObject &s) {
    need(r["protocol"]==QJsonValue(1)&&!QUuid(r["id"].toString()).isNull(),"invalid geometry protocol or transaction ID");
    need(r["bridge-instance"]==s["instance"],"stale bridge instance");
    const bool moving=r["move-output"]==QJsonValue(true);
    need(!r.contains("move-output")||moving,"invalid output move flag");
    const bool reconcile=r["reconcile"]==QJsonValue(true);
    need(!r.contains("reconcile")||(reconcile&&r["lifecycle"]==QJsonValue(true)&&s["capabilities"].toArray().contains("geometry-reconcile")&&!r.contains("move-output")&&!r.contains("floating-frame")&&!r.contains("interaction")),"invalid automatic reconciliation mode");
    const bool floating=r["floating-frame"]==QJsonValue(true);
    need(!r.contains("floating-frame")||floating,"invalid floating frame flag");
    if(floating)need(!moving&&r["placements"].toArray().size()==1&&!r.contains("source-output")&&!r.contains("supersedes")&&!r.contains("lifecycle")&&!r.contains("release")&&!r.contains("interaction")&&!r.contains("zoom-frame")&&!r.contains("context-id")&&!r.contains("context-generation"),"floating frame accepts one unowned window without layout metadata");
    if(moving)need(r["placements"].toArray().size()==1&&!r.contains("supersedes")&&!r.contains("lifecycle")&&!r.contains("release")&&!r.contains("context-id")&&!r.contains("context-generation"),"output move accepts one unowned floating window without context metadata");
    auto selected=r["placements"].toArray();need(r["placements"].isArray()&&(!selected.isEmpty()||(r["lifecycle"]==QJsonValue(true)&&r.contains("supersedes")))&&selected.size()<=20,"experimental control accepts between one and twenty explicit windows");
    need(r["context"].isObject(),"invalid geometry context");auto context=r["context"].toObject();QJsonObject output;
    for(auto v:s["outputs"].toArray()){auto o=v.toObject();if(o["id"]==context["output-id"])output=o;}
    need(!output.isEmpty()&&output["name"]==context["output"]&&output["current-desktop"]==context["desktop"]&&output["current-activity"]==context["activity"],"output or desktop context changed");
    auto area=frame(output["work-area"]);need(close(area,frame(r["work-area"])),"work area changed");
    QJsonObject source;
    if(moving) {
        for(auto v:s["outputs"].toArray())if(v.toObject()["id"]==r["source-output"].toObject()["id"])source=v.toObject();
        need(!source.isEmpty()&&source==r["source-output"].toObject()&&source["id"]!=output["id"],"source output changed or destination is the source");
    }
    QMap<QString,QJsonObject> windows;for(auto v:s["windows"].toArray()){auto w=v.toObject();windows[w["uuid"].toString()]=w;}
    const bool zooming=r.contains("zoom-frame");QRectF zoomArea;
    if(zooming){need(!moving&&r.contains("context-id")&&r.contains("context-generation"),"zoom requires a tiled context");zoomArea=frame(r["zoom-frame"]);need(area.contains(zoomArea),"zoom exceeds work area");}
    bool hasZoom=false;
    QSet<QString> seen;QList<QRectF> targets;
    for(auto v:selected) {
        need(v.isObject(),"invalid placement");auto p=v.toObject();auto id=p["uuid"].toString();need(windows.contains(id)&&!seen.contains(id),"missing or duplicate selected window");seen.insert(id);auto w=windows[id];
        need(w["candidate"]==QJsonValue(true)&&!w["minimized"].toBool()&&!w["fullscreen"].toBool()&&w["output"]==(moving?source["name"]:output["name"]),"window is no longer eligible");
        auto desktops=w["desktops"].toArray(),activities=w["activities"].toArray();
        need((desktops.isEmpty()||desktops.contains(context["desktop"]))&&(activities.isEmpty()||activities.contains(context["activity"])),"window context changed");
        need(close(frame(p["expected-frame"]),frame(w["frame"])),"selected window moved since planning");auto target=frame(p["frame"]);auto minimum=w["minimum"].toObject();
        need(target.width()+0.001>=minimum["w"].toDouble()&&target.height()+0.001>=minimum["h"].toDouble(),"window minimum size changed");
        need(area.adjusted(-0.001,-0.001,0.001,0.001).contains(target),"target exceeds work area");
        need(!p.contains("zoom-fullscreen")||p["zoom-fullscreen"].isBool(),"invalid zoom flag");
        if(p["zoom-fullscreen"].toBool()){
            need(zooming&&target==zoomArea,"zoom target must equal the padded root area");hasZoom=true;
        }else {
            for(auto other:targets)need(!target.intersects(other),"target rectangles overlap");
            targets.append(target);
        }
    }
    need(zooming==hasZoom,"zoom area requires at least one zoomed placement");
    if(zooming)for(auto target:targets)need(zoomArea.contains(target),"tile exceeds zoom root area");
}
QJsonObject GeometryTransactions::apply(const QString &owner,const QJsonObject &r,const QJsonObject &snapshot,qint64 now) {
    need(!owner.isEmpty(),"geometry requires an identified D-Bus owner");auto id=r["id"].toString();auto digest=QCryptographicHash::hash(QJsonDocument(r).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
    if(transactions_.contains(id)){need(transactions_[id].owner==owner&&transactions_[id].digest==digest,"transaction ID reused");return status(owner,id);}
    bool validSequence=false;auto sequenceText=r["request-sequence"].toString();auto sequence=sequenceText.toULongLong(&validSequence);
    need(validSequence&&sequence>0&&QString::number(sequence)==sequenceText,"canonical positive request-sequence required");
    need(sequence>ownerSequences_.value(owner),"request sequence is stale or its transaction has expired");
    need(ownerSequences_.contains(owner)||ownerSequences_.size()<64,"geometry owner capacity exhausted");
    for(const auto &t:transactions_)need(terminal(t.state),"another geometry operation is pending");
    need(!r.contains("lifecycle")||r["lifecycle"]==QJsonValue(true),"invalid lifecycle flag");
    const bool keepReleased=r.value("release-mode")==QJsonValue("keep-frame");
    const bool hiddenRelease=r.value("release-mode")==QJsonValue("hidden");
    need(!r.contains("release-mode")||((keepReleased||hiddenRelease)&&r["release"].isArray()&&!r["release"].toArray().isEmpty()),"invalid release mode");
    need(!hiddenRelease||(!r.contains("interaction")&&!r.contains("interaction-release")&&!r.contains("move-output")),"hidden release cannot authorize a mouse or output move");
    validateGeometryRequest(r,snapshot);
    const bool dropRelease=r["interaction-release"]==QJsonValue(true);
    need(!r.contains("interaction-release")||dropRelease,"invalid interaction release flag");
    need(!dropRelease||(r.contains("interaction")&&r["lifecycle"]==QJsonValue(true)&&r["release"].toArray().size()==1&&!r.contains("move-output")),"drop release requires a lifecycle replacement with one released interaction window");
    QString gestureWindow;
    if(r.contains("interaction")) {
        auto token=r.value("interaction").toObject();gestureWindow=token.value("window").toString();auto gesture=interaction(gestureWindow,now);
        need(token.size()==2&&!gesture.isEmpty()&&gesture["state"]=="finished"&&token["id"]==gesture["id"]&&r.value("supersedes")==gesture["transaction"]&&(dropRelease||(!r.contains("lifecycle")&&!r.contains("release")))&&!r.contains("move-output"),"invalid, unfinished or expired interaction authorization");
        bool selected=false;for(auto v:r["placements"].toArray())if(v.toObject()["uuid"]==QJsonValue(gestureWindow))selected=true;need(dropRelease?!selected&&r["release"].toArray()[0].toObject()["uuid"]==QJsonValue(gestureWindow):selected,"interaction window must remain tiled or be explicitly released");
        if(dropRelease){
            need(gesture["kind"]=="move","only a completed move can release an output drop");
            QJsonObject destination;for(auto value:snapshot["outputs"].toArray())if(value.toObject()["id"]==r["drop-output"].toObject()["id"])destination=value.toObject();
            auto context=r["context"].toObject();need(!destination.isEmpty()&&destination==r["drop-output"].toObject()&&destination["id"]!=context["output-id"]&&destination["current-desktop"]==context["desktop"]&&destination["current-activity"]==context["activity"],"drop destination changed context");
            auto pointer=gesture["pointer"].toObject();need(frame(destination["geometry"]).contains(QPointF(pointer["x"].toDouble(),pointer["y"].toDouble())),"drop pointer is outside destination output");
        }
    }
    if(r["move-output"]==QJsonValue(true))need(bool(output_),"output observation is required for movement");
    Transaction t;t.reconcile=r.value("reconcile").toBool();t.floatingFrame=r.value("floating-frame")==QJsonValue(true);t.lineage=id;if(r["move-output"]==QJsonValue(true)){t.targetOutput=r["context"].toObject()["output"].toString();t.originalOutput=r["source-output"].toObject()["name"].toString();}t.owner=owner;t.digest=digest;t.since=now;
    if(r.contains("context-id")) {
        t.contextId=r["context-id"].toString();bool ok=false;auto text=r["context-generation"].toString();t.generation=text.toULongLong(&ok);
        need(!QUuid(t.contextId).isNull()&&ok&&t.generation>0&&QString::number(t.generation)==text,"invalid context generation metadata");
    }
    if(r.contains("supersedes")) {
        need(r["supersedes"].isString()&&transactions_.contains(r["supersedes"].toString()),"replacement transaction not found");t.previous=r["supersedes"].toString();const auto &old=transactions_[t.previous];
        need(old.owner==owner&&old.state=="applied","replacement requires an applied transaction from this owner");t.lineage=old.lineage;
        need(!t.contextId.isEmpty()&&old.contextId==t.contextId&&t.generation>old.generation,"replacement requires the same context and a newer generation");
    }
    for(auto v:r["placements"].toArray()){auto p=v.toObject();auto uuid=p["uuid"].toString();auto actual=read_(uuid);need(actual.has_value()&&close(*actual,frame(p["expected-frame"])),"window changed before geometry write");t.windows.append({uuid,frame(p["frame"]),*actual});t.windows.last().zoomed=p["zoom-fullscreen"].toBool();}
    need(!r.contains("release")||(r["release"].isArray()&&!r["release"].toArray().isEmpty()&&r["release"].toArray().size()<=20&&r["lifecycle"]==QJsonValue(true)&&!t.previous.isEmpty()),"release requires a lifecycle replacement and between 1 and 20 windows");
    if(!t.previous.isEmpty()) {
        const auto &old=transactions_[t.previous];const bool lifecycle=r["lifecycle"]==QJsonValue(true);
        QSet<QString> selected,observed;for(auto w:t.windows)selected.insert(w.uuid);
        for(auto v:snapshot["windows"].toArray())observed.insert(v.toObject()["uuid"].toString());
        QSet<QString> released;QMap<QString,QJsonObject> live;for(auto v:snapshot["windows"].toArray())live[v.toObject()["uuid"].toString()]=v.toObject();
        for(auto v:r["release"].toArray()) {
            need(v.isObject(),"invalid released window");auto entry=v.toObject();auto uuid=entry["uuid"].toString();
            need(!selected.contains(uuid)&&!released.contains(uuid)&&live.contains(uuid),"missing, duplicate or still tiled release target");
            auto w=live[uuid];auto context=r["context"].toObject();
            need((t.reconcile|| (hiddenRelease?(w["minimized"].toBool()||w["fullscreen"].toBool()):(w["candidate"]==QJsonValue(true)&&!w["minimized"].toBool()&&!w["fullscreen"].toBool())))&&(w["output"]==context["output"]||(dropRelease&&w["output"]==r["drop-output"].toObject()["name"])),"release target is ineligible or changed output");
            auto ds=w["desktops"].toArray(),as=w["activities"].toArray();need((ds.isEmpty()||ds.contains(context["desktop"]))&&(as.isEmpty()||as.contains(context["activity"])),"release target changed desktop or Activity");
            auto actual=read_(uuid);need(actual&&close(*actual,frame(entry["expected-frame"]))&&close(*actual,frame(w["frame"])),"release target moved since selection");
            bool found=false;for(auto prior:old.windows)if(prior.uuid==uuid){
                need(t.reconcile||hiddenRelease||(dropRelease?uuid==gestureWindow&&close(prior.target,interactions_[uuid].start)&&close(*actual,interactions_[uuid].end):close(*actual,prior.target)),"release target moved after previous transaction");
                t.windows.append({uuid,*actual,*actual,true,true});found=true;
            }
            need(found,"release target is not owned by the previous transaction");released.insert(uuid);
        }
        for(auto prior:old.windows)if(!selected.contains(prior.uuid)&&!released.contains(prior.uuid))need(lifecycle&&!observed.contains(prior.uuid)&&!read_(prior.uuid).has_value(),"live windows require explicit release before leaving a transaction");
        for(auto &w:t.windows){if(w.released)continue;bool found=false;for(auto prior:old.windows)if(prior.uuid==w.uuid){need(t.reconcile||close(w.rollback,prior.target)||(w.uuid==gestureWindow&&close(w.rollback,interactions_[gestureWindow].end)&&close(prior.target,interactions_[gestureWindow].start)),"window moved after previous transaction");found=true;}need(found||lifecycle,"replacement membership changed");}
    }
    for(auto it=transactions_.begin();it!=transactions_.end();++it)if(it.key()!=t.previous&&it->state!="restored"&&it->state!="rolled_back"&&it->state!="superseded"&&it->state!="moved"&&it->state!="forgotten")
        for(auto a:it->windows)for(auto b:t.windows)need(a.uuid!=b.uuid,"release or explicitly update an existing transaction before reusing its windows");
    // A newly mapped client can center itself during its first configure,
    // including when reopening at the saved tile size. Allow one position-only
    // correction after size settles. Existing members and output moves opt out.
    if(r["lifecycle"]==QJsonValue(true)&&!r.contains("move-output"))for(auto &w:t.windows){
        bool existing=false;if(!t.previous.isEmpty())for(const auto &prior:transactions_[t.previous].windows)if(prior.uuid==w.uuid)existing=true;
        w.settlePosition=!existing&&!w.passive;
        if(w.settlePosition)++t.positionCandidates;
    }
    // All validation and request-frame capture finish before the first write.
    makeRoom();if(!gestureWindow.isEmpty())interactions_.remove(gestureWindow);transactions_.insert(id,t);order_.append(id);ownerSequences_[owner]=sequence;
    if(!t.previous.isEmpty()){auto &old=transactions_[t.previous];old.state="superseding";old.replacement=id;
        // Confirmed closed members have no geometry left to restore after rollback.
        if(r["lifecycle"]==QJsonValue(true)){
            for(auto it=old.windows.begin();it!=old.windows.end();){if(!read_(it->uuid))it=old.windows.erase(it);else ++it;}
        }
    }
    auto &stored=transactions_[id];
    // Ownership-only handoffs have no compositor work to wait for. All stale
    // frame, membership and owner checks above still apply before settling.
    bool unchanged=true;
    for(auto w:stored.windows)unchanged=unchanged&&w.rollback==w.target&&(stored.targetOutput.isEmpty()||output_(w.uuid)==stored.targetOutput);
    if(unchanged){confirmApply(stored);return status(owner,id);}
    for(auto w:stored.windows) {
        if(w.passive)continue;
        if(w.rollback==w.target&&(stored.targetOutput.isEmpty()||output_(w.uuid)==stored.targetOutput))continue;
        if(!write_(w.uuid,w.target)){if(stored.reconcile)continue;stored.error="geometry write rejected";rollbackWrites(stored,now);break;}
    }
    return status(owner,id);
}
void GeometryTransactions::rollbackWrites(Transaction &t,qint64 now) {
    t.state="rolling_back";t.since=now;t.matching=0;
    for(auto w:t.windows)if(!w.passive&&read_(w.uuid))write_(w.uuid,w.rollback);
}
QJsonObject GeometryTransactions::forget(const QString &owner,const QString &id) {
    need(!pending(),"wait for pending native geometry before resetting a context");
    need(transactions_.contains(id)&&transactions_[id].owner==owner,"unknown transaction for this owner");auto &t=transactions_[id];
    need(t.state=="applied"||t.state=="restore_failed"||t.state=="rolled_back"||t.state=="restored"||t.state=="forgotten","forget requires a settled active transaction");
    for(auto w:t.windows){auto g=interaction(w.uuid,0);need(!read_(w.uuid).has_value()||g.isEmpty()||g["state"]!="active","finish mouse interaction before resetting");}
    for(auto it=interactions_.begin();it!=interactions_.end();)if(it->previous==id)it=interactions_.erase(it);else ++it;
    if(preview_["transaction"]==QJsonValue(id)){preview_=QJsonObject{};previewOwner_.clear();}
    retirePrevious(t);t.windows.clear();t.state="forgotten";t.error.clear();return status(owner,id);
}
QJsonObject GeometryTransactions::restore(const QString &,const QString &,qint64) {
    throw std::invalid_argument("layout restoration was removed: yabai does not restore pre-tiling positions; use context reset to release stale ownership without moving windows");
}
QJsonObject GeometryTransactions::status(const QString &owner,const QString &id) const {
    need(transactions_.contains(id)&&transactions_[id].owner==owner,"unknown transaction for this owner");const auto &t=transactions_[id];
    QJsonArray accepted;for(auto w:t.windows)if(!w.released)accepted.append(QJsonObject{{"uuid",w.uuid},{"frame",QJsonObject{{"x",w.target.x()},{"y",w.target.y()},{"w",w.target.width()},{"h",w.target.height()}}}});
    QJsonArray managed;int corrections=0;for(auto w:t.windows){if(!w.released)managed.append(w.uuid);corrections+=w.positionCorrected;}
    return {{"reconciled",t.reconcile},{"accepted-placements",accepted},{"position-candidates",t.positionCandidates},{"position-size-matches",t.positionSizeMatches},{"position-corrections",corrections},{"managed-windows",managed},{"id",id},{"lineage",t.lineage},{"retained-records",transactions_.size()},{"record-limit",32},{"previous-id",t.previous},{"replacement-id",t.replacement},{"active-transaction",activeId(id)},{"state",t.state},{"completed",terminal(t.state)},{"success",t.state=="forgotten"||t.state=="moved"||t.state=="applied"||t.state=="restored"},{"acknowledged",t.state=="forgotten"||t.state=="moved"||t.state=="applied"||t.state=="restored"||t.state=="rolled_back"},{"error",t.error}};
}
bool GeometryTransactions::pending() const {for(const auto &t:transactions_)if(!terminal(t.state))return true;return false;}
bool GeometryTransactions::owns(const QString &uuid) const {for(const auto &t:transactions_)if(t.state!="restored"&&t.state!="rolled_back"&&t.state!="superseded"&&t.state!="moved"&&t.state!="forgotten")for(const auto &w:t.windows)if(w.uuid==uuid&&!w.released)return true;return false;}
bool GeometryTransactions::tick(qint64 now) {
    bool changed=false;
    for(auto &t:transactions_) {
        if(terminal(t.state)||t.state=="superseding")continue;
        if(t.reconcile&&t.state=="pending"){
            for(auto it=t.windows.begin();it!=t.windows.end();){
                if(!read_(it->uuid)){it=t.windows.erase(it);changed=true;}else ++it;
            }
        }
        bool matches=true,rejected=false;
        for(auto &w:t.windows){
            if(w.passive)continue;
            auto actual=read_(w.uuid);
            if(t.reconcile&&t.state=="pending"&&actual&&actual->width()>0&&actual->height()>0&&!close(*actual,w.target)){
                w.observedSamples=close(*actual,w.observedFrame)?w.observedSamples+1:1;w.observedFrame=*actual;
                if(w.observedSamples>=3&&now-t.since>=100&&!w.positionCorrected){
                    auto positioned=*actual;positioned.moveTopLeft(w.target.topLeft());w.positionCorrected=true;
                    if(!close(positioned,*actual)){write_(w.uuid,positioned);w.observedSamples=0;continue;}
                }
                if((w.observedSamples>=3&&now-t.since>=180)||now-t.since>=1200){w.target=*actual;changed=true;}
            }
            const bool match=actual.has_value()&&close(*actual,t.state=="pending"?w.target:w.rollback);matches=matches&&match;
            if(t.state!="pending"||!w.settlePosition||w.positionCorrected||!actual||match)continue;
            const bool sized=std::abs(actual->width()-w.target.width())<=1&&std::abs(actual->height()-w.target.height())<=1;
            if(!sized){w.positionSamples=0;continue;}
            ++t.positionSizeMatches;
            w.positionSamples=w.positionSamples&&close(*actual,w.positionSample)?w.positionSamples+1:1;w.positionSample=*actual;
            if(w.positionSamples<3||now-t.since<80||now-t.since>=1200)continue;
            // Keep the acknowledged dimensions, so this is a move rather than
            // another asynchronous resize. The native writer rechecks eligibility.
            auto positioned=*actual;positioned.moveTopLeft(w.target.topLeft());w.positionCorrected=true;changed=true;
            if(!write_(w.uuid,positioned)){t.error="geometry position correction rejected";rollbackWrites(t,now);rejected=true;break;}
        }
        if(rejected)continue;
        if(!t.targetOutput.isEmpty())for(auto w:t.windows)matches=matches&&output_(w.uuid)==(t.state=="pending"?t.targetOutput:t.originalOutput);
        if(matches&&++t.matching>=3){
            changed=true;
            if(t.state=="pending")confirmApply(t);
            else {t.state="rolled_back";for(auto &w:t.windows){w.rollback=QRectF{};w.positionSample=QRectF{};w.positionSamples=0;w.settlePosition=false;}t.originalOutput.clear();if(!t.previous.isEmpty()){auto &prior=transactions_[t.previous];prior.state="applied";prior.replacement.clear();}}
            continue;
        }
        if(!matches)t.matching=0;
        if(t.reconcile&&t.state=="pending"&&now-t.since>=1200){
            for(auto it=t.windows.begin();it!=t.windows.end();){auto actual=read_(it->uuid);if(!actual||actual->width()<=0||actual->height()<=0)it=t.windows.erase(it);else{it->target=*actual;++it;}}
            confirmApply(t);changed=true;continue;
        }
        if(now-t.since>=1200){changed=true;if(t.state=="pending"){t.error="geometry acknowledgement timed out";rollbackWrites(t,now);}else{t.state="restore_failed";t.error+="; request rollback frames could not all be confirmed";}}
    }
    return changed;
}
void GeometryTransactions::confirmApply(Transaction &t) {
    // Raise only newly zoomed windows after confirmed geometry. This is a
    // one-time ordering change, never activation. Automatic layering chooses
    // its priority separately. Reflow must not repeatedly raise it.
    if(raise_&&!t.floatingFrame&&t.targetOutput.isEmpty())for(const auto &w:t.windows)if(w.zoomed&&!w.released){
        bool alreadyZoomed=false;
        auto prior=transactions_.constFind(t.previous);
        if(prior!=transactions_.cend())for(const auto &old:prior->windows)if(old.uuid==w.uuid&&old.zoomed&&!old.released)alreadyZoomed=true;
        if(!alreadyZoomed)raise_(w.uuid);
    }
    retirePrevious(t);t.state=t.targetOutput.isEmpty()&&!t.floatingFrame?"applied":"moved";
    for(auto &w:t.windows){w.rollback=QRectF{};w.positionSample=QRectF{};w.positionSamples=0;w.settlePosition=false;}
    t.originalOutput.clear();
    if(t.state=="moved")t.windows.clear();
    for(auto it=t.windows.begin();it!=t.windows.end();)if(it->released)it=t.windows.erase(it);else ++it;
}
void GeometryTransactions::release(const QString &owner,qint64) {
    ownerSequences_.remove(owner);previewSequences_.remove(owner);
    if(previewOwner_==owner){preview_=QJsonObject{};previewOwner_.clear();}
    for(auto it=transactions_.begin();it!=transactions_.end();)if(it->owner==owner){order_.removeOne(it.key());it=transactions_.erase(it);}else ++it;
    for(auto it=interactions_.begin();it!=interactions_.end();)if(!transactions_.contains(it->previous))it=interactions_.erase(it);else ++it;
}
QString GeometryTransactions::activeId(QString id) const {
    auto found=transactions_.constFind(id);if(found==transactions_.cend())return {};
    for(auto it=transactions_.cbegin();it!=transactions_.cend();++it)if(it->lineage==found->lineage&&it->state!="restored"&&it->state!="rolled_back"&&it->state!="superseded"&&it->state!="superseding"&&it->state!="moved"&&it->state!="forgotten")return it.key();
    return {};
}
void GeometryTransactions::makeRoom() {
    if(transactions_.size()<32)return;
    for(auto id:order_){auto state=transactions_[id].state;if(state=="forgotten"||state=="moved"||state=="restored"||state=="rolled_back"||state=="superseded"){transactions_.remove(id);order_.removeOne(id);return;}}
    throw std::invalid_argument("geometry capacity exhausted by active or unresolved transactions; reset unused contexts");
}
void GeometryTransactions::retirePrevious(Transaction &t){if(!t.previous.isEmpty()&&transactions_.contains(t.previous))transactions_[t.previous].state="superseded";}
void GeometryTransactions::teardown(){transactions_.clear();order_.clear();interactions_.clear();ownerSequences_.clear();previewSequences_.clear();preview_=QJsonObject{};previewOwner_.clear();}
}
