#pragma once
#include "client.h"
#include "store.h"
#include <QTimer>
#include <QElapsedTimer>
namespace kwabai {
class LayerPolicy {
public:
    LayerPolicy(BridgeClient &,ContextStore &,bool enabled);
    void pause(bool value){paused_=value;}
    QJsonObject command(const QString &selector,const QString &layer);
    QJsonObject status() const;
private:
    void step();
    bool settle();
    QString signature(const QJsonObject &) const;
    QJsonObject submit(const QJsonObject &,const QString &,bool reset);
    BridgeClient &bridge_;ContextStore &contexts_;bool enabled_,paused_=false;
    QTimer timer_;QElapsedTimer pendingClock_;QString instance_,pending_,pendingWindow_;QMap<QString,QString> seen_,errors_;
};
}
