#include "manager.h"
#include "mouse_policy.h"
#include "geometry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
QRectF rect(const QJsonValue &v){auto o=v.toObject();return {o["x"].toDouble(),o["y"].toDouble(),o["w"].toDouble(),o["h"].toDouble()};}
bool close(QRectF a,QRectF b){return std::abs(a.x()-b.x())<=1&&std::abs(a.y()-b.y())<=1&&std::abs(a.width()-b.width())<=1&&std::abs(a.height()-b.height())<=1;}
}
bool ContextManager::interact(Slot &s,const QJsonObject &job,const QJsonObject &output) {
    QJsonObject gesture,window;int count=0;
    if(bridge_.state().hasMouse())for(auto v:bridge_.state().windows()["windows"].toArray()) {
        auto w=v.toObject();auto g=w.value("interaction").toObject();
        if(!g.isEmpty()&&g["transaction"]==job["id"]&&job["managed-windows"].toArray().contains(w["uuid"])){gesture=g;window=w;++count;}
    }
    if(!count){need(s.interactionId.isEmpty(),"mouse interaction lost its completion; explicitly recover this context");return false;}
    need(count==1,"simultaneous mouse interactions require explicit recovery");
    auto context=contexts_.show(bridge_.state(),s.outputId);
    need(job["state"]=="applied"&&job["context-generation"]==context["generation"]&&job["target-work-area"]==output["work-area"]&&!s.resuming,"layout or work area changed during mouse interaction");
    need(!window["minimized"].toBool()&&!window["fullscreen"].toBool(),"mouse interaction changed output or window state; explicitly recover this context");
    auto ds=window["desktops"].toArray(),as=window["activities"].toArray();need((ds.isEmpty()||ds.contains(output["current-desktop"]))&&(as.isEmpty()||as.contains(output["current-activity"])),"mouse interaction changed desktop or Activity");
    bool matched=false;
    for(auto v:job["target-placements"].toArray()) {
        auto p=v.toObject();if(p["uuid"]==window["uuid"]){matched=close(rect(p["frame"]),rect(gesture["start-frame"]));continue;}
        bool valid=false;for(auto value:bridge_.state().windows()["windows"].toArray())if(value.toObject()["uuid"]==p["uuid"])valid=close(rect(value.toObject()["frame"]),rect(p["frame"]));
        need(valid,"another layout member moved or closed during mouse interaction");
    }
    need(matched,"mouse interaction did not start at the confirmed tile");
    s.interactionId=gesture["id"].toString();
    auto pointer=gesture["pointer"].toObject();QPointF pointerPosition(pointer["x"].toDouble(),pointer["y"].toDouble());
    QJsonObject pointerOutput;for(auto value:bridge_.state().outputs()["outputs"].toArray())if([&]{auto r=rect(value.toObject()["geometry"]);return pointerPosition.x()>=r.left()&&pointerPosition.x()<r.right()&&pointerPosition.y()>=r.top()&&pointerPosition.y()<r.bottom();}())pointerOutput=value.toObject();
    const bool foreignPointer=!pointerOutput.isEmpty()&&pointerOutput["id"]!=output["id"];
    if(foreignPointer||window["output"]!=output["name"]) {
        need(bridge_.state().hasMouseOutputDrop()&&gesture["kind"]=="move","mouse interaction changed output or window state; explicitly recover this context");
        if(gesture["state"]=="active"){
            s.state="interacting-off-output";
            if(foreignPointer&&bridge_.state().hasMouseOutputPreview())try{beginOutputDrop(s,job,window,gesture,output,true);}catch(const std::exception &){bridge_.clearDropPreview(s.contextId);}
            else bridge_.clearDropPreview(s.contextId);
            return true;
        }
        bridge_.clearDropPreview(s.contextId);
        need(window["candidate"].toBool()&&close(rect(window["frame"]),rect(gesture["frame"])),"window changed after output drop");
        if(bridge_.geometryBusy()){s.state="queued-interaction";return true;}
        need(foreignPointer,"drop pointer and window output disagree; finish the move inside the destination output");
        beginOutputDrop(s,job,window,gesture,output);return true;
    }
    if(gesture["state"]=="active") {
        s.state="interacting";
        if(gesture["kind"]!="move"){bridge_.clearDropPreview(s.contextId);return true;}
        auto pointer=gesture["pointer"].toObject();QPointF drop(pointer["x"].toDouble(),pointer["y"].toDouble());QString target,direction;
        for(auto v:job["target-placements"].toArray()){auto p=v.toObject();if(!close(rect(gesture["start-frame"]),rect(window["frame"]))&&rect(p["frame"]).contains(drop)&&p["uuid"]!=window["uuid"]){target=p["uuid"].toString();direction=mouseDropDirection(rect(p["frame"]),drop);}}
        if(target.isEmpty()){bridge_.clearDropPreview(s.contextId);return true;}
        try {
            auto trial=contexts_;auto snapshot=bridge_.state().windows();auto status=bridge_.state().status();for(auto key:{"capabilities","kwin-version","event-sequence","geometry-window-limit"})snapshot[key]=status[key];snapshot["outputs"]=bridge_.state().outputs()["outputs"];if(bridge_.state().hasDesktopMove())snapshot["desktops"]=bridge_.state().desktops()["desktops"];if(bridge_.state().hasActivityMove())snapshot["activities"]=bridge_.state().activities()["activities"];
            auto windows=snapshot["windows"].toArray();for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]==window["uuid"]){w["candidate"]=true;windows[i]=w;}}snapshot["windows"]=windows;BridgeState view;view.accept(QJsonDocument(snapshot).toJson());
            if(direction.isEmpty())trial.edit(view,{"swap",s.outputId,window["uuid"].toString(),target});else trial.edit(view,{"warp",s.outputId,window["uuid"].toString(),target,direction});
            auto plan=trial.plan(view,s.outputId,true);for(auto v:plan["placements"].toArray()){auto p=v.toObject();if(!p["visible"].toBool())throw std::invalid_argument("hidden preview stack");if(p["uuid"]==window["uuid"])bridge_.setDropPreview({{"radius",contexts_.windowBorderRadius()},{"color",contexts_.insertFeedbackColor()},{"context-id",s.contextId},{"transaction",job["id"]},{"window",window["uuid"]},{"interaction",gesture["id"]},{"context",plan["context"]},{"work-area",plan["work-area"]},{"frame",p["frame"]},{"operation",direction.isEmpty()?"swap":"insert"},{"direction",direction}});}
        }catch(const std::exception &){bridge_.clearDropPreview(s.contextId);}
        return true;
    }
    bridge_.clearDropPreview(s.contextId);
    need(window["candidate"].toBool()&&close(rect(window["frame"]),rect(gesture["frame"])),"window changed after mouse interaction finished");
    if(bridge_.geometryBusy()){s.state="queued-interaction";return true;}
    auto candidate=contexts_;const auto uuid=window["uuid"].toString();
    if(gesture["kind"]=="move") {
        auto pointer=gesture["pointer"].toObject();QPointF drop(pointer["x"].toDouble(),pointer["y"].toDouble());QString target,direction;
        for(auto v:job["target-placements"].toArray()){auto p=v.toObject();if(!close(rect(gesture["start-frame"]),rect(gesture["frame"]))&&rect(p["frame"]).contains(drop)&&p["uuid"]!=window["uuid"]){target=p["uuid"].toString();direction=mouseDropDirection(rect(p["frame"]),drop);}}
        if(!target.isEmpty()) {
            if(direction.isEmpty())candidate.edit(bridge_.state(),{"swap",s.outputId,uuid,target});
            else candidate.edit(bridge_.state(),{"warp",s.outputId,uuid,target,direction});
        }
    } else {
        auto start=rect(gesture["start-frame"]),end=rect(gesture["frame"]);
        double left=end.left()-start.left(),right=end.right()-start.right(),top=end.top()-start.top(),bottom=end.bottom()-start.bottom();
        auto changed=[](double value){return std::abs(value)>1;};
        need(!(changed(left)&&changed(right))&&!(changed(top)&&changed(bottom)),"resize changed both opposing edges; explicitly recover this context");
        QString horizontal=changed(left)?"left":changed(right)?"right":"",vertical=changed(top)?"top":changed(bottom)?"bottom":"";
        QString handle=vertical.isEmpty()?horizontal:horizontal.isEmpty()?vertical:vertical+"_"+horizontal;
        if(!handle.isEmpty()) {
            try {candidate.edit(bridge_.state(),{"resize-edges",s.outputId,uuid,handle,QString::number(changed(left)?left:right,'g',17),QString::number(changed(top)?top:bottom,'g',17)});}
            catch(const std::invalid_argument &e){if(QString::fromUtf8(e.what())!="no internal split boundary for this resize handle")throw;}
        }
    }
    // Cancelled gestures and drops in gaps still need a confirmed replacement
    // to consume the native authorization and return the window to its tile.
    if(candidate.show(bridge_.state(),s.outputId)["generation"]==context["generation"])candidate.edit(bridge_.state(),{"refresh",s.outputId});
    auto plan=candidate.plan(bridge_.state(),s.outputId,true);for(auto p:plan["placements"].toArray())need(p.toObject()["visible"].toBool(),"mouse editing does not support hidden stacks");
    auto request=geometryRequest(plan,bridge_.state().windows(),QUuid::createUuid().toString(QUuid::WithoutBraces),job);request["supersedes"]=job["id"];request["interaction"]=QJsonObject{{"id",gesture["id"]},{"window",uuid}};
    persist_(candidate.state());contexts_=std::move(candidate);
    auto receipt=bridge_.submitGeometry(request);s.transactionId=receipt["id"].toString();s.interactionId.clear();s.state="waiting";return true;
}
}
