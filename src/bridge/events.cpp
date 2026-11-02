#include "events.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <limits>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
quint64 ordinal(const QJsonValue &v){bool ok=false;auto n=v.toString().toULongLong(&ok);need(v.isString()&&ok&&QString::number(n)==v.toString(),"invalid event ordinal");return n;}
const QSet<QString> types{"window_added","window_closed","window_geometry","window_state","window_context","window_title","window_drag_start","window_drag_step","window_drag_finish","context_changed","outputs_changed","window_focus"};
}
void EventJournal::append(const QString &type,const QJsonObject &data) {
    need(types.contains(type),"unknown event type");
    need(QJsonDocument(data).toJson(QJsonDocument::Compact).size()<=2048,"event payload exceeds 2048 bytes");
    need(latest_<std::numeric_limits<quint64>::max(),"event sequence exhausted");
    records_.append(QJsonObject{{"ordinal",QString::number(++latest_)},{"type",type},{"data",data}});
    if(records_.size()>256)records_.removeFirst();
}
QJsonObject EventJournal::since(const QString &instance,quint64 cursor) const {
    bool resync=instance!=instance_||cursor>latest_||(!records_.isEmpty()&&cursor<ordinal(records_.first().toObject()["ordinal"])-1);
    QJsonArray events;if(!resync)for(auto r:records_)if(ordinal(r.toObject()["ordinal"])>cursor)events.append(r);
    return {{"protocol",1},{"instance",instance_},{"after",QString::number(cursor)},{"latest",QString::number(latest_)},{"resync-required",resync},{"events",events}};
}
void EventCursor::baseline(const QString &instance,quint64 cursor){instance_=instance;cursor_=cursor;records_=QJsonArray{};ready_=true;}
void EventCursor::clear(){instance_.clear();cursor_=0;records_=QJsonArray{};ready_=false;}
bool EventCursor::accept(const QByteArray &json) {
    need(ready_,"event cursor has no baseline");need(json.size()<=1024*1024,"event batch exceeds 1 MiB");
    QJsonParseError error;auto doc=QJsonDocument::fromJson(json,&error);need(error.error==QJsonParseError::NoError&&doc.isObject(),"invalid event JSON");auto s=doc.object();
    need(s["protocol"]==QJsonValue(1)&&s["instance"].isString()&&s["resync-required"].isBool()&&s["events"].isArray(),"invalid event envelope");
    need(ordinal(s["after"])==cursor_,"event batch does not match requested cursor");auto latest=ordinal(s["latest"]);
    auto events=s["events"].toArray();need(events.size()<=256,"event batch too large");
    if(s["resync-required"].toBool()){need(events.isEmpty(),"resync batch must not contain events");return false;}
    need(s["instance"].toString()==instance_&&latest>=cursor_,"event instance changed or cursor moved backward");
    auto next=cursor_;auto recent=records_;
    for(auto value:events) {
        need(value.isObject(),"invalid event record");auto r=value.toObject();
        need(next<std::numeric_limits<quint64>::max()&&ordinal(r["ordinal"])==next+1,"gap or duplicate in event batch");++next;
        need(r["type"].isString()&&types.contains(r["type"].toString())&&r["data"].isObject(),"invalid event type or payload");
        need(QJsonDocument(r["data"].toObject()).toJson(QJsonDocument::Compact).size()<=2048,"event payload too large");
        recent.append(r);if(recent.size()>256)recent.removeFirst();
    }
    need(next==latest,"incomplete event batch");cursor_=next;records_=recent;return true;
}
QJsonObject EventCursor::status() const{return {{"ready",ready_},{"instance",instance_},{"cursor",QString::number(cursor_)},{"retained",records_.size()}};}
QJsonObject EventCursor::events() const{auto out=status();out["events"]=records_;return out;}
}
