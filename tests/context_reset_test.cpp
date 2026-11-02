#include "geometry.h"
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("native context reset assertion failed");}
template<class F>void rejects(F fn){bool fail=false;try{fn();}catch(const std::invalid_argument &){fail=true;}check(fail);}
QString id(int n){return QString("aaaaaaaa-aaaa-4aaa-8aaa-%1").arg(n,12,16,QChar('0'));}
QJsonObject rect(QRectF r){return {{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()}};}
int main(){try{
 auto a=id(1);QRectF actual(100,100,800,600);int writes=0;bool alive=true;
 GeometryTransactions engine([&](const QString &uuid)->std::optional<QRectF>{if(uuid==a&&alive)return actual;return {};},[&](const QString &,QRectF r){++writes;actual=r;return true;});
 QJsonObject output{{"id","one"},{"name","DP-1"},{"current-desktop",id(9)},{"current-activity","work"},{"work-area",rect({0,0,2560,1440})}};
 auto snapshot=[&]{
    QJsonObject window{{"uuid",a},{"candidate",true},{"frame",rect(actual)},{"output","DP-1"},{"minimum",QJsonObject{{"w",100},{"h",100}}}};
    return QJsonObject{{"instance",id(8)},{"outputs",QJsonArray{output}},{"windows",QJsonArray{window}}};
 };
 QJsonObject context{{"output-id","one"},{"output","DP-1"},{"desktop",id(9)},{"activity","work"}};
 QJsonObject placement{{"uuid",a},{"expected-frame",rect(actual)},{"frame",rect({10,10,2540,1420})}};
 QJsonObject request{{"protocol",1},{"id",id(10)},{"request-sequence","1"},{"bridge-instance",id(8)},{"context-id",id(11)},{"context-generation","1"},{"context",context},{"work-area",output["work-area"]},{"placements",QJsonArray{placement}}};
 engine.apply("owner",request,snapshot(),0);rejects([&]{engine.forget("owner",id(10));});engine.tick(40);engine.tick(80);engine.tick(120);auto before=writes;rejects([&]{engine.forget("stranger",id(10));});
 engine.beginInteraction(a,"move",140);rejects([&]{engine.forget("owner",id(10));});actual.translate(-1500,10);engine.finishInteraction(a,{-500,100},150);alive=false;
 auto result=engine.forget("owner",id(10));check(result["state"]=="forgotten"&&result["completed"].toBool()&&result["success"].toBool()&&result["acknowledged"].toBool()&&result["active-transaction"].toString().isEmpty());check(!engine.owns(a)&&writes==before);engine.forget("owner",id(10));rejects([&]{engine.restore("owner",id(10),200);});
 alive=true;auto endpoint=actual;engine.release("owner",200);engine.teardown();check(actual==endpoint&&writes==before);
 // New ownership captures the current endpoint, never the discarded baseline.
 request["id"]=id(12);request["request-sequence"]="2";request["context-generation"]="2";request["placements"]=QJsonArray{QJsonObject{{"uuid",a},{"expected-frame",rect(actual)},{"frame",rect({10,10,2540,1420})}}};engine.apply("owner",request,snapshot(),220);engine.tick(260);engine.tick(300);engine.tick(340);engine.forget("owner",id(12));engine.tick(390);engine.tick(430);engine.tick(470);check(actual==QRectF(10,10,2540,1420));
 std::cout<<"PASS reset ownership isolation, no writes for moved/missing windows, gesture/pending rejection and fresh ownership\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
