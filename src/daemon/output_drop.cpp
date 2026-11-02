#include "manager.h"
#include "geometry.h"
#include "mouse_policy.h"
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
QRectF rect(const QJsonValue &value){auto r=value.toObject();return {r["x"].toDouble(),r["y"].toDouble(),r["w"].toDouble(),r["h"].toDouble()};}
bool close(const QJsonValue &a,const QJsonValue &b){auto x=rect(a),y=rect(b);return std::abs(x.x()-y.x())<=1&&std::abs(x.y()-y.y())<=1&&std::abs(x.width()-y.width())<=1&&std::abs(x.height()-y.height())<=1;}
QJsonObject liveWindow(const BridgeState &state,const QString &uuid){for(auto v:state.windows()["windows"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject();throw std::invalid_argument("output drop window disappeared");}
BridgeState projectedView(const BridgeState &state,const QMap<QString,QString> &assignments,bool eligible=false){
    auto snapshot=state.windows(),status=state.status();for(auto key:{"capabilities","kwin-version","event-sequence","geometry-window-limit"})snapshot[key]=status[key];snapshot["outputs"]=state.outputs()["outputs"];if(state.hasDesktopMove())snapshot["desktops"]=state.desktops()["desktops"];if(state.hasActivityMove())snapshot["activities"]=state.activities()["activities"];
    auto windows=snapshot["windows"].toArray();for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();auto uuid=w["uuid"].toString();if(assignments.contains(uuid)){w["output"]=assignments[uuid];if(eligible)w["candidate"]=true;windows[i]=w;}}snapshot["windows"]=windows;BridgeState view;view.accept(QJsonDocument(snapshot).toJson());return view;
}
QJsonObject releaseRequest(const ContextStore &contexts,const BridgeState &state,const QString &output,const QJsonObject &prior,const QJsonObject &window,bool holdPeers){
    auto plan=contexts.plan(state,output,true);
    if(holdPeers){QJsonArray peers;for(auto v:prior["target-placements"].toArray())if(v.toObject()["uuid"]!=window["uuid"])peers.append(v);plan["placements"]=peers;}
    auto request=geometryRequest(plan,state.windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),prior);
    request["supersedes"]=prior["id"];request["lifecycle"]=true;request["release"]=QJsonArray{QJsonObject{{"uuid",window["uuid"]},{"expected-frame",window["frame"]}}};return request;
}
QJsonValue plannedFrame(const QJsonObject &plan,const QString &uuid){for(auto v:plan["placements"].toArray())if(v.toObject()["uuid"]==QJsonValue(uuid))return v.toObject()["frame"];throw std::invalid_argument("swap window missing from final plan");}
}
void ContextManager::beginOutputDrop(Slot &source,const QJsonObject &job,const QJsonObject &window,const QJsonObject &gesture,const QJsonObject &output,bool preview){
    need(!mouseDropBusy(),"another output drop is pending");
    auto point=gesture["pointer"].toObject();QPointF pointer(point["x"].toDouble(),point["y"].toDouble());QJsonObject destination;
    for(auto v:bridge_.state().outputs()["outputs"].toArray()){auto o=v.toObject();auto r=rect(o["geometry"]);if(pointer.x()>=r.left()&&pointer.x()<r.right()&&pointer.y()>=r.top()&&pointer.y()<r.bottom())destination=o;}
    need(!destination.isEmpty()&&destination["current-desktop"]==output["current-desktop"]&&destination["current-activity"]==output["current-activity"],"output drop destination changed desktop or Activity");
    auto target=contexts_.show(bridge_.state(),destination["id"].toString());auto targetId=target["id"].toString();auto it=slots_.find(targetId);
    need(it!=slots_.end()&&it->enabled&&!it->resuming&&it->bridgeInstance==source.bridgeInstance&&it->desktop==source.desktop&&it->activity==source.activity,"enable the destination context before dragging windows to it");
    QJsonObject targetJob;if(!it->transactionId.isEmpty())targetJob=bridge_.transaction(it->transactionId);
    need(targetJob.isEmpty()||(targetJob["state"]=="applied"&&targetJob["success"].toBool()&&targetJob["context-generation"]==target["generation"]&&targetJob["target-work-area"]==destination["work-area"]),"destination output has an unconfirmed layout");validateOwnedFrames(targetJob,false);
    auto uuid=window["uuid"].toString();QString peer,direction;
    for(auto v:targetJob["target-placements"].toArray()){auto p=v.toObject();if(rect(p["frame"]).contains(pointer)){peer=p["uuid"].toString();direction=mouseDropDirection(rect(p["frame"]),pointer);}}
    const bool swap=!peer.isEmpty()&&direction.isEmpty();
    need(!swap||bridge_.state().hasMouseOutputSwap(),"reload the native effect for cross-output center swaps");
    auto released=contexts_;auto originalView=projectedView(bridge_.state(),{{uuid,output["name"].toString()}},preview);released.edit(originalView,{"remove",source.outputId,uuid});
    auto candidate=released;QStringList admission{"add",destination["id"].toString(),uuid};QMap<QString,QString> assignments{{uuid,destination["name"].toString()}};
    if(swap){candidate=contexts_;candidate.exchangeOutputs(bridge_.state(),source.outputId,uuid,destination["id"].toString(),peer);assignments[peer]=output["name"].toString();}
    else {if(!peer.isEmpty())admission<<peer<<direction;auto view=projectedView(bridge_.state(),assignments,preview);candidate.edit(view,admission);}
    auto future=projectedView(bridge_.state(),assignments,preview);auto sourcePlan=candidate.plan(future,source.outputId,true),targetPlan=candidate.plan(future,destination["id"].toString(),true);
    for(auto plan:{sourcePlan,targetPlan})for(auto p:plan["placements"].toArray())need(p.toObject()["visible"].toBool(),"output drops do not support hidden stacks");
    if(preview){
        bridge_.setDropPreview({{"context-id",source.contextId},{"transaction",job["id"]},{"window",window["uuid"]},{"interaction",gesture["id"]},{"context",QJsonObject{{"output-id",output["id"]},{"output",output["name"]},{"desktop",output["current-desktop"]},{"activity",output["current-activity"]}}},{"work-area",output["work-area"]},{"preview-output",destination},{"preview-peers",targetJob["target-placements"].toArray()},{"frame",plannedFrame(targetPlan,uuid)},{"operation",swap?"swap":"insert"},{"direction",swap?QString{}:direction.isEmpty()?"east":direction},{"color",contexts_.insertFeedbackColor()},{"radius",contexts_.windowBorderRadius()}});return;
    }
    auto request=releaseRequest(released,bridge_.state(),source.outputId,job,window,swap);request["interaction-release"]=true;request["interaction"]=QJsonObject{{"id",gesture["id"]},{"window",uuid}};request["drop-output"]=destination;
    persist_(released.state());contexts_=std::move(released);auto receipt=bridge_.submitGeometry(request);source.transactionId=receipt["id"].toString();source.interactionId.clear();source.state="dropping-output";it->state="receiving-output-drop";
    QJsonArray args;for(auto a:admission)args.append(a);if(swap)dropCandidate_=std::move(candidate);else dropCandidate_.reset();
    dropClock_.start();drop_={{"source-id",source.contextId},{"destination-id",targetId},{"source",output},{"destination",destination},{"window",uuid},{"peer",peer},{"peer-endpoint",swap?liveWindow(bridge_.state(),peer)["frame"]:QJsonValue{}},{"endpoint",window["frame"]},{"mode",swap?"swap":"insert"},{"planned-source",sourcePlan},{"planned-target",targetPlan},{"bridge-instance",bridge_.state().windows()["instance"]},{"phase","release-source"},{"transaction",receipt["id"]},{"transactions",QJsonArray{receipt["id"]}},{"admission",args},{"source-generation",contexts_.show(bridge_.state(),source.outputId)["generation"]},{"destination-generation",target["generation"]}};
}
void ContextManager::stepOutputDrop(){
    auto sourceId=drop_["source-id"].toString(),targetId=drop_["destination-id"].toString();
    try{
        need(slots_.contains(sourceId)&&slots_[sourceId].enabled&&slots_.contains(targetId)&&slots_[targetId].enabled,"output drop controller was disabled");need(dropClock_.elapsed()<8000,"output drop timed out; inspect native transactions before recovery");
        need(bridge_.state().windows()["instance"]==drop_["bridge-instance"]&&bridge_.eventStatus()["ready"].toBool(),"bridge changed during output drop");
        for(auto name:{"source","destination"}){auto pinned=drop_[name].toObject();QJsonObject current;for(auto v:bridge_.state().outputs()["outputs"].toArray())if(v.toObject()["id"]==pinned["id"])current=v.toObject();need(current==pinned,"output, desktop, Activity or work area changed during drop");}
        auto &source=slots_[sourceId];auto &target=slots_[targetId];
        need(contexts_.show(bridge_.state(),source.outputId)["generation"]==drop_["source-generation"]&&contexts_.show(bridge_.state(),target.outputId)["generation"]==drop_["destination-generation"],"layout changed during output drop");
        auto receipt=bridge_.transaction(drop_["transaction"].toString());if(!receipt["completed"].toBool())return;
        need(receipt["success"].toBool()&&receipt["acknowledged"].toBool(),(QString("output drop ")+drop_["phase"].toString()+": "+receipt["state"].toString()+" "+receipt["error"].toString()).toUtf8().constData());
        if(!drop_.contains("snapshot")){drop_["snapshot"]=QString::number(bridge_.refresh());return;}if(bridge_.snapshotRevision()<=drop_["snapshot"].toString().toULongLong())return;
        auto phase=drop_["phase"].toString();const bool swap=drop_["mode"]=="swap";auto uuid=drop_["window"].toString(),peer=drop_["peer"].toString();auto from=drop_["source"].toObject(),to=drop_["destination"].toObject();
        auto track=[&](QJsonObject next,const QString &phase){auto timings=drop_["phase-ms"].toObject();timings[drop_["phase"].toString()]=dropClock_.elapsed()-drop_.value("phase-start-ms").toInteger();drop_["phase-ms"]=timings;drop_["phase-start-ms"]=dropClock_.elapsed();drop_["transaction"]=next["id"];drop_["phase"]=phase;drop_.remove("snapshot");auto ids=drop_["transactions"].toArray();ids.append(next["id"]);drop_["transactions"]=ids;};
        auto move=[&](const QString &id,const QJsonObject &origin,const QJsonObject &destination,const QJsonValue &frame,const QString &phase){
            auto w=liveWindow(bridge_.state(),id);need(w["candidate"].toBool(),"released window became ineligible");if(w["output"]==destination["name"])return false;need(w["output"]==origin["name"],"released window changed to an unexpected output");
            auto snapshot=bridge_.state().windows();snapshot["outputs"]=bridge_.state().outputs()["outputs"];auto request=outputMoveRequest(w,origin,destination,snapshot,QUuid::createUuid().toString(QUuid::WithoutBraces));
            if(!frame.isNull()&&!frame.isUndefined()){auto placements=request["placements"].toArray();auto p=placements[0].toObject();p["frame"]=frame;placements[0]=p;request["placements"]=placements;}validateGeometryRequest(request,snapshot);track(bridge_.submitGeometry(request),phase);return true;
        };
        if(phase=="release-source"){
            auto w=liveWindow(bridge_.state(),uuid);need(w["candidate"].toBool()&&close(w["frame"],drop_["endpoint"]),"released drop window changed before admission");validateOwnedFrames(receipt,false);
            QJsonObject prior;if(!target.transactionId.isEmpty())prior=bridge_.transaction(target.transactionId);validateOwnedFrames(prior,false);
            if(swap){
                auto victim=liveWindow(bridge_.state(),peer);need(victim["output"]==to["name"]&&close(victim["frame"],drop_["peer-endpoint"]),"swap target moved before release");auto candidate=contexts_;candidate.edit(bridge_.state(),{"remove",target.outputId,peer});
                auto request=releaseRequest(candidate,bridge_.state(),target.outputId,prior,victim,true);request["release-mode"]="keep-frame";persist_(candidate.state());contexts_=std::move(candidate);auto next=bridge_.submitGeometry(request);target.transactionId=next["id"].toString();drop_["destination-generation"]=contexts_.show(bridge_.state(),target.outputId)["generation"];track(next,"release-target");return;
            }
        }
        if(phase=="release-target"){
            validateOwnedFrames(receipt,false);validateOwnedFrames(bridge_.transaction(source.transactionId),false);auto victim=liveWindow(bridge_.state(),peer);need(close(victim["frame"],drop_["peer-endpoint"]),"swap target changed after release");
            if(move(peer,to,from,plannedFrame(drop_["planned-source"].toObject(),peer),"move-peer"))return;
        }
        if(phase=="move-peer"){
            auto victim=liveWindow(bridge_.state(),peer);need(victim["output"]==from["name"]&&close(victim["frame"],plannedFrame(drop_["planned-source"].toObject(),peer)),"swap peer output/frame was not confirmed");
        }
        if(phase=="release-source"||phase=="release-target"||phase=="move-peer"){
            auto w=liveWindow(bridge_.state(),uuid);need(close(w["frame"],drop_["endpoint"]),"released drop window moved before output handoff");
            if(move(uuid,from,to,swap?plannedFrame(drop_["planned-target"].toObject(),uuid):QJsonValue{},"move-window"))return;
        }
        if(phase=="release-source"||phase=="release-target"||phase=="move-peer"||phase=="move-window"){
            auto w=liveWindow(bridge_.state(),uuid);need(w["candidate"].toBool()&&w["output"]==to["name"],"destination output was not confirmed");validateOwnedFrames(bridge_.transaction(source.transactionId),false);QJsonObject prior;if(!target.transactionId.isEmpty())prior=bridge_.transaction(target.transactionId);validateOwnedFrames(prior,false);
            auto candidate=contexts_;
            if(swap){need(dropCandidate_.has_value(),"missing final swap layout");candidate=*dropCandidate_;(void)candidate.plan(bridge_.state(),source.outputId,true);}
            else {QStringList admission;for(auto a:drop_["admission"].toArray())admission<<a.toString();candidate.edit(bridge_.state(),admission);}
            (void)candidate.plan(bridge_.state(),target.outputId,true);persist_(candidate.state());contexts_=std::move(candidate);dropCandidate_.reset();drop_["source-generation"]=contexts_.show(bridge_.state(),source.outputId)["generation"];drop_["destination-generation"]=contexts_.show(bridge_.state(),target.outputId)["generation"];
            if(swap){auto next=submit(source.outputId,bridge_.transaction(source.transactionId),true);source.transactionId=next["id"].toString();track(next,"apply-source");}
            else {auto next=submit(target.outputId,prior,true);target.transactionId=next["id"].toString();track(next,"apply-target");}return;
        }
        if(phase=="apply-source"){
            validateOwnedFrames(receipt,false);auto next=submit(target.outputId,bridge_.transaction(target.transactionId),true);target.transactionId=next["id"].toString();track(next,"apply-target");return;
        }
        need(phase=="apply-target","unknown output-drop phase");validateOwnedFrames(receipt,false);validateOwnedFrames(bridge_.transaction(source.transactionId),false);source.state=target.state="active";lastDrop_=drop_;auto timings=lastDrop_["phase-ms"].toObject();timings[phase]=dropClock_.elapsed()-drop_.value("phase-start-ms").toInteger();lastDrop_["phase-ms"]=timings;lastDrop_["elapsed-ms"]=dropClock_.elapsed();lastDrop_["state"]="applied";lastDrop_["completed"]=true;lastDrop_["success"]=true;drop_=QJsonObject{};
    }catch(const std::exception &e){lastDrop_=drop_;lastDrop_["elapsed-ms"]=dropClock_.elapsed();lastDrop_["state"]="failed";lastDrop_["completed"]=true;lastDrop_["success"]=false;lastDrop_["error"]=QString::fromUtf8(e.what());drop_=QJsonObject{};dropCandidate_.reset();auto message=QString("output drop failed: ")+QString::fromUtf8(e.what());stop(sourceId,message);stop(targetId,message);}
}
}
