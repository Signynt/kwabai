#include "transfers.h"
#include "geometry.h"
#include "desktops.h"
#include <QJsonDocument>
#include <QUuid>
#include <stdexcept>
#include <cmath>
namespace kwabai {
namespace {
void need(bool condition,const char *error){if(!condition)throw std::invalid_argument(error);}
QJsonObject combined(const BridgeState &b){auto s=b.windows();auto status=b.status();for(auto key:{"capabilities","kwin-version","event-sequence","geometry-window-limit"})s[key]=status[key];s["outputs"]=b.outputs()["outputs"];if(b.hasDesktopMove())s["desktops"]=b.desktops()["desktops"];if(b.hasActivityMove())s["activities"]=b.activities()["activities"];return s;}
void applied(const BridgeClient &b,const QJsonObject &c,bool required) {
    auto status=b.contextStatus(c["id"].toString());auto job=status["active"].toObject();
    need(status["active-count"].toInt()<=1&&(!required||!job.isEmpty()),"transfer requires one confirmed source transaction");
    if(!job.isEmpty())need(job["state"]=="applied"&&job["success"].toBool()&&job["bridge-instance"]==b.state().windows()["instance"]&&job["context-generation"]==c["generation"],"finish or recover context transactions before transferring");
}
void framesMatch(const QJsonArray &placements,const BridgeState &b) {
    for(auto value:placements){auto p=value.toObject();QJsonObject observed;for(auto w:b.windows()["windows"].toArray())if(w.toObject()["uuid"]==p["uuid"])observed=w.toObject();
        need(!observed.isEmpty(),"transfer layout member disappeared");
        for(auto field:{"x","y","w","h"})need(std::abs(p["frame"].toObject()[field].toDouble()-observed["frame"].toObject()[field].toDouble())<=1,"transfer layout differs from observed frames; recover before transferring");
    }
}

}
OutputTransfers::OutputTransfers(BridgeClient &bridge,ContextStore &contexts,ContextManager &manager,Execute execute):bridge_(bridge),contexts_(contexts),manager_(manager),execute_(std::move(execute)) {
    timer_.setInterval(20);QObject::connect(&timer_,&QTimer::timeout,&timer_,[this]{step();});
    QObject::connect(&bridge_,&BridgeClient::updated,&timer_,[this]{step();},Qt::QueuedConnection);
}
QJsonObject OutputTransfers::window() const {
    QJsonObject found;for(auto v:bridge_.state().windows()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid_))found=v.toObject();
    need(!found.isEmpty(),"transfer window disappeared");return found;
}
QJsonObject OutputTransfers::status(const QString &id) const {need(jobs_.contains(id),"unknown or expired context transfer in this daemon lifetime");return jobs_[id];}
QJsonObject OutputTransfers::start(const QString &selector,const QString &destination,bool desktop,bool activity) {
    desktop=desktop||activity;const QString membership=activity?"activities":"desktops",current=activity?"current-activity":"current-desktop",field=activity?"activity":"desktop";
    need(!active(),"another context transfer is in progress");
    need((activity?bridge_.state().hasActivityMove():desktop?bridge_.state().hasDesktopMove():bridge_.state().hasOutputMove())&&bridge_.state().hasGeometryRelease()&&bridge_.eventStatus()["ready"].toBool(),"transfer requires the matching movement capability, geometry-release and ready events");
    need(!bridge_.geometryBusy(),"another geometry operation is unresolved; wait for confirmation");
    QJsonObject w,from;int count=0;
    for(auto v:bridge_.state().windows()["windows"].toArray()){auto candidate=v.toObject();if((selector=="focused"&&candidate["focused"].toBool())||candidate["id"]==QJsonValue(selector)||candidate["uuid"]==QJsonValue(selector)){w=candidate;++count;}}
    need(count==1,"transfer window selector is missing or ambiguous");
    for(auto v:bridge_.state().outputs()["outputs"].toArray())if(v.toObject()["name"]==w["output"])from=v.toObject();
    need(!from.isEmpty(),"source output missing");
    auto to=from;QString desktopId;
    if(desktop){desktopId=(activity?activityDestination(bridge_.state().activities()["activities"].toArray(),destination):desktopDestination(bridge_.state().desktops()["desktops"].toArray(),destination))["id"].toString();to[current]=desktopId;}
    else {to=outputDestination(bridge_.state().outputs()["outputs"].toArray(),from,destination);need(to["id"]!=from["id"],"destination is the source output");}
    bool member=false;for(auto v:contexts_.state()["contexts"].toArray())for(auto m:v.toObject()["members"].toArray())if(m.toObject()["uuid"]==w["uuid"])member=true;
    if(!member)return execute_({"context",activity?"send-activity":desktop?"send-desktop":"send-output",w["uuid"].toString(),desktop?desktopId:to["id"].toString()});
    auto source=contexts_.show(bridge_.state(),from["id"].toString());QJsonObject target;
    applied(bridge_,source,true);framesMatch(contexts_.plan(bridge_.state(),from["id"].toString(),true)["placements"].toArray(),bridge_.state());
    auto snapshot=combined(bridge_.state());auto candidate=contexts_;candidate.edit(bridge_.state(),{"float",from["id"].toString(),w["uuid"].toString(),"on"});
    if(desktop) {
        need(activity||w["desktops"]==QJsonValue(QJsonArray{from["current-desktop"]}),"float sticky or multi-desktop tiled windows before changing desktop membership");
        if(to[current]==from[current])return {{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"kind",activity?"activity-transfer":"desktop-transfer"},{"state","unchanged"},{"completed",true},{"success",true},{"acknowledged",true},{"unchanged",true},{"confirmed-window-id",w["id"]},{activity?"confirmed-activity":"confirmed-desktop",desktopId}};
        for(auto v:contexts_.state()["contexts"].toArray()){auto c=v.toObject();if(c["output-id"]==from["id"]&&c["desktop"]==to["current-desktop"]&&c["activity"]==to["current-activity"])target=c;}
        if(!target.isEmpty()){auto status=bridge_.contextStatus(target["id"].toString());auto active=status["active"].toObject();need(status["active-count"].toInt()<=1&&(active.isEmpty()||(active["state"]=="applied"&&active["success"].toBool()&&active["bridge-instance"]==snapshot["instance"])),"inactive destination has an unresolved transaction");}
        QJsonObject move{{"protocol",1},{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"bridge-instance",snapshot["instance"]},{"window",w["uuid"]},{field,desktopId},{"source-output",from},{"expected-desktops",w["desktops"]},{"expected-activities",w["activities"]},{"expected-frame",w["frame"]}};if(activity)validateActivityMove(move,snapshot);else validateDesktopMove(move,snapshot);
        (void)candidate.stageDesktopAdmission(bridge_.state(),from["id"].toString(),w["uuid"].toString(),desktopId,activity);
    } else {
        target=contexts_.show(bridge_.state(),to["id"].toString());applied(bridge_,target,false);
        if(!bridge_.contextStatus(target["id"].toString())["active"].toObject().isEmpty())framesMatch(contexts_.plan(bridge_.state(),to["id"].toString(),true)["placements"].toArray(),bridge_.state());
        auto movement=outputMoveRequest(w,from,to,snapshot,QUuid::createUuid().toString(QUuid::WithoutBraces));validateGeometryRequest(movement,snapshot);
        auto projected=snapshot;auto windows=projected["windows"].toArray();for(qsizetype i=0;i<windows.size();++i){auto next=windows[i].toObject();if(next["uuid"]==w["uuid"]){next["output"]=to["name"];next["frame"]=movement["placements"].toArray()[0].toObject()["frame"];windows[i]=next;}}
        projected["windows"]=windows;BridgeState future;future.accept(QJsonDocument(projected).toJson());candidate.edit(future,{"float",to["id"].toString(),w["uuid"].toString(),"off"});
        for(auto output:{from["id"].toString(),to["id"].toString()})for(auto v:candidate.plan(future,output,true)["placements"].toArray())need(v.toObject()["visible"].toBool(),"transfers do not support hidden stacks");
    }
    if(jobs_.size()>=32)jobs_.remove(order_.takeFirst());
    desktop_=desktop;activity_=activity;desktopId_=desktopId;
    activeId_=QUuid::createUuid().toString(QUuid::WithoutBraces);order_.append(activeId_);uuid_=w["uuid"].toString();source_=from;destination_=to;sourceId_=from["id"].toString();destinationId_=to["id"].toString();sourceContext_=source["id"].toString();destinationContext_=target["id"].toString();bridgeInstance_=snapshot["instance"].toString();
    jobs_[activeId_]={{"id",activeId_},{"kind",activity?"activity-transfer":desktop?"desktop-transfer":"output-transfer"},{"state","releasing"},{"phase","releasing"},{"completed",false},{"success",false},{"acknowledged",false},{"error",""},{"window",uuid_},{"source-output",sourceId_},{"destination-output",destinationId_},{"confirmed-output",to["name"]},{"confirmed-window-id",w["id"]},{"transactions",QJsonArray{}}};
    if(desktop){jobs_[activeId_][activity?"confirmed-activity":"confirmed-desktop"]=desktopId;jobs_[activeId_][activity?"destination-activity":"destination-desktop"]=desktopId;}
    manager_.pauseForTransfer(true);elapsed_.start();timer_.start();
    try {submit("releasing",{"context","float",sourceId_,uuid_,"on"});}catch(const std::exception &e){finish(QString::fromUtf8(e.what()));}
    return jobs_[order_.last()];
}
void OutputTransfers::submit(const QString &phase,const QStringList &command) {
    auto receipt=execute_(command);need(receipt.contains("id")&&receipt.contains("completed"),"transfer stage did not submit a transaction");transactionId_=receipt["id"].toString();transactionKind_=receipt.value("kind").toString();
    auto &job=jobs_[activeId_];job["phase"]=phase;job["state"]=phase;auto transactions=job["transactions"].toArray();transactions.append(transactionId_);job["transactions"]=transactions;auto stages=job["stage-transactions"].toArray();stages.append(QJsonObject{{"phase",phase},{"id",transactionId_},{"kind",(transactionKind_=="desktop-move"||transactionKind_=="activity-move")?transactionKind_:"geometry"}});job["stage-transactions"]=stages;
}
void OutputTransfers::validateContext() const {
    need(bridge_.state().connected()&&bridge_.eventStatus()["ready"].toBool()&&bridge_.state().windows()["instance"]==QJsonValue(bridgeInstance_),"bridge changed during transfer; inspect stage transactions before recovery");
    QJsonObject from,to;for(auto v:bridge_.state().outputs()["outputs"].toArray()){auto o=v.toObject();if(o["id"]==QJsonValue(sourceId_))from=o;if(o["id"]==QJsonValue(destinationId_))to=o;}
    need(from==source_&&(desktop_||to==destination_),"output, desktop, Activity or work area changed during transfer");
}
void OutputTransfers::refresh(const QString &phase){jobs_[activeId_]["phase"]=phase;jobs_[activeId_]["state"]=phase;snapshotRevision_=bridge_.refresh();}
void OutputTransfers::finish(const QString &error) {
    auto &job=jobs_[activeId_];job["state"]=error.isEmpty()?(activity_?"queued-for-activity":desktop_?"queued-for-desktop":"transferred"):"failed";job["completed"]=true;job["success"]=error.isEmpty();job["acknowledged"]=error.isEmpty();job["error"]=error;
    if(!error.isEmpty()){job["recovery"]="Completed stages are retained; inspect transactions and current window output. No automatic retry or reversal.";manager_.stop(sourceContext_,"context transfer failed: "+error);if(!destinationContext_.isEmpty())manager_.stop(destinationContext_,"context transfer failed: "+error);}
    activeId_.clear();timer_.stop();manager_.pauseForTransfer(false);
}
void OutputTransfers::step() {
    if(!active())return;
    try {
        validateContext();need(elapsed_.elapsed()<15000,"context transfer timed out; inspect stage transactions");
        auto phase=jobs_[activeId_]["phase"].toString();
        if(phase.endsWith("snapshot")) {
            if(bridge_.snapshotRevision()<=snapshotRevision_)return;
            if(phase=="release-snapshot"){auto w=window();need(w["output"]==source_["name"],"released window changed output unexpectedly");submit("moving",{"context",activity_?"send-activity":desktop_?"send-desktop":"send-output",uuid_,desktop_?desktopId_:destinationId_});}
            else if(phase=="move-snapshot") {
                if(desktop_){need(window()[activity_?"activities":"desktops"]==QJsonValue(QJsonArray{desktopId_}),"destination desktop membership was not observed");auto context=execute_({"context",activity_?"queue-activity":"queue-desktop",sourceId_,uuid_,desktopId_});destinationContext_=context["id"].toString();jobs_[activeId_]["destination-context"]=destinationContext_;jobs_[activeId_]["admission"]="deferred";submit("updating-source",{"context","apply",sourceId_});}
                else {need(window()["output"]==destination_["name"],"destination output was not observed");submit("admitting",{"context","float",destinationId_,uuid_,"off"});}
            }
            else if(phase=="admission-snapshot")submit("updating-source",{"context","apply",sourceId_});
            else {
                QJsonArray placements;for(auto output:desktop_?QStringList{sourceId_}:QStringList{sourceId_,destinationId_})for(auto p:contexts_.plan(bridge_.state(),output,true)["placements"].toArray())placements.append(p);
                need(window()["output"]==destination_["name"],"window left destination during transfer");framesMatch(placements,bridge_.state());applied(bridge_,contexts_.show(bridge_.state(),sourceId_),true);
                if(desktop_){need(window()[activity_?"activities":"desktops"]==QJsonValue(QJsonArray{desktopId_}),"queued arrival changed desktop membership");bool saved=false;for(auto c:contexts_.state()["contexts"].toArray())if(c.toObject()["id"]==QJsonValue(destinationContext_))for(auto m:c.toObject()["members"].toArray())if(m.toObject()["uuid"]==QJsonValue(uuid_))saved=true;need(saved,"destination membership was not saved");}
                else applied(bridge_,contexts_.show(bridge_.state(),destinationId_),true);
                jobs_[activeId_]["confirmed-placements"]=placements;finish();
            }
            return;
        }
        auto transaction=(transactionKind_=="desktop-move"||transactionKind_=="activity-move")?bridge_.desktopTransaction(transactionId_):bridge_.transaction(transactionId_);if(!transaction["completed"].toBool())return;
        need(transaction["success"].toBool()&&transaction["acknowledged"].toBool(),(transaction["state"].toString()+": "+transaction["error"].toString()).toUtf8().constData());
        if(phase=="releasing")refresh("release-snapshot");else if(phase=="moving")refresh("move-snapshot");else if(phase=="admitting")refresh("admission-snapshot");else refresh("final-snapshot");
    }catch(const std::exception &e){finish(QString::fromUtf8(e.what()));}
}
}
