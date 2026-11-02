#include "model.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace kwabai {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
WindowId id(const QString &s){bool ok=false;auto n=s.toULongLong(&ok);need(ok&&n&&QString::number(n)==s,"window ID must be a nonzero decimal integer");return n;}
double number(const QString &s){bool ok=false;double n=s.toDouble(&ok);need(ok&&std::isfinite(n),"expected finite number");return n;}
Direction direction(const QString &s){if(s=="north")return Direction::North;if(s=="east")return Direction::East;if(s=="south")return Direction::South;if(s=="west")return Direction::West;throw std::invalid_argument("expected north/east/south/west");}
}
QJsonObject Model::state() const {
    return {{"version",1},{"generation",QString::number(generation)},
        {"tree",QJsonDocument::fromJson(encodeSnapshot(tree)).object()},
        {"area",QJsonArray{area.x,area.y,area.width,area.height}},
        {"padding",QJsonArray{padding.top,padding.right,padding.bottom,padding.left}},{"gap",gap}};
}
Model Model::restore(const QJsonObject &s) {
    need(s.value("version")==QJsonValue(1),"unsupported model version");Model m;
    auto gen=s.value("generation");bool ok=false;m.generation=gen.toString().toULongLong(&ok);
    need(gen.isString()&&ok&&QString::number(m.generation)==gen.toString(),"invalid generation");
    need(s.value("tree").isObject(),"invalid tree");m.tree=decodeSnapshot(QJsonDocument(s.value("tree").toObject()).toJson());
    auto tuple=[&](const char *key) {
        need(s.value(key).isArray(),"invalid geometry tuple");auto a=s.value(key).toArray();need(a.size()==4,"invalid geometry tuple");
        for(auto v:a)need(v.isDouble()&&std::isfinite(v.toDouble()),"invalid geometry value");
        return a;
    };
    auto a=tuple("area"),p=tuple("padding");m.area={a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};
    m.padding={p[0].toDouble(),p[1].toDouble(),p[2].toDouble(),p[3].toDouble()};
    need(s.value("gap").isDouble(),"invalid gap");m.gap=s.value("gap").toDouble();
    (void)m.tree.layout(m.area,m.padding,m.gap);return m;
}
QString Model::inspect() const {
    QJsonArray windows;
    for(auto p:tree.layout(area,padding,gap))windows.append(QJsonObject{{"id",QString::number(qulonglong(p.window))},
        {"leaf",QString::number(qulonglong(p.leaf))},{"visible",p.visible},
        {"frame",QJsonObject{{"x",p.frame.x},{"y",p.frame.y},{"w",p.frame.width},{"h",p.frame.height}}}});
    auto s=state();s["windows"]=windows;return QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Indented));
}
void Model::mutate(const QStringList &a) {
    need(!a.isEmpty(),"model operation missing");auto op=a[0];
    if(op=="add") {need(a.size()==2||a.size()==4||a.size()==5,"model add ID [TARGET DIRECTION [RATIO]]");tree.add(id(a[1]),a.size()>2?id(a[2]):0,a.size()>2?direction(a[3]):Direction::East,a.size()>4?number(a[4]):0.5);}
    else if(op=="remove"||op=="activate") {need(a.size()==2,"model remove/activate ID");if(op=="remove")tree.remove(id(a[1]));else tree.activate(id(a[1]));}
    else if(op=="swap"||op=="stack") {need(a.size()==3,"model swap/stack SOURCE TARGET");if(op=="swap")tree.swap(id(a[1]),id(a[2]));else tree.stack(id(a[1]),id(a[2]));}
    else if(op=="warp") {need(a.size()==4||a.size()==5,"model warp SOURCE TARGET DIRECTION [RATIO]");tree.warp(id(a[1]),id(a[2]),direction(a[3]),a.size()==5?number(a[4]):0.5);}
    else if(op=="ratio") {need(a.size()==4&&(a[2]=="x"||a[2]=="y"),"model ratio ID x|y RATIO");tree.setRatio(id(a[1]),a[2]=="x"?Axis::X:Axis::Y,number(a[3]));}
    else if(op=="gap") {need(a.size()==2,"model gap PIXELS");gap=number(a[1]);}
    else if(op=="area") {need(a.size()==5,"model area X Y WIDTH HEIGHT");area={number(a[1]),number(a[2]),number(a[3]),number(a[4])};}
    else throw std::invalid_argument("unsupported model operation");
    tree.validate();(void)tree.layout(area,padding,gap);
}
}
