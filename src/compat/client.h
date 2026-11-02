#pragma once
#include <QJsonObject>
#include <QStringList>
#include <functional>
namespace kwabai::compat {
struct Result {int status=0;QString output,error;};
using Call=std::function<QJsonObject(const QStringList &)>;
Result run(const QStringList &arguments,const Call &call);
}
