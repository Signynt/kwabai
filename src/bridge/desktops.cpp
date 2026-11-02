#include "desktops.h"
#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool condition,const char *error){if(!condition)throw std::invalid_argument(error);}
bool sameFrame(const QJsonValue &first,const QJsonValue &second){auto a=first.toObject(),b=second.toObject();for(auto f:{"x","y","w","h"})if(!a[f].isDouble()||!b[f].isDouble()||!std::isfinite(a[f].toDouble())||!std::isfinite(b[f].toDouble())||std::abs(a[f].toDouble()-b[f].toDouble())>1)return false;return a["w"].toDouble()>0&&a["h"].toDouble()>0;}
bool terminal(const QString &s){return s=="moved"||s=="rolled_back"||s=="failed"||s=="restore_failed";}
}
QJsonObject desktopDestination(const QJsonArray &catalog,const QString &selector) {
    bool numeric=false;auto number=selector.toUInt(&numeric);numeric=numeric&&number>0&&QString::number(number)==selector;
    const auto uuid=QUuid(selector);QJsonObject found;int count=0;
    for(auto v:catalog){auto d=v.toObject();if((numeric&&d["index"]==QJsonValue(int(number)))||(!uuid.isNull()&&QUuid(d["id"].toString())==uuid)){found=d;++count;}}
    need(count==1,"desktop selector must be an existing KDE desktop index or UUID");return found;
}
namespace {
void validateMembershipMove(const QJsonObject &r,const QJsonObject &s,bool activity) {
    need(r["protocol"]==QJsonValue(1)&&!QUuid(r["id"].toString()).isNull(),"invalid membership movement protocol or ID");
    need(r["bridge-instance"]==s["instance"],"stale membership movement bridge");
    bool target=false;for(auto v:s[activity?"activities":"desktops"].toArray())if(v.toObject()["id"]==r[activity?"activity":"desktop"])target=true;need(target,"destination membership no longer exists");
    auto source=r["source-output"].toObject();QJsonObject output;
    for(auto v:s["outputs"].toArray())if(v.toObject()["id"]==source["id"])output=v.toObject();
    need(!output.isEmpty()&&output==source,"source output, desktop, Activity or work area changed");
    QJsonObject window;for(auto v:s["windows"].toArray())if(v.toObject()["uuid"]==r["window"])window=v.toObject();
    need(!window.isEmpty()&&window["candidate"]==QJsonValue(true)&&!window["minimized"].toBool()&&!window["fullscreen"].toBool()&&window["output"]==source["name"],"membership movement window is missing or ineligible");
    auto ds=window["desktops"].toArray(),as=window["activities"].toArray();
    need(r["expected-desktops"].isArray()&&r["expected-activities"].isArray()&&window["desktops"]==r["expected-desktops"]&&window["activities"]==r["expected-activities"],"window desktop or Activity membership changed");
    need((ds.isEmpty()||ds.contains(source["current-desktop"]))&&(as.isEmpty()||as.contains(source["current-activity"])),"window is not on the current source context");
    need(sameFrame(window["frame"],r["expected-frame"]),"window moved since membership selection");
}
}
QJsonObject activityDestination(const QJsonArray &catalog,const QString &selector) {
    const auto id=QUuid(selector);need(!id.isNull(),"Activity selector must be an existing UUID");
    QJsonObject found;int count=0;for(auto value:catalog)if(QUuid(value.toObject()["id"].toString())==id){found=value.toObject();++count;}
    need(count==1,"Activity selector must be an existing UUID");return found;
}
void validateDesktopMove(const QJsonObject &r,const QJsonObject &s){validateMembershipMove(r,s,false);}
void validateActivityMove(const QJsonObject &r,const QJsonObject &s){validateMembershipMove(r,s,true);}
bool MembershipMoves::pending() const {for(const auto &job:jobs_)if(!terminal(job.state))return true;return false;}
bool MembershipMoves::untouched(const Job &j,const QJsonObject &w) const {return !w.isEmpty()&&w["output"]==QJsonValue(j.output)&&w[otherMembership()]==QJsonValue(j.other)&&(w[membership()]==QJsonValue(j.target)||w[membership()]==QJsonValue(j.original));}
QJsonObject MembershipMoves::submit(const QString &owner,const QJsonObject &r,const QJsonObject &s,qint64 now) {
    need(!owner.isEmpty(),"membership movement requires a D-Bus owner");auto id=r["id"].toString();auto digest=QCryptographicHash::hash(QJsonDocument(r).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
    if(jobs_.contains(id)){need(jobs_[id].owner==owner&&jobs_[id].digest==digest,"membership movement ID reused");return status(owner,id);}
    bool ok=false;auto text=r["request-sequence"].toString();auto sequence=text.toULongLong(&ok);need(ok&&sequence>0&&QString::number(sequence)==text&&sequence>sequences_.value(owner),"invalid or expired membership movement sequence");
    need(sequences_.contains(owner)||sequences_.size()<64,"membership movement owner capacity exceeded");need(!pending(),"another membership movement is pending");
    validateMembershipMove(r,s,activity_);auto window=r["window"].toString();need(available_(window),"release native tiling ownership before changing membership");
    auto fresh=read_(window);need(!fresh.isEmpty()&&fresh["desktops"]==r["expected-desktops"]&&fresh["activities"]==r["expected-activities"]&&fresh["output"]==r["source-output"].toObject()["name"]&&sameFrame(fresh["frame"],r["expected-frame"]),"window changed before membership write");
    if(jobs_.size()>=32){jobs_.remove(order_.takeFirst());}
    Job j;j.owner=owner;j.window=window;j.output=fresh["output"].toString();j.original=fresh[membership()].toArray();j.other=fresh[otherMembership()].toArray();j.target=QJsonArray{r[targetField()]};j.digest=digest;j.since=now;
    jobs_[id]=j;order_.append(id);sequences_[owner]=sequence;
    if(j.original==j.target){jobs_[id].state="moved";jobs_[id].original=QJsonArray{};}
    else if(!write_(window,j.target))rollback(jobs_[id],now,"KWin rejected membership write");
    return status(owner,id);
}
QJsonObject MembershipMoves::status(const QString &owner,const QString &id) const {
    need(jobs_.contains(id)&&jobs_[id].owner==owner,"unknown membership movement for this owner");const auto &j=jobs_[id];
    return {{"id",id},{"window",j.window},{targetField(),j.target[0]},{"state",j.state},{"completed",terminal(j.state)},{"success",j.state=="moved"},{"acknowledged",j.state=="moved"},{"error",j.error}};
}
void MembershipMoves::rollback(Job &j,qint64 now,const QString &error) {
    j.error=error;auto w=read_(j.window);
    if(!untouched(j,w)){j.state="failed";j.error+="; window disappeared or its context changed, preserving current state";return;}
    j.state="rolling_back";j.since=now;j.matching=0;
    if(w[membership()]!=QJsonValue(j.original)&&!write_(j.window,j.original)){j.state="restore_failed";j.error+="; original membership write was rejected";}
}
bool MembershipMoves::tick(qint64 now) {
    bool changed=false;
    for(auto &j:jobs_)if(!terminal(j.state)) {
        auto w=read_(j.window);
        if(!untouched(j,w)){j.state="failed";j.error="window disappeared or context changed during membership movement";changed=true;continue;}
        const auto expected=j.state=="pending"?j.target:j.original;
        if(w[membership()]==QJsonValue(expected)){if(++j.matching>=3){j.state=j.state=="pending"?"moved":"rolled_back";j.original=QJsonArray{};changed=true;continue;}}else j.matching=0;
        if(now-j.since>=1200){changed=true;if(j.state=="pending")rollback(j,now,"membership confirmation timed out");else{j.state="restore_failed";j.error+="; original membership could not be confirmed";}}
    }
    return changed;
}
void MembershipMoves::release(const QString &owner,qint64){sequences_.remove(owner);for(auto it=jobs_.begin();it!=jobs_.end();)if(it->owner==owner){order_.removeOne(it.key());it=jobs_.erase(it);}else ++it;}
void MembershipMoves::teardown(){jobs_.clear();order_.clear();sequences_.clear();}
}
