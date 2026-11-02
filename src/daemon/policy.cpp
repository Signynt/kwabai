#include "manager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <stdexcept>
namespace kwabai {
namespace {
QString policyKey(const QJsonObject &o){return QString::fromUtf8(QJsonDocument(QJsonArray{o["id"],o["current-desktop"],o["current-activity"]}).toJson(QJsonDocument::Compact));}
bool belongs(const QJsonObject &w,const QJsonObject &o){const auto ds=w["desktops"].toArray(),as=w["activities"].toArray();return w["output"]==o["name"]&&(ds.isEmpty()||ds.contains(o["current-desktop"]))&&(as.isEmpty()||as.contains(o["current-activity"]));}
bool eligible(const QJsonObject &w,const QJsonObject &o){return w["scratchpad"].toString().isEmpty()&&w["candidate"].toBool()&&belongs(w,o);}
bool current(const QJsonObject &c,const QJsonObject &o){return c["output-id"]==o["id"]&&c["desktop"]==o["current-desktop"]&&c["activity"]==o["current-activity"];}
}
QJsonObject ContextManager::layoutStatus() const {QJsonObject errors;for(auto it=policyErrors_.begin();it!=policyErrors_.end();++it)errors[it.key()]=it.value();return {{"layout",layout_},{"configuration-paused",configurationPaused_},{"releasing",releaseAll_||!policyReleases_.isEmpty()},{"errors",errors}};}
void ContextManager::setLayout(const QString &layout) {
    if(layout!="bsp"&&layout!="float")throw std::invalid_argument("layout must be bsp or float; stack is not implemented");
    if(bridge_.geometryBusy()||mouseDropBusy())throw std::invalid_argument("wait for pending geometry before changing layout");
    if(!bridge_.state().hasGeometryForget()||!bridge_.state().hasNoRestore())throw std::invalid_argument("layout control requires the updated native effect; run scripts/reload-effect.sh");
    if(layout=="bsp"){
        auto candidate=contexts_;candidate.useGlobalSpacing(bridge_.state());if(candidate.state()!=contexts_.state())persist_(candidate.state());contexts_=std::move(candidate);
        for(auto it=slots_.begin();it!=slots_.end();)if(!it->enabled)it=slots_.erase(it);else {it->lifecycle=true;it->arrivalOnly=false;++it;}
    }else {stop({});contexts_.clearZoom();releaseAll_=true;}
    releaseAll_=true;layout_=layout;policyErrors_.clear();visiblePolicyKeys_.clear();QTimer::singleShot(0,&timer_,[this]{step();});
}
bool ContextManager::stepPolicy() {
    if(configurationPaused_)return true;
    if(layout_!="bsp"&&!releaseAll_&&policyReleases_.isEmpty())return false;
    if(!bridge_.state().connected()||!bridge_.eventStatus()["ready"].toBool())return false;
    for(auto it=policyReleases_.begin();it!=policyReleases_.end();){auto receipt=bridge_.transaction(it.value());if(!receipt["completed"].toBool())return true;
        if(receipt["state"]!="forgotten"){
            policyErrors_[it.key()]=receipt.value("forget-error").toString(receipt["error"].toString());
            it=policyReleases_.erase(it);continue;
        }
        policyErrors_.remove(it.key());
        if(slots_.contains(it.key())){slots_[it.key()].transactionId.clear();slots_[it.key()].resuming=true;}it=policyReleases_.erase(it);
    }
    if(bridge_.geometryBusy())return false;
    const auto outputs=bridge_.state().outputs()["outputs"].toArray();QSet<QString> visible;
    for(auto value:outputs)visible.insert(policyKey(value.toObject()));
    // Inactive trees keep their model but release compositor ownership. This
    // also frees sticky windows before a newly visible context admits them.
    for(auto value:contexts_.state()["contexts"].toArray()){
        auto c=value.toObject();bool active=false;for(auto o:outputs)if(current(c,o.toObject()))active=true;
        const auto id=c["id"].toString();auto control=bridge_.contextStatus(id),job=control["active"].toObject();if(job.isEmpty())continue;
        // Desktop/Activity departures are observable even when KWin marks
        // the now-inactive window as ineligible for geometry control.
        bool moved=false;for(auto o:outputs)if(current(c,o.toObject()))for(auto v:bridge_.state().windows()["windows"].toArray()){auto w=v.toObject();if(job["managed-windows"].toArray().contains(w["uuid"])&&!belongs(w,o.toObject()))moved=true;}
        if(!releaseAll_&&active&&!moved)continue;
        // Native pending work is a temporary release refusal, not a permanent
        // context failure. Keep other displays progressing and retry when idle.
        if(policyErrors_.value(id)=="wait for pending native geometry before resetting a context")policyErrors_.remove(id);
        if(policyErrors_.contains(id)&&!bridge_.state().hasGeometryReconcile())continue;
        policyErrors_.remove(id);
        if((job["state"]!="applied"||!job["success"].toBool())&&job["state"]!="restore_failed"&&job["state"]!="rolled_back"){policyErrors_[id]="finish or reset the unresolved context before changing layout";continue;}
        try {for(auto o:outputs)if(current(c,o.toObject()))visiblePolicyKeys_.remove(policyKey(o.toObject()));auto receipt=bridge_.forgetGeometry(job["id"].toString());policyReleases_[id]=receipt["id"].toString();return true;}
        catch(const std::exception &e){policyErrors_[id]=QString::fromUtf8(e.what());}
    }
    releaseAll_=false;if(layout_!="bsp")return false;
    for(auto value:outputs){auto o=value.toObject();auto key=policyKey(o);if(policyErrors_.contains(key)&&!bridge_.state().hasGeometryReconcile())continue;
        try {
            QJsonObject context;try{context=contexts_.show(bridge_.state(),o["id"].toString());}catch(const std::invalid_argument &){}
            if(!context.isEmpty()&&slots_.contains(context["id"].toString())&&!slots_[context["id"].toString()].enabled)continue;
            bool foreign=false;for(auto old:contexts_.state()["contexts"].toArray())if(old.toObject()["id"]!=context["id"])for(auto m:old.toObject()["members"].toArray())for(auto v:bridge_.state().windows()["windows"].toArray())if(v.toObject()["uuid"]==m.toObject()["uuid"]&&eligible(v.toObject(),o))foreign=true;
            if(context.isEmpty()||!visiblePolicyKeys_.contains(key)||foreign){
                auto candidate=contexts_;candidate.prepareAutomatic(bridge_.state(),o["id"].toString());(void)candidate.plan(bridge_.state(),o["id"].toString(),true);
                if(candidate.state()!=contexts_.state())persist_(candidate.state());
                contexts_=std::move(candidate);context=contexts_.show(bridge_.state(),o["id"].toString());
            }
            auto id=context["id"].toString();if(slots_.contains(id)&&slots_[id].enabled){slots_[id].lifecycle=true;slots_[id].arrivalOnly=false;}if(!slots_.contains(id)||!slots_[id].enabled)enable(o["id"].toString(),true);policyErrors_.remove(key);
        }catch(const std::exception &e){policyErrors_[key]=QString::fromUtf8(e.what());}
    }
    visiblePolicyKeys_=visible;return false;
}
}
