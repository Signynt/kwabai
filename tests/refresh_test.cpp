#include "client.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QThread>
#include <iostream>
#include <stdexcept>
void check(bool ok){if(!ok)throw std::runtime_error("explicit snapshot refresh assertion failed");}
template<class F>void until(F ready){QElapsedTimer clock;clock.start();while(!ready()){if(clock.elapsed()>2000)throw std::runtime_error("snapshot refresh timed out");QCoreApplication::processEvents();QThread::msleep(1);}}
class DelayedBridge : public QObject,protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kwabai.Bridge1")
public:
    QList<QDBusMessage> requests;
public Q_SLOTS:
    QString Snapshot(){setDelayedReply(true);requests.append(message());return {};}
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    auto server=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"refresh-test-server");DelayedBridge fake;
    check(server.registerService("org.kde.KWin"));check(server.registerObject("/org/kwabai/Bridge",&fake,QDBusConnection::ExportAllSlots));
    kwabai::BridgeClient client;client.start();until([&]{return fake.requests.size()==1;});
    auto reply=[&](int request){QJsonObject snapshot{{"protocol",1},{"instance","11111111-1111-4111-8111-111111111111"},{"sequence",QString::number(request+1)},{"kwin-version","test"},{"capabilities",QJsonArray{"snapshot"}},{"windows",QJsonArray{}}};check(server.send(fake.requests[request].createReply(QString::fromUtf8(QJsonDocument(snapshot).toJson()))));};
    // The older in-flight response must not satisfy a post-ack refresh.
    auto token=client.refresh();check(token==1);check(client.refresh()==token);reply(0);
    until([&]{return fake.requests.size()==2;});check(client.snapshotRevision()==token);check(client.state().connected());reply(1);
    until([&]{return client.snapshotRevision()>token;});check(client.state().windows()["sequence"]=="2");
    // With no request in flight, explicit refresh starts immediately and the
    // returned token still refers to the preceding request, not the new one.
    token=client.refresh();check(token==2);until([&]{return fake.requests.size()==3;});reply(2);until([&]{return client.snapshotRevision()>token;});check(client.state().windows()["sequence"]=="3");
    std::cout<<"PASS fresh-after-request revisions and coalesced in-flight refreshes\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
#include "refresh_test.moc"
