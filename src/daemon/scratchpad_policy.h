#pragma once
#include "client.h"
#include "store.h"
#include "manager.h"
#include <QTimer>
namespace kwabai {
class ScratchpadPolicy {
public:
    ScratchpadPolicy(BridgeClient &,ContextStore &,ContextManager &,bool enabled);
    void pause(bool value){paused_=value;}
    void overrideWindow(const QString &uuid);
    QJsonObject status() const;
private:
    void step();
    QString signature(const QJsonObject &) const;
    BridgeClient &bridge_;ContextStore &contexts_;ContextManager &manager_;bool enabled_,paused_=false;
    QTimer timer_;QString instance_,pending_,pendingWindow_;QMap<QString,QString> seen_,errors_;
};
}
