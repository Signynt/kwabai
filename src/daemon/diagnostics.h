#pragma once
#include "client.h"
#include "store.h"
namespace kwabai {
QJsonObject geometryDiagnostics(const BridgeClient &bridge,const ContextStore &contexts,const QString &output);
}
