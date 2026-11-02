#include "../compat/color.h"
#include "store.h"
#include "json.h"
#include "../compat/resize.h"
#include "desktops.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <limits>
#include <tuple>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
quint64 integer(const QJsonValue &value){bool ok=false;auto n=value.toString().toULongLong(&ok);need(value.isString()&&ok&&QString::number(n)==value.toString(),"invalid context integer");return n;}
double number(const QString &s){bool ok=false;auto d=s.toDouble(&ok);need(ok&&std::isfinite(d),"expected finite number");return d;}
Direction direction(const QString &s){if(s=="north")return Direction::North;if(s=="east")return Direction::East;if(s=="south")return Direction::South;if(s=="west")return Direction::West;throw std::invalid_argument("expected north/east/south/west");}
QJsonObject focused(const BridgeState &b) {
    QJsonObject found;int count=0;
    for(auto v:b.windows()["windows"].toArray())if(v.toObject()["focused"].toBool()){found=v.toObject();++count;}
    need(count==1,"focused window is missing or ambiguous");return found;
}
bool cardinal(const QString &s){return s=="north"||s=="east"||s=="south"||s=="west";}
QJsonArray paddingJson(Insets p){return {p.top,p.right,p.bottom,p.left};}
Insets paddingFromJson(const QJsonValue &value) {
    need(value.isArray()&&value.toArray().size()==4,"invalid context padding");auto values=value.toArray();
    for(auto v:values)need(v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=0,"invalid context padding");
    return {values[0].toDouble(),values[1].toDouble(),values[2].toDouble(),values[3].toDouble()};
}
QString unavailable(const QJsonObject &w,const QJsonObject &o) {
    if(!w["scratchpad"].toString().isEmpty())return "scratchpad";
    if(w["output"]!=o["name"])return "other-output";
    auto ds=w["desktops"].toArray(),as=w["activities"].toArray();
    if(!ds.isEmpty()&&!ds.contains(o["current-desktop"]))return "other-desktop";
    if(!as.isEmpty()&&!as.contains(o["current-activity"]))return "other-activity";
    if(w["minimized"].toBool())return "minimized";
    if(w["fullscreen"].toBool())return "fullscreen";
    if(!w["candidate"].toBool())return "ineligible";
    return {};
}
QJsonArray floatingJson(const QSet<QString> &ids){auto sorted=ids.values();std::sort(sorted.begin(),sorted.end());QJsonArray result;for(auto id:sorted)result.append(id);return result;}
Rect rect(const QJsonObject &o){return {o["x"].toDouble(),o["y"].toDouble(),o["w"].toDouble(),o["h"].toDouble()};}
}
QJsonObject ContextStore::output(const BridgeState &b,const QString &selector) {
    need(b.hasContexts(),"connected output contexts required");QJsonObject found;int count=0;
    auto selected=selector=="focused"?focused(b)["output"].toString():selector;
    for(auto v:b.outputs()["outputs"].toArray()){auto o=v.toObject();if(o["id"].toString()==selected||o["name"].toString()==selected){found=o;++count;}}
    need(count==1,"output selector missing or ambiguous");return found;
}
QString ContextStore::key(const QJsonObject &o){return QString::fromUtf8(QJsonDocument(QJsonArray{o["id"],o["current-desktop"],o["current-activity"]}).toJson(QJsonDocument::Compact));}
WindowId ContextStore::member(const Context &c,const BridgeState &bridge,const QString &selector) const {
    if(cardinal(selector))return member(c,bridge,neighbor(bridge,c.output,"focused",selector)["uuid"].toString());
    QString uuid;if(selector=="focused")uuid=focused(bridge)["uuid"].toString();
    auto parsed=QUuid(selector);if(!parsed.isNull())uuid=parsed.toString(QUuid::WithoutBraces);
    else for(auto v:bridge.windows()["windows"].toArray()){auto w=v.toObject();if(w["id"].toString()==selector)uuid=w["uuid"].toString();}
    for(auto it=c.members.begin();it!=c.members.end();++it)if(it.value()==uuid)return it.key();
    throw std::invalid_argument("window is not a member of this context");
}
int ContextStore::globalSpacing(const QString &name) const {need(spacing_.contains(name),"unsupported spacing setting");return spacing_[name];}
void ContextStore::setGlobalSpacing(const BridgeState &b,const QString &name,const QString &text) {
    need(spacing_.contains(name),"unsupported spacing setting");bool ok=false;auto value=text.toLongLong(&ok);
    need(ok&&value>=0&&value<=std::numeric_limits<qint32>::max()&&!text.isEmpty(),"spacing must be a nonnegative 32-bit integer");for(auto c:text)need(c>='0'&&c<='9',"spacing must be a nonnegative integer");
    spacing_[name]=int(value);globalSpacing_=true;
    for(auto &c:contexts_){auto before=c.padding;auto gap=c.gap;
        if(name=="window_gap")c.gap=value;else if(name=="top_padding")c.padding.top=value;else if(name=="right_padding")c.padding.right=value;else if(name=="bottom_padding")c.padding.bottom=value;else c.padding.left=value;
        if(gap!=c.gap||paddingJson(before)!=paddingJson(c.padding)){need(c.generation<std::numeric_limits<quint64>::max(),"context generation exhausted");++c.generation;}
        for(auto output:b.outputs()["outputs"].toArray())if(output.toObject()["id"]==QJsonValue(c.output))(void)c.tree.layout(rect(output.toObject()["work-area"].toObject()),c.padding,c.gap);
    }
}
void ContextStore::useGlobalSpacing(const BridgeState &b){auto values=spacing_;for(auto it=values.begin();it!=values.end();++it)setGlobalSpacing(b,it.key(),QString::number(it.value()));}
void ContextStore::prepareAutomatic(const BridgeState &b,const QString &selector) {
    const auto selected=output(b,selector);const auto selectedKey=key(selected);
    // A sticky window has one native owner, so visible windows are rehomed
    // from inactive contexts before the destination reconciles its membership.
    QMap<QString,QJsonObject> windows;for(auto value:b.windows()["windows"].toArray())windows[value.toObject()["uuid"].toString()]=value.toObject();
    for(auto it=contexts_.begin();it!=contexts_.end();++it){if(it.key()==selectedKey)continue;auto &c=it.value();bool changed=false;
        for(auto member=c.members.begin();member!=c.members.end();){auto w=windows.value(member.value());if(!w.isEmpty()&&unavailable(w,selected).isEmpty()){c.tree.remove(member.key());member=c.members.erase(member);changed=true;}else ++member;}
        if(changed){need(c.generation<std::numeric_limits<quint64>::max(),"context generation exhausted");++c.generation;c.undo.clear();c.redo.clear();}
    }
    if(!contexts_.contains(selectedKey))editImpl(b,{"reconcile",selector});
    else {
        auto &c=contexts_[selectedKey];bool changed=false;
        for(auto member=c.members.begin();member!=c.members.end();){auto w=windows.value(member.value());auto reason=unavailable(w,selected);if(w.isEmpty()||reason=="scratchpad"||reason=="other-output"||reason=="other-desktop"||reason=="other-activity"||rules_.evaluate(w)["action"]==QJsonValue("float")){c.tree.remove(member.key());member=c.members.erase(member);changed=true;}else ++member;}
        if(changed){need(c.generation<std::numeric_limits<quint64>::max(),"context generation exhausted");++c.generation;c.undo.clear();c.redo.clear();}
        editImpl(b,{b.hasGeometryReconcile()?"reconcile-adaptive":b.hasHiddenRelease()?"reconcile-hidden":"reconcile",selector});
    }
}
bool ContextStore::isZoomed(const QString &uuid) const {
    for(const auto &c:contexts_)if(c.tree.snapshot().nodes.size()>1&&c.zoomed.contains(uuid)&&c.members.values().contains(uuid))return true;
    return false;
}
void ContextStore::clearZoom(){for(auto &c:contexts_)if(!c.zoomed.isEmpty()){need(c.generation<std::numeric_limits<quint64>::max(),"context generation exhausted");c.zoomed.clear();++c.generation;}}
void ContextStore::edit(const BridgeState &b,const QStringList &a){ContextStore candidate(*this);candidate.editImpl(b,a);*this=std::move(candidate);}
void ContextStore::reset(const BridgeState &bridge,const QStringList &selectors) {
    ContextStore candidate(*this);QStringList outputs;
    for(auto selector:selectors){auto o=output(bridge,selector);auto k=key(o);need(!outputs.contains(o["id"].toString()),"duplicate output reset");outputs<<o["id"].toString();auto it=candidate.contexts_.find(k);if(it==candidate.contexts_.end())continue;
        need(it->generation<std::numeric_limits<quint64>::max(),"context generation exhausted");it->tree=Tree{};it->members.clear();it->zoomed.clear();it->undo.clear();it->redo.clear();++it->generation;
    }
    // Clear all selected memberships first so a stale source cannot exclude a
    // live window from its actual destination when resetting both outputs.
    for(auto selector:outputs){candidate.edit(bridge,{"reconcile",selector});(void)candidate.plan(bridge,selector,true);}
    *this=std::move(candidate);
}
void ContextStore::exchangeOutputs(const BridgeState &bridge,const QString &sourceOutput,const QString &sourceWindow,const QString &targetOutput,const QString &targetWindow) {
    ContextStore candidate(*this);auto from=output(bridge,sourceOutput),to=output(bridge,targetOutput);need(from["id"]!=to["id"]&&from["current-desktop"]==to["current-desktop"]&&from["current-activity"]==to["current-activity"],"swap requires two outputs on the same desktop and Activity");
    auto sourceKey=key(from),targetKey=key(to);need(candidate.contexts_.contains(sourceKey)&&candidate.contexts_.contains(targetKey),"swap requires two stored contexts");auto &source=candidate.contexts_[sourceKey];auto &target=candidate.contexts_[targetKey];auto a=member(source,bridge,sourceWindow),b=member(target,bridge,targetWindow);auto uuidA=source.members[a],uuidB=target.members[b];
    auto replace=[](Tree &tree,WindowId oldId,WindowId newId){auto snapshot=tree.snapshot();for(auto &node:snapshot.nodes){for(auto &id:node.windows)if(id==oldId)id=newId;if(node.active==oldId)node.active=newId;}tree=Tree::restore(snapshot);};
    source.zoomed.remove(uuidA);target.zoomed.remove(uuidB);
    replace(source.tree,a,b);replace(target.tree,b,a);source.members.remove(a);source.members[b]=uuidB;target.members.remove(b);target.members[a]=uuidA;
    for(auto *context:{&source,&target}){need(context->generation<std::numeric_limits<quint64>::max()-1,"context generation exhausted");context->generation+=2;context->undo.clear();context->redo.clear();}
    *this=std::move(candidate);
}
QJsonObject ContextStore::stageDesktopAdmission(const BridgeState &b,const QString &selector,const QString &uuid,const QString &desktop,bool activity) {
    need(activity?b.hasActivityMove():b.hasDesktopMove(),"desktop admission requires a desktop catalog");
    auto target=activity?activityDestination(b.activities()["activities"].toArray(),desktop):desktopDestination(b.desktops()["desktops"].toArray(),desktop);auto selected=output(b,selector);selected[activity?"current-activity":"current-desktop"]=target["id"];
    // This view is solely for saving inactive layout intent. No native geometry
    // request may use it; actual visibility/eligibility is checked on activation.
    auto projected=b.windows();auto status=b.status();for(auto field:{"capabilities","kwin-version","event-sequence","geometry-window-limit"})projected[field]=status[field];
    projected["desktops"]=b.desktops()["desktops"];projected["activities"]=b.activities()["activities"];auto outputs=b.outputs()["outputs"].toArray();for(qsizetype i=0;i<outputs.size();++i)if(outputs[i].toObject()["id"]==selected["id"])outputs[i]=selected;projected["outputs"]=outputs;
    auto windows=projected["windows"].toArray();bool found=false;
    for(qsizetype i=0;i<windows.size();++i){auto w=windows[i].toObject();if(w["uuid"]==QJsonValue(uuid)){need(w["output"]==selected["name"]&&!w["minimized"].toBool()&&!w["fullscreen"].toBool(),"desktop arrival changed output or became ineligible");w[activity?"activities":"desktops"]=QJsonArray{target["id"]};w["candidate"]=true;windows[i]=w;found=true;}}
    need(found,"desktop arrival window disappeared");projected["windows"]=windows;BridgeState view;view.accept(QJsonDocument(projected).toJson());
    auto candidate=*this;auto k=key(selected);
    if(!candidate.contexts_.contains(k)){need(candidate.contexts_.size()<64,"context capacity exceeded");Context context;if(globalSpacing_){context.gap=spacing_["window_gap"];context.padding={double(spacing_["top_padding"]),double(spacing_["right_padding"]),double(spacing_["bottom_padding"]),double(spacing_["left_padding"])};}context.id=QUuid::createUuid().toString(QUuid::WithoutBraces);context.output=selected["id"].toString();context.desktop=selected["current-desktop"].toString();context.activity=selected["current-activity"].toString();candidate.contexts_.insert(k,context);}
    candidate.edit(view,{"float",selected["id"].toString(),uuid,"off"});
    const auto &context=candidate.contexts_[k];QMap<QString,QJsonObject> live;for(auto v:windows)live[v.toObject()["uuid"].toString()]=v.toObject();std::map<WindowId,Size> minimums;
    for(auto it=context.members.begin();it!=context.members.end();++it){need(live.contains(it.value()),"inactive destination contains a missing window");auto w=live[it.value()];auto ds=w["desktops"].toArray(),as=w["activities"].toArray();
        need(w["output"]==selected["name"]&&!w["minimized"].toBool()&&!w["fullscreen"].toBool()&&(ds.isEmpty()||ds.contains(selected["current-desktop"]))&&(as.isEmpty()||as.contains(selected["current-activity"])),"inactive destination member changed context or became ineligible");
        auto minimum=w["minimum"].toObject();minimums[it.key()]={minimum["w"].toDouble(),minimum["h"].toDouble()};
    }
    for(auto p:context.tree.layout(rect(selected["work-area"].toObject()),context.padding,context.gap,minimums))need(p.visible,"desktop transfers do not support hidden stacks");
    auto result=candidate.show(view,selected["id"].toString());*this=std::move(candidate);return result;
}
void ContextStore::editImpl(const BridgeState &b,const QStringList &a) {
    need(a.size()>=2,"context OPERATION OUTPUT ...");auto o=output(b,a[1]);auto k=key(o);auto op=a[0];
    need(op=="add"||op=="reconcile"||op=="reconcile-hidden"||op=="reconcile-adaptive"||contexts_.contains(k),"no stored tree for this output/desktop/Activity");
    const bool created=!contexts_.contains(k);
    if(created){need(contexts_.size()<64,"context capacity exceeded");Context c;if(globalSpacing_){c.gap=spacing_["window_gap"];c.padding={double(spacing_["top_padding"]),double(spacing_["right_padding"]),double(spacing_["bottom_padding"]),double(spacing_["left_padding"])};}c.id=QUuid::createUuid().toString(QUuid::WithoutBraces);c.output=o["id"].toString();c.desktop=o["current-desktop"].toString();c.activity=o["current-activity"].toString();contexts_.insert(k,c);}
    auto &c=contexts_[k];
    Revision before{c.tree,c.gap,c.padding};auto previousMembers=c.members;
    auto insert=[&](const QJsonObject &w,WindowId target,Direction axis,double ratio,bool automatic) {
        need(c.members.size()<20&&nextWindow_<std::numeric_limits<quint64>::max(),"context window capacity exceeded");
        // Reverse layout traversal breaks equal-area ties toward bottom/right leaves.
        if(automatic&&c.tree.root()){auto placements=c.tree.layout(rect(o["work-area"].toObject()),c.padding,c.gap);auto largest=std::max_element(placements.rbegin(),placements.rend(),[](auto x,auto y){return x.frame.width*x.frame.height<y.frame.width*y.frame.height;});target=largest->window;axis=largest->frame.width>=largest->frame.height?Direction::East:Direction::South;}
        c.tree.add(nextWindow_,target,axis,ratio);c.members[nextWindow_++]=w["uuid"].toString();
    };
    need(c.generation<std::numeric_limits<quint64>::max(),"context generation exhausted");
    if(op=="undo"||op=="redo") {
        need(a.size()==2,"context undo/redo OUTPUT");auto &from=op=="undo"?c.undo:c.redo;auto &to=op=="undo"?c.redo:c.undo;
        need(!from.empty(),op=="undo"?"no layout edit to undo in this daemon lifetime":"no layout edit to redo in this daemon lifetime");
        auto target=from.back();from.pop_back();to.push_back(before);if(to.size()>32)to.erase(to.begin());
        // Restoring topology must not reuse identities allocated by a discarded edit.
        auto snapshot=target.tree.snapshot();snapshot.nextNode=std::max(snapshot.nextNode,c.tree.snapshot().nextNode);c.tree=Tree::restore(snapshot);c.gap=target.gap;c.padding=target.padding;
        auto area=rect(o["work-area"].toObject());need(c.padding.left+c.padding.right<area.width&&c.padding.top+c.padding.bottom<area.height,"historical padding leaves no usable work area");
    }
    else if(op=="zoom-fullscreen") {
        need(a.size()==3,"context zoom-fullscreen OUTPUT WINDOW");auto id=member(c,b,a[2]);
        if(c.tree.snapshot().nodes.size()==1)return; // Yabai: a root leaf is already full size.
        auto uuid=c.members[id];if(c.zoomed.contains(uuid))c.zoomed.remove(uuid);else c.zoomed.insert(uuid);
    }
    else if(op=="float") {
        need(a.size()==4&&(a[3]=="on"||a[3]=="off"),"context float OUTPUT WINDOW on|off");
        QJsonObject w;auto uuid=QUuid(a[2]);int matches=0;
        for(auto v:b.windows()["windows"].toArray()){auto candidate=v.toObject();if((a[2]=="focused"&&candidate["focused"].toBool())||candidate["id"]==QJsonValue(a[2])||(!uuid.isNull()&&QUuid(candidate["uuid"].toString())==uuid)){w=candidate;++matches;}}
        need(matches==1&&unavailable(w,o).isEmpty(),"float target is missing, ambiguous or ineligible for this context");
        auto selected=w["uuid"].toString();WindowId admitted=0;for(auto it=c.members.begin();it!=c.members.end();++it)if(it.value()==selected)admitted=it.key();
        for(auto it=contexts_.begin();it!=contexts_.end();++it)if(it.key()!=k){
            for(auto other:it->members)need(other!=selected,"window belongs to another stored context");
            // Explicit floating/retiling follows the live window after an output/context move.
            if(it->floating.contains(selected)){need(it->generation<std::numeric_limits<quint64>::max(),"context generation exhausted");it->floating.remove(selected);++it->generation;}
        }
        if(a[3]=="on") {
            if(c.floating.contains(selected))return;
            need(c.floating.size()<256,"floating window capacity exceeded");
            if(admitted){c.tree.remove(admitted);c.members.remove(admitted);}c.floating.insert(selected);
        } else {
            if(admitted)return;
            c.floating.remove(selected);insert(w,0,Direction::East,0.5,true);
        }
    }
    else if(op=="add") {
        need(a.size()==3||a.size()==5||a.size()==6,"context add OUTPUT ID [TARGET DIRECTION [RATIO]]");
        auto admission=a[2]=="focused"?focused(b)["id"].toString():a[2];
        QJsonObject w;for(auto v:b.windows()["windows"].toArray()){auto candidate=v.toObject();if(candidate["id"].toString()==admission||candidate["uuid"].toString()==admission)w=candidate;}
        need(w["scratchpad"].toString().isEmpty()&&!w.isEmpty()&&w["candidate"].toBool()&&w["output"]==o["name"],"selected window is missing or ineligible for this output");
        auto desktops=w["desktops"].toArray(),activities=w["activities"].toArray();
        need((desktops.isEmpty()||desktops.contains(o["current-desktop"]))&&(activities.isEmpty()||activities.contains(o["current-activity"])),"window is outside the current context");
        for(const auto &context:contexts_)need(!context.floating.contains(w["uuid"].toString()),"window is explicitly floating; use context float OUTPUT WINDOW off");
        need(rules_.evaluate(w)["action"]!="float","window excluded by a float rule; change the rule before admission");
        for(const auto &context:contexts_)for(auto uuid:context.members)need(uuid!=w["uuid"].toString(),"window is already admitted to a context");
        WindowId target=0;Direction axis=Direction::East;
        if(a.size()>3){target=member(c,b,a[3]);axis=direction(a[4]);}
        insert(w,target,axis,a.size()>5?number(a[5]):0.5,a.size()==3);
    } else if(op=="reconcile"||op=="reconcile-hidden"||op=="reconcile-adaptive") {
        need(a.size()==2,"context reconcile OUTPUT --apply");
        auto changes=reconcile(b,a[1],op=="reconcile-hidden"||(op=="reconcile-adaptive"&&b.hasHiddenRelease()),op=="reconcile-adaptive");
        for(auto v:changes["forget-floating"].toArray())c.floating.remove(v.toString());
        for(auto v:changes["remove"].toArray()){auto id=member(c,b,v.toObject()["uuid"].toString());c.tree.remove(id);c.members.remove(id);}
        for(auto v:changes["add"].toArray())insert(v.toObject(),0,Direction::East,0.5,true);
        if(!created&&changes["remove"].toArray().isEmpty()&&changes["add"].toArray().isEmpty()&&changes["forget-floating"].toArray().isEmpty())return;
    } else if(op=="remove") {need(a.size()==3,"context remove OUTPUT ID_OR_UUID");auto id=member(c,b,a[2]);c.tree.remove(id);c.members.remove(id);}
    else if(op=="prune") {need(a.size()==2,"context prune OUTPUT");QSet<QString> live;for(auto v:b.windows()["windows"].toArray())live.insert(v.toObject()["uuid"].toString());auto members=c.members;for(auto it=members.begin();it!=members.end();++it)if(!live.contains(it.value())){c.tree.remove(it.key());c.members.remove(it.key());}}
    else if(op=="swap"||op=="stack") {need(a.size()==4,"context swap/stack OUTPUT SOURCE TARGET");auto from=member(c,b,a[2]),to=member(c,b,a[3]);c.zoomed.remove(c.members[from]);c.zoomed.remove(c.members[to]);if(op=="swap")c.tree.swap(from,to);else c.tree.stack(from,to);}
    else if(op=="activate") {need(a.size()==3,"context activate OUTPUT ID");c.tree.activate(member(c,b,a[2]));}
    else if(op=="warp") {need(a.size()==5||a.size()==6,"context warp OUTPUT SOURCE TARGET DIRECTION [RATIO]");auto from=member(c,b,a[2]),to=member(c,b,a[3]);c.zoomed.remove(c.members[from]);c.zoomed.remove(c.members[to]);c.tree.warp(from,to,direction(a[4]),a.size()==6?number(a[5]):0.5);}
    else if(op=="resize-edges") {
        need(a.size()==6,"context resize-edges OUTPUT ID HANDLE DX DY");
        auto resize=compat::parseEdgeResize(a[3]+":"+a[4]+":"+a[5]);
        const std::map<QString,ResizeHandle> handles{{"top",ResizeHandle::Top},{"left",ResizeHandle::Left},{"bottom",ResizeHandle::Bottom},{"right",ResizeHandle::Right},{"top_left",ResizeHandle::TopLeft},{"top_right",ResizeHandle::TopRight},{"bottom_right",ResizeHandle::BottomRight},{"bottom_left",ResizeHandle::BottomLeft}};
        std::map<WindowId,Size> minimums;
        for(auto it=c.members.begin();it!=c.members.end();++it)for(auto value:b.windows()["windows"].toArray())if(value.toObject()["uuid"]==QJsonValue(it.value())){auto minimum=value.toObject()["minimum"].toObject();minimums[it.key()]={minimum["w"].toDouble(),minimum["h"].toDouble()};}
        if(!c.tree.resizeEdges(member(c,b,a[2]),handles.at(resize.handle),resize.dx,resize.dy,rect(o["work-area"].toObject()),c.padding,c.gap,minimums))return;
    }
    else if(op=="resize") {need(a.size()==5&&(a[3]=="x"||a[3]=="y"),"context resize OUTPUT ID x|y DELTA");c.tree.resize(member(c,b,a[2]),a[3]=="x"?Axis::X:Axis::Y,number(a[4]));}
    else if(op=="toggle-split") {need(a.size()==3,"context toggle-split OUTPUT ID");if(!c.tree.toggleParentSplit(member(c,b,a[2])))return;}
    else if(op=="parent-ratio") {
        need(a.size()==5&&(a[3]=="abs"||a[3]=="rel"),"context parent-ratio OUTPUT ID abs|rel VALUE");
        if(!c.tree.adjustParentRatio(member(c,b,a[2]),number(a[4]),a[3]=="rel"))return;
    }
    else if(op=="ratio") {need(a.size()==5&&(a[3]=="x"||a[3]=="y"),"context ratio OUTPUT ID x|y RATIO");c.tree.setRatio(member(c,b,a[2]),a[3]=="x"?Axis::X:Axis::Y,number(a[4]));}
    else if(op=="equalize") {need(a.size()==2||(a.size()==3&&(a[2]=="x"||a[2]=="y")),"context equalize OUTPUT [x|y]");if(!c.tree.equalize(a.size()==2?std::nullopt:std::optional<Axis>(a[2]=="x"?Axis::X:Axis::Y)))return;}
    else if(op=="mirror-slots") {need(a.size()==3&&(a[2]=="x"||a[2]=="y"),"context mirror-slots OUTPUT x|y");if(!c.tree.mirrorSlots(a[2]=="x"?Axis::X:Axis::Y))return;}
    else if(op=="balance") {need(a.size()==2||(a.size()==3&&(a[2]=="x"||a[2]=="y")),"context balance OUTPUT [x|y]");c.tree.balance(a.size()==2?std::nullopt:std::optional<Axis>(a[2]=="x"?Axis::X:Axis::Y));}
    else if(op=="rotate") {need(a.size()==3&&(a[2]=="90"||a[2]=="180"||a[2]=="270"),"context rotate OUTPUT 90|180|270");c.tree.rotate(a[2].toInt());}
    else if(op=="mirror") {need(a.size()==3&&(a[2]=="x"||a[2]=="y"),"context mirror OUTPUT x|y");c.tree.mirror(a[2]=="x"?Axis::X:Axis::Y);}
    else if(op=="padding") {
        need(a.size()==3||a.size()==4||a.size()==6,"context padding OUTPUT PIXELS, EDGE PIXELS, or TOP RIGHT BOTTOM LEFT");
        if(a.size()==3){auto value=number(a[2]);c.padding={value,value,value,value};}
        else if(a.size()==6)c.padding={number(a[2]),number(a[3]),number(a[4]),number(a[5])};
        else {auto value=number(a[3]);if(a[2]=="top")c.padding.top=value;else if(a[2]=="right")c.padding.right=value;else if(a[2]=="bottom")c.padding.bottom=value;else if(a[2]=="left")c.padding.left=value;else throw std::invalid_argument("padding edge must be top/right/bottom/left");}
        c.padding=paddingFromJson(paddingJson(c.padding));auto area=rect(o["work-area"].toObject());
        need(c.padding.left+c.padding.right<area.width&&c.padding.top+c.padding.bottom<area.height,"padding leaves no usable work area");
    }
    else if(op=="gap") {need(a.size()==3,"context gap OUTPUT PIXELS");c.gap=number(a[2]);need(c.gap>=0,"gap must be nonnegative");}
    else if(op=="refresh") {need(a.size()==2,"context refresh OUTPUT");} // New work area, unchanged topology/history.
    else throw std::invalid_argument("unsupported context operation");
    QSet<QString> present;for(auto uuid:c.members)present.insert(uuid);c.zoomed.intersect(present);
    if(c.tree.snapshot().nodes.size()<=1||op=="undo"||op=="redo")c.zoomed.clear();
    c.tree.validate();(void)c.tree.layout(rect(o["work-area"].toObject()),c.padding,c.gap);
    if(c.members!=previousMembers){c.undo.clear();c.redo.clear();}
    else if(op!="undo"&&op!="redo"&&(encodeSnapshot(before.tree)!=encodeSnapshot(c.tree)||before.gap!=c.gap||paddingJson(before.padding)!=paddingJson(c.padding))) {
        c.undo.push_back(std::move(before));if(c.undo.size()>32)c.undo.erase(c.undo.begin());c.redo.clear();
    }
    ++c.generation;
}
QJsonObject ContextStore::list() const {
    QJsonArray list;for(const auto &c:contexts_)list.append(QJsonObject{{"id",c.id},{"output-id",c.output},{"desktop",c.desktop},{"activity",c.activity},{"generation",QString::number(c.generation)},{"members",c.members.size()}});
    return {{"contexts",list}};
}
QJsonObject ContextStore::show(const BridgeState &b,const QString &selector) const {
    auto o=output(b,selector);auto k=key(o);need(contexts_.contains(k),"no stored tree for this context");const auto &c=contexts_[k];
    QMap<QString,QString> live;for(auto v:b.windows()["windows"].toArray()){auto w=v.toObject();live[w["uuid"].toString()]=w["id"].toString();}
    QJsonArray members;for(auto it=c.members.begin();it!=c.members.end();++it)members.append(QJsonObject{{"layout-window-id",QString::number(qulonglong(it.key()))},{"uuid",it.value()},{"id",live.contains(it.value())?QJsonValue(live[it.value()]):QJsonValue(QJsonValue::Null)}});
    return {{"zoomed",floatingJson(c.zoomed)},{"floating",floatingJson(c.floating)},{"id",c.id},{"generation",QString::number(c.generation)},{"output",o["name"]},{"output-id",c.output},{"desktop",c.desktop},{"activity",c.activity},{"gap",c.gap},{"padding",paddingJson(c.padding)},{"history",QJsonObject{{"undo",int(c.undo.size())},{"redo",int(c.redo.size())},{"limit",32},{"persistent",false}}},{"members",members},{"tree",QJsonDocument::fromJson(encodeSnapshot(c.tree)).object()}};
}
QJsonObject ContextStore::testRule(const BridgeState &b,const QString &selector) const {
    auto result=rules_.test(b,selector);bool admitted=false;
    for(const auto &context:contexts_)for(auto uuid:context.members)if(uuid==result["uuid"].toString())admitted=true;
    bool explicitlyFloating=false;for(const auto &context:contexts_)if(context.floating.contains(result["uuid"].toString()))explicitlyFloating=true;
    result["rule-action"]=result["action"];if(explicitlyFloating)result["action"]="float";result["explicit-floating"]=explicitlyFloating;
    result["already-admitted"]=admitted;result["requires-manual-removal"]=admitted&&result["action"]=="float";return result;
}
QJsonObject ContextStore::neighbor(const BridgeState &b,const QString &selector,const QString &source,const QString &heading) const {
    need(cardinal(heading),"expected north/east/south/west");need(!cardinal(source),"neighbor source must be focused, a public ID or UUID");
    auto o=output(b,selector);auto k=key(o);need(contexts_.contains(k),"no stored tree for this context");const auto &c=contexts_[k];
    auto sourceUuid=c.members[member(c,b,source)];
    auto unzoomed=*this;for(auto &context:unzoomed.contexts_)context.zoomed.clear();
    auto layout=unzoomed.plan(b,selector);QJsonObject from;
    for(auto v:layout["placements"].toArray())if(v.toObject()["uuid"]==QJsonValue(sourceUuid))from=v.toObject();
    need(!from.isEmpty()&&from["visible"].toBool(),"source is not a visible tree member");
    auto f=rect(from["frame"].toObject());const bool horizontal=heading=="east"||heading=="west";const double sign=heading=="east"||heading=="south"?1:-1;
    const double primary=horizontal?f.x+f.width/2:f.y+f.height/2,orthogonal=horizontal?f.y+f.height/2:f.x+f.width/2;
    using Score=std::tuple<int,double,double,double,QString>;Score best;bool found=false;QJsonObject chosen;
    for(auto v:layout["placements"].toArray()) {
        auto p=v.toObject();if(p["uuid"]==QJsonValue(sourceUuid)||!p["visible"].toBool())continue;
        auto r=rect(p["frame"].toObject());double delta=sign*((horizontal?r.x+r.width/2:r.y+r.height/2)-primary);if(delta<=0.001)continue;
        auto offset=std::abs((horizontal?r.y+r.height/2:r.x+r.width/2)-orthogonal);
        auto overlap=horizontal?std::min(f.y+f.height,r.y+r.height)-std::max(f.y,r.y):std::min(f.x+f.width,r.x+r.width)-std::max(f.x,r.x);
        auto edge=std::max(0.0,delta-(horizontal?f.width+r.width:f.height+r.height)/2);
        Score score{overlap>0.001?0:1,edge,offset,std::hypot(delta,offset),p["uuid"].toString()};
        if(!found||score<best){best=score;chosen=p;found=true;}
    }
    need(found,"no visible neighbor in this direction");
    chosen["bridge-instance"]=layout["bridge-instance"];chosen["sequence"]=layout["sequence"];chosen["source"]=from;chosen["direction"]=heading;chosen["context-id"]=c.id;chosen["context-generation"]=QString::number(c.generation);chosen["dry-run"]=true;chosen["desktop-control"]=false;return chosen;
}
QJsonObject ContextStore::reconcile(const BridgeState &b,const QString &selector,bool removeHidden,bool removeIneligible) const {
    need(!removeHidden||b.hasHiddenRelease(),"hidden-window reconciliation requires geometry-hidden-release");
    auto o=output(b,selector);auto k=key(o);auto c=contexts_.value(k);
    QMap<QString,QJsonObject> live;for(auto v:b.windows()["windows"].toArray()){auto w=v.toObject();live[w["uuid"].toString()]=w;}
    QSet<QString> members;for(auto uuid:c.members)members.insert(uuid);
    QMap<QString,QString> owners;for(const auto &context:contexts_)for(auto uuid:context.members)owners[uuid]=context.id;
    QSet<QString> floating;for(const auto &context:contexts_)floating.unite(context.floating);
    QJsonArray add,remove,blocked,excluded,forgetFloating;for(auto uuid:c.floating)if(!live.contains(uuid)||!live[uuid]["scratchpad"].toString().isEmpty())forgetFloating.append(uuid);
    for(auto uuid:c.members) {
        if(!live.contains(uuid)){remove.append(QJsonObject{{"uuid",uuid},{"reason","missing"}});continue;}
        auto popupPolicy=rules_.evaluate(live[uuid]);
        if(popupPolicy["automatic-popup"].toBool()&&popupPolicy["action"]=="float"){remove.append(QJsonObject{{"uuid",uuid},{"reason","popup"}});continue;}
        auto reason=unavailable(live[uuid],o);if(reason=="scratchpad"||(removeIneligible&&reason=="ineligible")||(removeHidden&&(reason=="minimized"||reason=="fullscreen"))){remove.append(QJsonObject{{"uuid",uuid},{"reason",reason}});continue;}if(!reason.isEmpty())blocked.append(QJsonObject{{"uuid",uuid},{"id",live[uuid]["id"]},{"reason",reason}});
    }
    // Native UUID ordering makes batch insertion independent of observer public IDs.
    for(auto it=live.begin();it!=live.end();++it) {
        auto w=it.value();if(members.contains(it.key()))continue;
        auto reason=unavailable(w,o);
        if(reason.isEmpty()&&owners.contains(it.key()))reason="other-stored-context";
        if(reason.isEmpty()&&floating.contains(it.key()))reason="explicit-float";
        auto policy=rules_.evaluate(w);if(reason.isEmpty()&&policy["action"]=="float")reason="rule-float";
        QJsonObject entry{{"rule",policy["matched"]},{"uuid",it.key()},{"id",w["id"]},{"app",w["app"]},{"title",w["title"]}};
        if(reason.isEmpty())add.append(entry);else {entry["reason"]=reason;excluded.append(entry);}
    }
    return {{"forget-floating",forgetFloating},{"dry-run",true},{"desktop-control",false},{"bridge-instance",b.windows()["instance"]},{"sequence",b.windows()["sequence"]},{"context-id",c.id},{"context-generation",QString::number(c.generation)},{"output",o["name"]},{"add",add},{"remove",remove},{"blocked",blocked},{"excluded",excluded},{"within-capacity",c.members.size()-remove.size()+add.size()<=20}};
}
QJsonObject ContextStore::plan(const BridgeState &b,const QString &selector,bool allowEmpty) const {
    auto o=output(b,selector);auto k=key(o);need(contexts_.contains(k),"no stored tree for this context");const auto &c=contexts_[k];need(allowEmpty||!c.members.isEmpty(),"context tree is empty");
    QMap<QString,QJsonObject> live;for(auto v:b.windows()["windows"].toArray()){auto w=v.toObject();live[w["uuid"].toString()]=w;}
    std::map<WindowId,Size> minimums;
    for(auto it=c.members.begin();it!=c.members.end();++it) {
        need(live.contains(it.value()),"context contains an unavailable window; restore it or explicitly prune closed members");auto w=live[it.value()];
        need(w["scratchpad"].toString().isEmpty()&&w["candidate"].toBool()&&!w["minimized"].toBool()&&!w["fullscreen"].toBool()&&w["output"]==o["name"],"context member is currently ineligible or on another output");
        auto ds=w["desktops"].toArray(),as=w["activities"].toArray();need((ds.isEmpty()||ds.contains(o["current-desktop"]))&&(as.isEmpty()||as.contains(o["current-activity"])),"context member moved to another desktop/Activity");auto min=w["minimum"].toObject();minimums[it.key()]={min["w"].toDouble(),min["h"].toDouble()};
    }
    const auto area=rect(o["work-area"].toObject());auto whole=[](double value){return std::floor(value)==value;};
    bool pixelAligned=whole(area.x)&&whole(area.y)&&whole(area.width)&&whole(area.height)&&whole(c.gap)&&whole(c.padding.top)&&whole(c.padding.right)&&whole(c.padding.bottom)&&whole(c.padding.left);
    for(auto [id,minimum]:minimums){(void)id;pixelAligned=pixelAligned&&whole(minimum.width)&&whole(minimum.height);}
    auto layout=c.tree.layout(area,c.padding,c.gap,minimums);
    if(pixelAligned) {
        auto snapped=layout;
        for(auto &p:snapped) {
            // Round shared edges, never independent widths. Integer minima and
            // gaps remain valid, including across negative coordinate origins.
            auto snap=[](double value){return std::floor(value+0.5);};double right=snap(p.frame.x+p.frame.width),bottom=snap(p.frame.y+p.frame.height);p.frame.x=snap(p.frame.x);p.frame.y=snap(p.frame.y);p.frame.width=right-p.frame.x;p.frame.height=bottom-p.frame.y;
            auto minimum=minimums.at(p.window);if(p.frame.width<=0||p.frame.height<=0||p.frame.width<minimum.width||p.frame.height<minimum.height)pixelAligned=false;
        }
        if(pixelAligned)layout=std::move(snapped);
    }
    QJsonArray placements,selected;
    QJsonObject zoomFrame{{"x",area.x+c.padding.left},{"y",area.y+c.padding.top},{"w",area.width-c.padding.left-c.padding.right},{"h",area.height-c.padding.top-c.padding.bottom}};
    bool zooming=false;const bool splitRoot=c.tree.snapshot().nodes.size()>1;
    for(auto p:layout) {
        const bool zoomed=splitRoot&&c.zoomed.contains(c.members[p.window]);
        if(zoomed){p.frame=rect(zoomFrame);zooming=true;}
        auto tileFrame=QJsonObject{{"x",p.frame.x},{"y",p.frame.y},{"w",p.frame.width},{"h",p.frame.height}};
        auto w=live[c.members[p.window]];auto maximum=w["maximum"].toObject();
        if(!zoomed&&maximum["w"].toDouble()>0&&maximum["h"].toDouble()>0){p.frame.width=std::min(p.frame.width,maximum["w"].toDouble());p.frame.height=std::min(p.frame.height,maximum["h"].toDouble());}
        selected.append(w["id"]);placements.append(QJsonObject{{"tile-frame",tileFrame},{"id",w["id"]},{"uuid",w["uuid"]},{"visible",p.visible},{"zoom-fullscreen",zoomed},{"frame",QJsonObject{{"x",p.frame.x},{"y",p.frame.y},{"w",p.frame.width},{"h",p.frame.height}}}});
    }
    QJsonObject result{{"pixel-aligned",pixelAligned},{"floating",floatingJson(c.floating)},{"dry-run",true},{"desktop-control",false},{"bridge-instance",b.windows()["instance"]},{"sequence",b.windows()["sequence"]},{"context-id",c.id},{"context-generation",QString::number(c.generation)},{"context",QJsonObject{{"output-id",o["id"]},{"output",o["name"]},{"desktop",o["current-desktop"]},{"activity",o["current-activity"]}}},{"work-area",o["work-area"]},{"padding",paddingJson(c.padding)},{"gap",c.gap},{"selected",selected},{"placements",placements}};
    if(zooming)result["zoom-frame"]=zoomFrame;
    return result;
}
void ContextStore::setFloatPopups(const QString &value) {need(value=="on"||value=="off","float_popups must be on or off");rules_.setFloatPopups(value=="on");}
void ContextStore::setWindowBorderRadius(const QString &radius) {windowBorderRadius_=compat::windowBorderRadius(radius);}
void ContextStore::setInsertFeedbackColor(const QString &color) {insertFeedbackColor_=compat::insertFeedbackColor(color);}
QJsonObject ContextStore::state() const {
    QJsonArray contexts;
    for(const auto &c:contexts_) {QJsonArray members;for(auto it=c.members.begin();it!=c.members.end();++it)members.append(QJsonObject{{"id",QString::number(qulonglong(it.key()))},{"uuid",it.value()}});
        contexts.append(QJsonObject{{"id",c.id},{"output-id",c.output},{"desktop",c.desktop},{"activity",c.activity},{"generation",QString::number(c.generation)},{"gap",c.gap},{"padding",paddingJson(c.padding)},{"floating",floatingJson(c.floating)},{"members",members},{"tree",QJsonDocument::fromJson(encodeSnapshot(c.tree)).object()}});}
    return {{"version",8},{"settings",QJsonObject{{"float_popups",floatPopups()}, {"insert_feedback_color",insertFeedbackColor_},{"window_border_radius",windowBorderRadius_}}},{"rules",rules_.state()},{"next-window",QString::number(qulonglong(nextWindow_))},{"contexts",contexts}};
}
ContextStore ContextStore::restore(const QJsonObject &s) {
    need((s["version"]==QJsonValue(1)||s["version"]==QJsonValue(2)||s["version"]==QJsonValue(3)||s["version"]==QJsonValue(4)||s["version"]==QJsonValue(5)||s["version"]==QJsonValue(6)||s["version"]==QJsonValue(7)||s["version"]==QJsonValue(8))&&s["contexts"].isArray()&&s["contexts"].toArray().size()<=64,"invalid context snapshot");ContextStore store;if(s["version"].toInt()>=3){need(s["rules"].isArray(),"invalid saved rules");store.rules_=RuleSet::restore(s["rules"].toArray());}else need(!s.contains("rules")||(s["rules"].isArray()&&s["rules"].toArray().isEmpty()),"legacy state cannot contain rules");store.nextWindow_=integer(s["next-window"]);need(store.nextWindow_>0,"invalid next window identity");QSet<QString> uuids,contextIds;QSet<quint64> layoutIds;
    if(s["version"]==QJsonValue(6)){need(s["settings"].isObject(),"invalid saved settings");auto settings=s["settings"].toObject();need(settings.size()==1&&settings["insert_window_border_color"].isString(),"invalid saved preview color");store.setInsertFeedbackColor(settings["insert_window_border_color"].toString());}
    if(s["version"].toInt()>=7){need(s["settings"].isObject(),"invalid saved settings");auto settings=s["settings"].toObject();if(s["version"]==QJsonValue(8)){need(settings["float_popups"].isBool(),"invalid saved popup policy");store.rules_.setFloatPopups(settings.take("float_popups").toBool());}need(settings.size()==2&&settings["insert_feedback_color"].isString()&&settings["window_border_radius"].isDouble(),"invalid saved preview settings");store.setInsertFeedbackColor(settings["insert_feedback_color"].toString());auto radius=settings["window_border_radius"].toDouble();need(std::isfinite(radius)&&std::floor(radius)==radius&&radius>=0&&radius<=20,"invalid saved radius");store.setWindowBorderRadius(QString::number(int(radius)));}
    for(auto v:s["contexts"].toArray()) {need(v.isObject(),"invalid stored context");auto o=v.toObject();Context c;c.id=QUuid(o["id"].toString()).toString(QUuid::WithoutBraces);need(!QUuid(c.id).isNull()&&!contextIds.contains(c.id),"invalid context identity");contextIds.insert(c.id);c.output=o["output-id"].toString();c.desktop=QUuid(o["desktop"].toString()).toString(QUuid::WithoutBraces);c.activity=o["activity"].toString();need(!c.output.isEmpty()&&!QUuid(c.desktop).isNull()&&o["activity"].isString(),"invalid context key");c.generation=integer(o["generation"]);need(o["gap"].isDouble()&&std::isfinite(o["gap"].toDouble())&&o["gap"].toDouble()>=0,"invalid gap");c.gap=o["gap"].toDouble();if(s["version"]!=QJsonValue(1))c.padding=paddingFromJson(o["padding"]);need(o["tree"].isObject(),"invalid context tree");c.tree=decodeSnapshot(QJsonDocument(o["tree"].toObject()).toJson());need(o["members"].isArray()&&o["members"].toArray().size()<=20,"invalid members");
        for(auto m:o["members"].toArray()){need(m.isObject(),"invalid member");auto member=m.toObject();auto id=integer(member["id"]);auto parsed=QUuid(member["uuid"].toString());auto uuid=parsed.toString(QUuid::WithoutBraces);need(id>0&&id<store.nextWindow_&&!layoutIds.contains(id)&&!parsed.isNull()&&!uuids.contains(uuid),"duplicate or invalid context member");layoutIds.insert(id);uuids.insert(uuid);c.members[id]=uuid;}
        if(s["version"].toInt()>=4) {
            need(o["floating"].isArray()&&o["floating"].toArray().size()<=256,"invalid floating window list");
            for(auto v:o["floating"].toArray()){auto parsed=QUuid(v.toString());auto uuid=parsed.toString(QUuid::WithoutBraces);need(v.isString()&&!parsed.isNull()&&!uuids.contains(uuid),"duplicate or invalid floating UUID");c.floating.insert(uuid);uuids.insert(uuid);}
        } else need(!o.contains("floating")||(o["floating"].isArray()&&o["floating"].toArray().isEmpty()),"legacy context cannot contain floating windows");
        QSet<quint64> treeIds;for(auto r:c.tree.snapshot().nodes)for(auto id:r.windows)treeIds.insert(id);QSet<quint64> memberIds;for(auto id:c.members.keys())memberIds.insert(id);need(treeIds==memberIds,"tree/membership mismatch");
        auto k=key(QJsonObject{{"id",c.output},{"current-desktop",c.desktop},{"current-activity",c.activity}});need(!store.contexts_.contains(k),"duplicate context key");store.contexts_[k]=std::move(c);
    }return store;
}
}
