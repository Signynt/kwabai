#pragma once
#include "state.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QList>
#include <memory>
namespace kwabai {
class PosixRegex;
// Admission policy only: changing rules never releases a managed window.
class RuleSet {
public:
    void edit(const QStringList &arguments);
    bool floatPopups() const {return floatPopups_;}
    void setFloatPopups(bool enabled) {floatPopups_=enabled;}
    QJsonArray listPosix() const;
    QJsonArray state() const;
    QJsonObject list() const;
    QJsonObject evaluate(const QJsonObject &window) const;
    QJsonObject test(const BridgeState &bridge,const QString &selector) const;
    static RuleSet restore(const QJsonArray &rules);
private:
    struct Rule {QString label,action,app,title,layer,scratchpad,grid,syntax="glob";bool anonymous=false;QRegularExpression appMatcher,titleMatcher;std::shared_ptr<PosixRegex> appRegex,titleRegex;};
    QList<Rule> rules_;
    bool floatPopups_=true;
    void add(const QString &label,const QString &action,const QString &app,const QString &title,const QString &syntax="glob",bool anonymous=false,const QString &layer={},const QString &scratchpad={},const QString &grid={});
};
}
