#include "diagnostics.h"
#include <QJsonArray>
#include <cmath>
#include <algorithm>
#include <exception>
namespace kwabai {
QJsonObject geometryDiagnostics(const BridgeClient &bridge,const ContextStore &contexts,const QString &output) {
    auto context=contexts.show(bridge.state(),output);auto status=bridge.contextStatus(context["id"].toString());auto active=status["active"].toObject();
    QJsonObject plan;QString planError;
    try{plan=contexts.plan(bridge.state(),output,true);}catch(const std::exception &e){planError=QString::fromUtf8(e.what());}
    QMap<QString,QJsonObject> observed,targets,planned;QSet<QString> uuids;
    for(auto v:bridge.state().windows()["windows"].toArray()){auto w=v.toObject();observed[w["uuid"].toString()]=w;}
    for(auto v:context["members"].toArray())uuids.insert(v.toObject()["uuid"].toString());
    for(auto v:active["target-placements"].toArray()){auto p=v.toObject();targets[p["uuid"].toString()]=p;uuids.insert(p["uuid"].toString());}
    for(auto v:plan["placements"].toArray()){auto p=v.toObject();planned[p["uuid"].toString()]=p;}
    auto ids=uuids.values();std::sort(ids.begin(),ids.end());QJsonArray windows;bool confirmed=status["active-count"].toInt()==1&&!active.isEmpty()&&active["state"]=="applied"&&active["success"].toBool()&&active["bridge-instance"]==bridge.state().windows()["instance"],matches=confirmed,exact=confirmed;
    for(auto uuid:ids) {
        auto w=observed.value(uuid);auto target=targets.value(uuid).value("frame").toObject();auto frame=w.value("frame").toObject();
        QJsonObject row{{"uuid",uuid},{"id",w.value("id")},{"app",w.value("app")},{"present",!w.isEmpty()},{"target-frame",target.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(target)},{"frame",w.value("frame")},{"planned-frame",planned.value(uuid).value("frame")},{"minimum",w.value("minimum")}};
        for(auto field:{"requested-frame","client-frame","buffer-frame","interaction","minimized","fullscreen","candidate","output"})if(w.contains(field))row[field]=w[field];
        bool available=!target.isEmpty()&&!frame.isEmpty();double error=0;QJsonObject delta;
        if(available){for(auto field:{"x","y","w","h"}){double d=frame[field].toDouble()-target[field].toDouble();delta[field]=d;error=std::max(error,std::abs(d));}
            row["delta"]=delta;row["max-error"]=error;row["right-edge-error"]=delta["x"].toDouble()+delta["w"].toDouble();row["bottom-edge-error"]=delta["y"].toDouble()+delta["h"].toDouble();
        }
        row["within-tolerance"]=available&&error<=1;row["exact-match"]=available&&error<=0.000001;matches=matches&&available&&error<=1;exact=exact&&available&&error<=0.000001;windows.append(row);
    }
    return {{"context-id",context["id"]},{"output",context["output"]},{"bridge-instance",bridge.state().windows()["instance"]},{"snapshot-sequence",bridge.state().windows()["sequence"]},{"stored-generation",context["generation"]},{"transaction",active.value("id")},{"transaction-state",active.value("state")},{"transaction-generation",active.value("context-generation")},{"active-count",status["active-count"]},{"confirmed",confirmed},{"frames-match",matches},{"exact-frames",exact},{"tolerance",1},{"pixel-aligned",plan.value("pixel-aligned")},{"plan-error",planError},{"windows",windows}};
}
}
