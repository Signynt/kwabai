#include "plan.h"
#include "tree.h"
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
Rect rect(QJsonObject r){return {r["x"].toDouble(),r["y"].toDouble(),r["w"].toDouble(),r["h"].toDouble()};}
}
QJsonObject dryRunPlan(const BridgeState &state,const QString &selector,const QStringList &selected) {
    need(state.hasContexts(),"bridge has no output/context capability");
    need(!selected.isEmpty()&&selected.size()<=20,"plan requires between 1 and 20 explicit window IDs");
    QJsonObject output;int matches=0;
    for(auto value:state.outputs().value("outputs").toArray()) {
        auto o=value.toObject();if(o["name"].toString()==selector||o["id"].toString()==selector){output=o;++matches;}
    }
    need(matches==1,"output selector missing or ambiguous");
    const auto area=rect(output["work-area"].toObject());
    QMap<QString,QJsonObject> windows;for(auto value:state.windows().value("windows").toArray()){auto w=value.toObject();windows[w["id"].toString()]=w;}
    Tree tree;QSet<QString> seen;std::map<WindowId,Size> minimums;QJsonArray selectedIds;
    for(auto key:selected) {
        need(windows.contains(key)&&!seen.contains(key),"unknown or repeated window ID");seen.insert(key);auto w=windows[key];
        need(w["candidate"].toBool()&&!w["minimized"].toBool()&&!w["fullscreen"].toBool(),"selected window is not eligible in its current state");
        need(w["output"].toString()==output["name"].toString(),"selected window belongs to another output");
        auto desktops=w["desktops"].toArray(),activities=w["activities"].toArray();
        need(desktops.isEmpty()||desktops.contains(output["current-desktop"]),"selected window is not on the output's current desktop");
        need(activities.isEmpty()||activities.contains(output["current-activity"]),"selected window is not on the current Activity");
        bool ok=false;auto id=key.toULongLong(&ok);need(ok&&id,"invalid public window ID");
        WindowId target=0;Direction direction=Direction::East;
        if(tree.root()) {
            auto current=tree.layout(area);auto largest=std::max_element(current.begin(),current.end(),[](auto a,auto b){return a.frame.width*a.frame.height<b.frame.width*b.frame.height;});
            target=largest->window;direction=largest->frame.width>=largest->frame.height?Direction::East:Direction::South;
        }
        tree.add(id,target,direction);auto min=w["minimum"].toObject();minimums[id]={min["w"].toDouble(),min["h"].toDouble()};selectedIds.append(key);
    }
    QJsonArray placements;
    // Resolve constraints only once against the complete tree: no partial plan is published.
    for(auto p:tree.layout(area,{},10,minimums)) {
        auto key=QString::number(qulonglong(p.window));
        placements.append(QJsonObject{{"id",key},{"uuid",windows[key]["uuid"]},{"frame",QJsonObject{{"x",p.frame.x},{"y",p.frame.y},{"w",p.frame.width},{"h",p.frame.height}}}});
    }
    return {{"dry-run",true},{"desktop-control",false},{"bridge-instance",state.windows()["instance"]},{"sequence",state.windows()["sequence"]},
        {"context",QJsonObject{{"output-id",output["id"]},{"output",output["name"]},{"desktop",output["current-desktop"]},{"activity",output["current-activity"]}}},
        {"work-area",output["work-area"]},{"padding",10},{"gap",10},{"selected",selectedIds},{"placements",placements}};
}
}
