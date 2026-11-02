#pragma once
#include "state.h"
#include <QStringList>
namespace kwabai {
QJsonObject dryRunPlan(const BridgeState &state,const QString &output,const QStringList &windows);
}
