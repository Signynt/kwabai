#pragma once
#include "json.h"
#include <QJsonObject>
#include <QStringList>
namespace kwabai {
struct Model {
    Tree tree;
    Rect area{0,0,2560,1440};
    Insets padding{};
    double gap=10;
    quint64 generation=0;
    QJsonObject state() const;
    static Model restore(const QJsonObject &);
    QString inspect() const;
    void mutate(const QStringList &args);
};
}
