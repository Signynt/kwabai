#include "manager.h"
#include <QDebug>
#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <stdexcept>
#include <cmath>
#include <algorithm>
namespace kwabai {
ContextManager::ContextManager(BridgeClient &bridge,ContextStore &contexts,std::function<void(const QJsonObject &)> persist):persist_(std::move(persist)),bridge_(bridge),contexts_(contexts) {
    QObject::connect(&bridge_,&BridgeClient::updated,&timer_,[this]{step();},Qt::QueuedConnection);
    timer_.setInterval(200);QObject::connect(&timer_,&QTimer::timeout,[this]{step();});timer_.start();
}
QJsonObject ContextManager::describe(const Slot &s) const {
    QJsonArray arrivals;auto pending=s.arrivals.values();std::sort(pending.begin(),pending.end());for(auto id:pending)arrivals.append(id);
    return {{"isolated-windows",s.isolatedWindows},{"recovering-windows",s.recoveryWindows},{"recovery-transaction",s.recoveryId},{"paused-windows",s.pausedWindows},{"interaction",s.interactionId},{"arrivals",arrivals},{"arrival-only",s.arrivalOnly},{"paused-for-transfer",s.enabled&&paused_},{"context-id",s.contextId},{"enabled",s.enabled},{"state",s.state},{"error",s.error},{"transaction-id",s.transactionId},{"output-id",s.outputId},{"desktop",s.desktop},{"activity",s.activity},{"automatic-membership",s.enabled&&s.lifecycle}};
}
QJsonObject ContextManager::status(const QString &id) const {
    if(!id.isEmpty())return slots_.contains(id)?describe(slots_[id]):QJsonObject{{"enabled",false},{"state","disabled"},{"error",""},{"automatic-membership",false}};
    QJsonArray entries;int enabled=0,suspended=0;
    for(const auto &s:slots_){entries.append(describe(s));enabled+=s.enabled;suspended+=s.enabled&&(s.state=="suspended"||s.state=="suspended-window"||s.state=="resuming-window");}
    QJsonObject result=slots_.size()==1?describe(slots_.first()):QJsonObject{{"enabled",enabled>0},{"state",enabled?"managing":"disabled"},{"error",""}};
    result["contexts"]=entries;result["enabled-count"]=enabled;result["suspended-count"]=suspended;return result;
}
void ContextManager::armArrival(const QJsonObject &context,const QString &uuid) {
    const auto id=context["id"].toString();auto current=bridge_.contextStatus(id);auto active=current["active"].toObject();
    if(current["active-count"].toInt()>1||(!active.isEmpty()&&(active["state"]!="applied"||!active["success"].toBool()||active["bridge-instance"]!=bridge_.state().windows()["instance"])))throw std::invalid_argument("inactive destination has an unresolved transaction");
    auto it=slots_.find(id);
    if(it==slots_.end()||!it->enabled){Slot s;s.contextId=id;s.outputId=context["output-id"].toString();s.desktop=context["desktop"].toString();s.activity=context["activity"].toString();s.bridgeInstance=bridge_.state().windows()["instance"].toString();s.enabled=true;s.arrivalOnly=true;s.resuming=true;s.state="suspended";slots_[id]=s;}
    auto &s=slots_[id];if(s.bridgeInstance!=bridge_.state().windows()["instance"].toString())throw std::invalid_argument("destination controller belongs to a replaced bridge");s.transactionId=active["id"].toString();s.arrivals.insert(uuid);
}
void ContextManager::adopt(const QString &id,const QJsonObject &receipt) {
    auto it=slots_.find(id);if(it!=slots_.end()&&it->enabled){it->transactionId=receipt["id"].toString();it->state="waiting";}
}
void ContextManager::stop(const QString &id,const QString &reason) {
    for(auto it=slots_.begin();it!=slots_.end();++it)if(id.isEmpty()||it.key()==id){bridge_.clearDropPreview(it.key());it->enabled=false;it->windowSuspended=false;it->pausedWindows=QJsonArray{};it->resumeClock.invalidate();it->state=reason.isEmpty()?"disabled":"stopped";it->error=reason;}
}
void ContextManager::validateEdit(const QString &output,const QString &operation) const {
    QString id;
    try {id=contexts_.show(bridge_.state(),output)["id"].toString();}
    catch(const std::invalid_argument &){return;} // Creation/selector validation belongs to the edit itself.
    if(!slots_.contains(id)||!slots_[id].enabled)return;
    if(slots_[id].windowSuspended)throw std::invalid_argument("restore minimized/fullscreen windows before editing this paused layout");
    if(operation!="gap"&&operation!="ratio"&&operation!="swap"&&operation!="warp"&&operation!="rotate"&&operation!="mirror"&&operation!="balance"&&operation!="resize"&&operation!="padding"&&operation!="undo"&&operation!="redo")throw std::invalid_argument("managed context permits layout edits; turn management off before changing membership or stacks");
}
QJsonObject ContextManager::submit(const QString &output,const QJsonObject &active,bool lifecycle) {
    auto plan=contexts_.plan(bridge_.state(),output,lifecycle);
    if(lifecycle&&bridge_.state().hasHiddenRelease())plan["release-hidden"]=true;
    for(auto v:plan["placements"].toArray())if(!v.toObject()["visible"].toBool())throw std::invalid_argument("managed stack visibility is unsupported");
    if(lifecycle&&layout_=="bsp"&&bridge_.state().hasGeometryReconcile())plan["reconcile"]=true;
    auto request=geometryRequest(plan,bridge_.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),active);
    if(!active.isEmpty())request["supersedes"]=active["id"];
    if(lifecycle)request["lifecycle"]=true;
    return bridge_.submitGeometry(request);
}
QJsonObject ContextManager::enable(const QString &output,bool lifecycle) {
    if(!bridge_.state().hasNoRestore())throw std::invalid_argument("run scripts/reload-effect.sh to update the effect before managing windows");
    if(!bridge_.state().hasGeometryUpdate())throw std::invalid_argument("management requires the geometry-update bridge");
    if(lifecycle&&!bridge_.state().hasGeometryLifecycle())throw std::invalid_argument("lifecycle management requires geometry-lifecycle; run scripts/reload-effect.sh");
    auto context=contexts_.show(bridge_.state(),output);auto id=context["id"].toString();
    if(slots_.contains(id)&&slots_[id].enabled){if(lifecycle!=slots_[id].lifecycle)throw std::invalid_argument("turn management off before changing lifecycle mode");return status(id);}
    auto current=bridge_.contextStatus(id);
    if(current["active-count"].toInt()>1)throw std::invalid_argument("multiple active context transactions");
    auto active=current["active"].toObject();
    if(!active.isEmpty()&&(active["state"]!="applied"||!active["success"].toBool()||active["bridge-instance"]!=bridge_.state().windows()["instance"]))throw std::invalid_argument("finish the existing transaction or reset the context before managing");
    if(!bridge_.eventStatus()["ready"].toBool())throw std::invalid_argument("management requires a ready event baseline");
    if(lifecycle){reconcile(output);context=contexts_.show(bridge_.state(),output);}
    (void)contexts_.plan(bridge_.state(),output,lifecycle);
    Slot slot;slot.bridgeInstance=bridge_.state().windows()["instance"].toString();slot.contextId=id;slot.outputId=context["output-id"].toString();slot.desktop=context["desktop"].toString();slot.activity=context["activity"].toString();slot.transactionId=active["id"].toString();slot.enabled=true;slot.lifecycle=lifecycle;slot.state="queued";
    if(slots_.contains(id)&&slots_[id].bridgeInstance==slot.bridgeInstance){for(auto value:context["members"].toArray())if(slots_[id].arrivals.contains(value.toObject()["uuid"].toString()))slot.arrivals.insert(value.toObject()["uuid"].toString());}
    slots_[id]=slot;step(slots_[id]);
    return status(id);
}
void ContextManager::reconcile(const QString &output) {
    auto changes=contexts_.reconcile(bridge_.state(),output,bridge_.state().hasHiddenRelease(),layout_=="bsp"&&bridge_.state().hasGeometryReconcile());
    if(!changes["blocked"].toArray().isEmpty()) {
        QStringList details;bool minimized=false,moved=false;
        for(auto value:changes["blocked"].toArray()) {
            auto entry=value.toObject();QString label="window "+entry["id"].toString();
            for(auto w:bridge_.state().windows()["windows"].toArray())if(w.toObject()["uuid"]==entry["uuid"]){auto window=w.toObject();label+=" ("+window["app"].toString().simplified().left(80)+": "+window["title"].toString().simplified().left(100)+")";break;}
            details.append(label+": "+entry["reason"].toString());minimized=minimized||entry["reason"]=="minimized";moved=moved||entry["reason"]=="other-output";
        }
        auto message="managed member is ineligible or changed context: "+details.join("; ");
        if(minimized)message+=". Restore the minimized window(s) from KDE's taskbar, then enable management again";
        if(moved)message+=". To rebuild membership from current locations, run kwabai context reset all, then enable management again";
        throw std::invalid_argument(message.toStdString());
    }
    if(changes["add"].toArray().isEmpty()&&changes["remove"].toArray().isEmpty()&&changes["forget-floating"].toArray().isEmpty())return;
    auto candidate=contexts_;candidate.edit(bridge_.state(),{layout_=="bsp"&&bridge_.state().hasGeometryReconcile()?"reconcile-adaptive":bridge_.state().hasHiddenRelease()?"reconcile-hidden":"reconcile",output});
    auto plan=candidate.plan(bridge_.state(),output,true);
    for(auto v:plan["placements"].toArray())if(!v.toObject()["visible"].toBool())throw std::invalid_argument("managed stack visibility is unsupported");
    persist_(candidate.state());contexts_=std::move(candidate);
}
void ContextManager::validateOwnedFrames(const QJsonObject &job,bool allowClosed) const {
    for(auto value:job["target-placements"].toArray()) {
        auto p=value.toObject();QJsonObject observed;
        for(auto w:bridge_.state().windows()["windows"].toArray())if(w.toObject()["uuid"]==p["uuid"])observed=w.toObject();
        if(observed.isEmpty()&&allowClosed)continue;
        if(observed.isEmpty())throw std::invalid_argument("managed window disappeared while context was suspended");
        for(auto field:{"x","y","w","h"})if(std::abs(p["frame"].toObject()[field].toDouble()-observed["frame"].toObject()[field].toDouble())>1)throw std::invalid_argument("managed window moved while context was suspended or output changed; explicitly recover this context");
    }
}
void ContextManager::step() {
    if(paused_||configurationPaused_)return;
    if(mouseDropBusy()){stepOutputDrop();return;}
    // During a gesture only its owning controller runs. Destination lifecycle
    // admission must not race the source while the pointer crosses outputs.
    for(auto value:bridge_.state().windows()["windows"].toArray()){
        auto gesture=value.toObject()["interaction"].toObject();if(gesture.isEmpty())continue;
        for(auto it=slots_.begin();it!=slots_.end();++it)if(it->enabled&&QJsonValue(it->transactionId)==gesture["transaction"]){step(it.value());return;}
    }
    if(stepPolicy())return;
    for(auto it=slots_.begin();it!=slots_.end();++it)if(it->enabled)step(it.value());
}
bool ContextManager::pauseForWindowState(Slot &s,const QJsonObject &job,const QJsonObject &context,const QJsonObject &output) {
    if(s.lifecycle&&bridge_.state().hasHiddenRelease())return false;
    QMap<QString,QJsonObject> live;for(auto v:bridge_.state().windows()["windows"].toArray())live[v.toObject()["uuid"].toString()]=v.toObject();
    QJsonArray hidden;QSet<QString> hiddenIds;
    for(auto value:context["members"].toArray()) {
        auto uuid=value.toObject()["uuid"].toString();auto w=live.value(uuid);if(w.isEmpty())continue;
        if(!w["minimized"].toBool()&&!w["fullscreen"].toBool())continue;
        auto ds=w["desktops"].toArray(),as=w["activities"].toArray();
        if(w["output"]!=output["name"]||(!ds.isEmpty()&&!ds.contains(output["current-desktop"]))||(!as.isEmpty()&&!as.contains(output["current-activity"])))throw std::invalid_argument("paused window changed output, desktop or Activity; explicitly recover this context");
        hiddenIds.insert(uuid);hidden.append(QJsonObject{{"uuid",uuid},{"id",w["id"]},{"app",w["app"]},{"reason",w["minimized"].toBool()?"minimized":"fullscreen"}});
    }
    if(!hidden.isEmpty()) {
        if(job["state"]!="applied"||job["context-generation"]!=context["generation"]||!s.interactionId.isEmpty())throw std::invalid_argument("window state changed during an unconfirmed layout or mouse edit; explicitly recover this context");
        // Keep native ownership and original baselines. Never configure hidden
        // windows, change the saved tree, or reflow peers during this pause.
        auto peers=job;QJsonArray placements;for(auto p:job["target-placements"].toArray())if(!hiddenIds.contains(p.toObject()["uuid"].toString()))placements.append(p);peers["target-placements"]=placements;validateOwnedFrames(peers,s.lifecycle);
        bridge_.clearDropPreview(s.contextId);s.windowSuspended=true;s.resuming=true;s.resumeClock.invalidate();s.pausedWindows=hidden;s.state="suspended-window";s.error.clear();return true;
    }
    if(!s.windowSuspended)return false;
    if(!s.resumeClock.isValid())s.resumeClock.start();
    try {
        validateOwnedFrames(job,s.lifecycle);
        // Validate the full model before resuming; lifecycle reconciliation may
        // prune closed members or admit windows opened while this output paused.
        if(s.lifecycle) {
            if(!contexts_.reconcile(bridge_.state(),s.outputId)["blocked"].toArray().isEmpty())throw std::invalid_argument("restored window is still ineligible or changed context; explicitly recover this context");
        }else (void)contexts_.plan(bridge_.state(),s.outputId,true);
    }catch(const std::invalid_argument &) {
        // KDE can clear fullscreen before the client's restored geometry lands.
        // Give that bounded transition time without overwriting unexpected frames.
        if(s.resumeClock.elapsed()>=750)throw;
        s.state="resuming-window";return true;
    }
    s.windowSuspended=false;s.pausedWindows=QJsonArray{};s.resumeClock.invalidate();return false;
}
bool ContextManager::recoverChangedWindows(Slot &s,const QJsonObject &job) {
    // A stale replacement is rejected before any native write. Retire the
    // confirmed ownership first, then isolate only clients that independently
    // changed frames. Never replay a resize against an unwilling client.
    if(layout_=="bsp"&&s.lifecycle&&bridge_.state().hasGeometryReconcile()){
        if(job["state"]=="rejected"){
            auto prior=job["previous-id"].toString();s.transactionId=(!prior.isEmpty()&&bridge_.transaction(prior)["state"]=="applied")?prior:QString{};
            s.state="retrying-layout";s.error=job["error"].toString();s.retries=std::min(s.retries+1,10);s.retryClock.start();return true;
        }
        if((job["state"]=="restore_failed"||job["state"]=="rolled_back")&&!bridge_.geometryBusy()){
            s.recoveryId=bridge_.forgetGeometry(job["id"].toString())["id"].toString();s.recoveryWindows=QJsonArray{};s.state="recovering-window";return true;
        }
    }
    if(layout_!="bsp"||!s.lifecycle||job["state"]!="rejected"||job["error"]!="window moved after previous transaction")return false;
    auto priorId=job["previous-id"].toString();if(priorId.isEmpty())return false;
    auto prior=bridge_.transaction(priorId);if(prior["state"]!="applied"||!prior["success"].toBool())return false;
    if(bridge_.geometryBusy()){s.state="queued-recovery";return true;}
    auto context=contexts_.show(bridge_.state(),s.outputId);if(context["id"]!=QJsonValue(s.contextId))return false;
    QJsonArray changed;
    for(auto value:prior["target-placements"].toArray()){
        auto placement=value.toObject();QJsonObject window;
        for(auto w:bridge_.state().windows()["windows"].toArray())if(w.toObject()["uuid"]==placement["uuid"])window=w.toObject();
        if(window.isEmpty())continue;
        if(window["interaction"].toObject()["state"]=="active"){s.state="waiting-interaction";return true;}
        bool differs=false;for(auto field:{"x","y","w","h"})if(std::abs(window["frame"].toObject()[field].toDouble()-placement["frame"].toObject()[field].toDouble())>1)differs=true;
        if(!differs)continue;
        // Other-output/desktop and hidden transitions have separate ownership
        // paths. Do not mistake them for a geometry-refusing client.
        auto ds=window["desktops"].toArray(),as=window["activities"].toArray();
        QJsonObject output;for(auto o:bridge_.state().outputs()["outputs"].toArray())if(o.toObject()["id"]==QJsonValue(s.outputId))output=o.toObject();
        if(!window["candidate"].toBool()||window["output"]!=output["name"]||(!ds.isEmpty()&&!ds.contains(s.desktop))||(!as.isEmpty()&&!as.contains(s.activity)))return false;
        changed.append(QJsonObject{{"uuid",window["uuid"]},{"id",window["id"]},{"app",window["app"]},{"reason","window changed its confirmed geometry outside a mouse interaction; left floating"}});
    }
    if(changed.isEmpty())return false;
    auto receipt=bridge_.forgetGeometry(priorId);s.recoveryId=receipt["id"].toString();s.recoveryWindows=changed;s.state="recovering-window";s.error.clear();return true;
}
bool ContextManager::finishWindowRecovery(Slot &s) {
    auto receipt=bridge_.transaction(s.recoveryId);if(!receipt["completed"].toBool()){s.state="recovering-window";return false;}
    if(receipt["state"]!="forgotten"&&receipt["forget-error"]=="wait for pending native geometry before resetting a context"){
        if(!bridge_.geometryBusy())s.recoveryId=bridge_.forgetGeometry(s.recoveryId)["id"].toString();
        s.state="recovering-window";return false;
    }
    if(receipt["state"]!="forgotten")throw std::invalid_argument("window recovery ownership release failed: "+receipt["error"].toString().toStdString());
    auto current=contexts_.show(bridge_.state(),s.outputId);if(current["id"]!=QJsonValue(s.contextId)){s.state="suspended";return false;}
    auto candidate=contexts_;
    for(auto value:s.recoveryWindows){auto entry=value.toObject();bool live=false;for(auto w:bridge_.state().windows()["windows"].toArray())if(w.toObject()["uuid"]==entry["uuid"])live=true;
        if(live)candidate.edit(bridge_.state(),{"float",s.outputId,entry["uuid"].toString(),"on"});
    }
    candidate.prepareAutomatic(bridge_.state(),s.outputId);(void)candidate.plan(bridge_.state(),s.outputId,true);
    if(candidate.state()!=contexts_.state())persist_(candidate.state());contexts_=std::move(candidate);
    qWarning().noquote()<<QString::fromUtf8(QJsonDocument(QJsonObject{{"event","windows_left_floating"},{"context",s.contextId},{"windows",s.recoveryWindows}}).toJson(QJsonDocument::Compact));
    for(auto entry:s.recoveryWindows){if(s.isolatedWindows.size()>=32)s.isolatedWindows.removeFirst();s.isolatedWindows.append(entry);}
    s.recoveryWindows=QJsonArray{};s.recoveryId.clear();s.transactionId.clear();s.interactionId.clear();s.resuming=false;s.error.clear();return true;
}
void ContextManager::adaptGeometry(Slot &s,const QJsonObject &job) {
    if(layout_!="bsp"||!s.lifecycle||!bridge_.state().hasGeometryReconcile()||job["state"]!="applied")return;
    auto before=contexts_.show(bridge_.state(),s.outputId);if(job["context-generation"]!=before["generation"])return;
    auto candidate=contexts_;bool drift=false;
    auto reference=job["target-placements"].toArray();
    if(s.adaptedTransaction!=job["id"].toString()&&job["reconciled"].toBool())reference=job["requested-placements"].toArray();
    for(auto value:reference){auto p=value.toObject();if(p["zoom-fullscreen"].toBool())continue;QJsonObject w;
        for(auto v:bridge_.state().windows()["windows"].toArray())if(v.toObject()["uuid"]==p["uuid"])w=v.toObject();
        if(w.isEmpty()||!w["candidate"].toBool()||!w["interaction"].toObject().isEmpty())continue;
        auto frame=w["frame"].toObject(),old=(s.adaptedTransaction!=job["id"].toString()&&p["tile-frame"].isObject()?p["tile-frame"]:p["frame"]).toObject();
        for(auto target:job["target-placements"].toArray())if(target.toObject()["uuid"]==p["uuid"])for(auto key:{"x","y","w","h"})if(std::abs(target.toObject()["frame"].toObject()[key].toDouble()-frame[key].toDouble())>1)drift=true;
        for(auto axis:{QString("x"),QString("y")}){
            const double delta=frame[axis=="x"?"w":"h"].toDouble()-old[axis=="x"?"w":"h"].toDouble();if(std::abs(delta)<=1)continue;
            for(int side=0;side<2;++side){auto handle=axis=="x"?(side?"left":"right"):(side?"top":"bottom");try{candidate.edit(bridge_.state(),{"resize-edges",s.outputId,p["uuid"].toString(),handle,QString::number(axis=="x"?(side?-delta:delta):0,'g',17),QString::number(axis=="y"?(side?-delta:delta):0,'g',17)});break;}catch(const std::invalid_argument &){} }
        }
    }
    s.adaptedTransaction=job["id"].toString();
    if(drift&&candidate.show(bridge_.state(),s.outputId)["generation"]==before["generation"])candidate.edit(bridge_.state(),{"refresh",s.outputId});
    if(candidate.state()!=contexts_.state()){(void)candidate.plan(bridge_.state(),s.outputId,true);persist_(candidate.state());contexts_=std::move(candidate);}
}
void ContextManager::step(Slot &s) {
    try {
        if(!bridge_.state().connected()||!bridge_.eventStatus()["ready"].toBool()){
            s.state="waiting-bridge";s.error=bridge_.state().status()["error"].toString();return;
        }
        if(!bridge_.state().hasGeometryUpdate())throw std::invalid_argument("geometry-update capability unavailable");
        if(s.lifecycle&&!bridge_.state().hasGeometryLifecycle())throw std::invalid_argument("lifecycle bridge capability unavailable");
        if(bridge_.state().windows()["instance"]!=QJsonValue(s.bridgeInstance))throw std::invalid_argument("bridge instance changed; explicitly enable management after recovery");
        if(s.retryClock.isValid()&&s.retryClock.elapsed()<std::min(2000,s.retries*100)){s.state="retrying-layout";return;}
        if(!s.recoveryId.isEmpty()&&!finishWindowRecovery(s))return;
        QJsonObject job;if(!s.transactionId.isEmpty())job=bridge_.transaction(s.transactionId);auto state=job.value("state").toString();
        if(!job.isEmpty()&&job["bridge-instance"]!=bridge_.state().windows()["instance"])throw std::invalid_argument("bridge instance changed; restoration outcome is unknown");
        const bool waiting=!job.value("completed").toBool()&&(state=="submitting"||state=="pending"||state=="rolling_back"||state=="submitting_forget"||state=="checking_forget");
        if(!job.isEmpty()&&!waiting&&(state!="applied"||!job["success"].toBool())&&recoverChangedWindows(s,job))return;
        if(!job.isEmpty()&&!waiting&&(state!="applied"||!job["success"].toBool()))throw std::invalid_argument((QString("transaction ")+state+": "+job["error"].toString()).toStdString());
        QJsonObject output;for(auto v:bridge_.state().outputs()["outputs"].toArray())if(v.toObject()["id"]==QJsonValue(s.outputId))output=v.toObject();
        if(output.isEmpty()||output["current-desktop"]!=QJsonValue(s.desktop)||output["current-activity"]!=QJsonValue(s.activity)) {
            bridge_.clearDropPreview(s.contextId);s.state="suspended";s.resuming=true;s.error=output.isEmpty()?"output disconnected":"desktop or Activity inactive";return;
        }
        if(waiting){s.state="waiting";return;}
        auto context=contexts_.show(bridge_.state(),s.outputId);
        if(context["id"]!=QJsonValue(s.contextId))throw std::invalid_argument("context identity changed");
        if(pauseForWindowState(s,job,context,output))return;
        if(!job.isEmpty()&&interact(s,job,output))return;
        const bool adaptive=layout_=="bsp"&&s.lifecycle&&bridge_.state().hasGeometryReconcile();
        if(adaptive){reconcile(s.outputId);context=contexts_.show(bridge_.state(),s.outputId);}
        adaptGeometry(s,job);context=contexts_.show(bridge_.state(),s.outputId);
        const bool areaChanged=!job.isEmpty()&&job["target-work-area"]!=output["work-area"];
        if(s.resuming||areaChanged) {
            if(!(layout_=="bsp"&&s.lifecycle&&bridge_.state().hasGeometryReconcile()))validateOwnedFrames(job,s.lifecycle);
            // Validate membership/context as well as frames before resuming writes.
            if(!s.lifecycle)(void)contexts_.plan(bridge_.state(),s.outputId,true);
        }
        for(auto uuid:s.arrivals){bool live=false;for(auto w:bridge_.state().windows()["windows"].toArray())if(w.toObject()["uuid"]==QJsonValue(uuid))live=true;if(!live)throw std::invalid_argument("queued desktop arrival closed before admission; inspect saved destination membership");}
        if(s.lifecycle&&!adaptive){reconcile(s.outputId);context=contexts_.show(bridge_.state(),s.outputId);}
        if(areaChanged&&job["context-generation"]==context["generation"]) {
            auto candidate=contexts_;candidate.edit(bridge_.state(),{"refresh",s.outputId});
            (void)candidate.plan(bridge_.state(),s.outputId,true);persist_(candidate.state());contexts_=std::move(candidate);context=contexts_.show(bridge_.state(),s.outputId);
        }
        s.resuming=false;s.error.clear();
        if(job.isEmpty()&&context["members"].toArray().isEmpty()){s.state="idle";return;}
        if(job["context-generation"]==context["generation"]){
            for(auto uuid:s.arrivals)if(!job["managed-windows"].toArray().contains(uuid))throw std::invalid_argument("queued arrival is absent from confirmed native ownership");
            s.arrivals.clear();s.retries=0;s.retryClock.invalidate();s.state="active";if(s.arrivalOnly)stop(s.contextId);return;
        }
        if(bridge_.geometryBusy()){s.state="queued";return;}
        auto receipt=submit(s.outputId,job,s.lifecycle||!s.arrivals.isEmpty());s.transactionId=receipt["id"].toString();s.state="waiting";
    }catch(const std::exception &e){
        if(layout_=="bsp"&&s.lifecycle&&bridge_.state().hasGeometryReconcile()&&bridge_.state().windows()["instance"]==QJsonValue(s.bridgeInstance)){
            s.state="retrying-layout";s.error=QString::fromUtf8(e.what());s.retries=std::min(s.retries+1,10);s.retryClock.start();
        }else stop(s.contextId,QString::fromUtf8(e.what()));
    }
}
}
