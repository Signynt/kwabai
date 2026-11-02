#include "json.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <stdexcept>
namespace kwabai {
namespace {
void require(bool ok,const char *message) {if(!ok)throw std::invalid_argument(message);}
QString id(std::uint64_t value) {return QString::number(qulonglong(value));}
std::uint64_t parseId(const QJsonValue &v) {
    require(v.isString(),"snapshot IDs must be decimal strings");
    auto str=v.toString();bool ok=false;auto value=str.toULongLong(&ok);
    require(ok && id(value)==str,"invalid snapshot ID");return value;
}
}
QByteArray encodeSnapshot(const Tree &tree) {
    tree.validate();auto s=tree.snapshot();QJsonArray nodes;
    for(auto &r:s.nodes) {
        QJsonArray windows;for(auto w:r.windows)windows.append(id(w));
        nodes.append(QJsonObject{{"id",id(r.id)},{"first",id(r.first)},{"second",id(r.second)},
            {"axis",r.axis==Axis::X?"x":"y"},{"ratio",r.ratio},{"windows",windows},{"active",id(r.active)}});
    }
    auto bytes=QJsonDocument(QJsonObject{{"version",int(s.version)},{"root",id(s.root)},{"next-node",id(s.nextNode)},{"nodes",nodes}}).toJson(QJsonDocument::Indented);
    require(bytes.size()<=1024*1024,"snapshot exceeds 1 MiB");
    return bytes;
}
Tree decodeSnapshot(const QByteArray &bytes) {
    require(bytes.size()<=1024*1024,"snapshot exceeds 1 MiB");
    QJsonParseError error;auto doc=QJsonDocument::fromJson(bytes,&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),"invalid snapshot JSON");
    auto obj=doc.object();require(obj.value("version").isDouble() && obj.value("version").toDouble()==1,"unsupported snapshot version");
    require(obj.value("nodes").isArray(),"snapshot nodes must be an array");
    Snapshot s;s.root=parseId(obj.value("root"));s.nextNode=parseId(obj.value("next-node"));
    auto nodes=obj.value("nodes").toArray();require(nodes.size()<=4095,"too many snapshot nodes");
    for(auto value:nodes) {
        require(value.isObject(),"invalid snapshot node");auto n=value.toObject();Record r;
        r.id=parseId(n.value("id"));r.first=parseId(n.value("first"));r.second=parseId(n.value("second"));r.active=parseId(n.value("active"));
        require(n.value("axis")==QJsonValue("x")||n.value("axis")==QJsonValue("y"),"invalid axis");
        r.axis=n.value("axis").toString()=="x"?Axis::X:Axis::Y;
        require(n.value("ratio").isDouble(),"invalid ratio");r.ratio=n.value("ratio").toDouble();
        require(n.value("windows").isArray(),"invalid stack array");
        for(auto w:n.value("windows").toArray())r.windows.push_back(parseId(w));
        s.nodes.push_back(std::move(r));
    }
    return Tree::restore(s);
}
}
