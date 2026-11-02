#include "rules.h"
#include "scratchpads.h"
#include "../compat/grid.h"
#include <QUuid>
#include <stdexcept>
#include <regex.h>
namespace kwabai {
class PosixRegex {
public:
    explicit PosixRegex(const QString &pattern){auto bytes=pattern.toUtf8();if(regcomp(&value_,bytes.constData(),REG_EXTENDED|REG_NOSUB)!=0)throw std::invalid_argument("invalid POSIX extended rule expression");}
    ~PosixRegex(){regfree(&value_);}
    bool matches(const QString &text) const {auto bytes=text.toUtf8();return regexec(&value_,bytes.constData(),0,nullptr,0)==0;}
private:
    regex_t value_{};
};
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
QRegularExpression glob(const QString &pattern) {
    need(pattern.size()<=256,"rule pattern exceeds 256 characters");
    QRegularExpression result(QRegularExpression::wildcardToRegularExpression(pattern,QRegularExpression::NonPathWildcardConversion),QRegularExpression::DotMatchesEverythingOption);
    need(result.isValid(),"invalid rule glob");return result;
}
}
void RuleSet::add(const QString &label,const QString &action,const QString &app,const QString &title,const QString &syntax,bool anonymous,const QString &layer,const QString &scratchpad,const QString &grid) {
    static const QRegularExpression labelPattern(QRegularExpression::anchoredPattern("[A-Za-z0-9_-]{1,64}"));
    need(syntax=="posix"?(!label.isEmpty()&&label.size()<=256&&!label.contains(QChar(0))):labelPattern.match(label).hasMatch(),"rule label must be 1-64 letters, digits, underscores or hyphens");
    need(action=="float"||action=="tile"||(action=="inherit"&&(!layer.isEmpty()||!scratchpad.isEmpty()||!grid.isEmpty())),"rule requires manage, layer, scratchpad or grid action");
    if(!grid.isEmpty())compat::parseGrid(grid);
    need(scratchpad.isEmpty()||Scratchpads::validLabel(scratchpad),"invalid or reserved scratchpad label");
    need(layer.isEmpty()||layer=="above"||layer=="normal"||layer=="below"||layer=="auto","invalid rule layer");
    need(syntax=="posix"||!app.isEmpty()||!title.isEmpty(),"rule requires --app or --title");need(rules_.size()<64,"rule capacity exceeded");
    for(const auto &rule:rules_)need(rule.label!=label,"rule label already exists; remove it first");
    need((syntax=="glob"||syntax=="posix")&&(!anonymous||syntax=="posix"),"invalid rule syntax");need(app.size()<=256&&title.size()<=256&&!app.contains(QChar(0))&&!title.contains(QChar(0)),"invalid or oversized rule pattern");
    Rule rule;rule.label=label;rule.action=action;rule.app=app;rule.title=title;rule.syntax=syntax;rule.anonymous=anonymous;rule.layer=layer;rule.scratchpad=scratchpad;rule.grid=grid;
    if(syntax=="glob"){rule.appMatcher=glob(app.isEmpty()?"*":app);rule.titleMatcher=glob(title.isEmpty()?"*":title);}
    else {if(!app.isEmpty())rule.appRegex=std::make_shared<PosixRegex>(app);if(!title.isEmpty())rule.titleRegex=std::make_shared<PosixRegex>(title);}
    rules_.append(rule);
}
void RuleSet::edit(const QStringList &a) {
    need(!a.isEmpty(),"rule add/remove arguments required");
    if(a[0]=="add-posix") {
        need(a.size()>=5&&a.size()<=8,"invalid POSIX rule arguments");auto layer=a.size()>=6?a[5]:QString{};auto scratchpad=a.size()>=7?a[6]:QString{};auto grid=a.size()==8?a[7]:QString{};auto label=a[1];bool anonymous=label.isEmpty();if(anonymous)label="__yabai_"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        RuleSet candidate=*this;
        if(anonymous)for(qsizetype i=candidate.rules_.size();i>0;--i){const auto &r=candidate.rules_[i-1];if(r.syntax=="posix"&&r.anonymous&&r.action==a[2]&&r.app==a[3]&&r.title==a[4]&&r.layer==layer&&r.scratchpad==scratchpad&&r.grid==grid)candidate.rules_.removeAt(i-1);}

        if(!anonymous)for(qsizetype i=0;i<candidate.rules_.size();++i)if(candidate.rules_[i].label==label){candidate.rules_.removeAt(i);break;}
        candidate.add(label,a[2],a[3],a[4],"posix",anonymous,layer,scratchpad,grid);*this=std::move(candidate);return;
    }
    if(a[0]=="remove-posix") {
        need(a.size()==2,"rule --remove INDEX|LABEL");bool ok=false;auto index=a[1].toInt(&ok);int current=0;
        for(qsizetype i=0;i<rules_.size();++i)if(rules_[i].syntax=="posix"){if((ok&&index==current)||(!ok&&!rules_[i].anonymous&&rules_[i].label==a[1])){rules_.removeAt(i);return;}++current;}
        throw std::invalid_argument("compatible rule selector not found");
    }
    if(a[0]=="remove") {
        need(a.size()==2,"rule remove LABEL");
        for(qsizetype i=0;i<rules_.size();++i)if(rules_[i].label==a[1]){rules_.removeAt(i);return;}
        throw std::invalid_argument("rule label not found");
    }
    need(a[0]=="add"&&a.size()>=5&&(a.size()-3)%2==0,"rule add LABEL float|tile --app GLOB [--title GLOB], or --title GLOB alone");
    QString app,title;bool hasApp=false,hasTitle=false;
    for(qsizetype i=3;i<a.size();i+=2) {
        need(!a[i+1].isEmpty(),"empty rule pattern");
        if(a[i]=="--app"){need(!hasApp,"duplicate --app");hasApp=true;app=a[i+1];}
        else if(a[i]=="--title"){need(!hasTitle,"duplicate --title");hasTitle=true;title=a[i+1];}
        else throw std::invalid_argument("unknown rule filter; use --app or --title");
    }
    add(a[1],a[2],app,title);
}
QJsonArray RuleSet::state() const {
    QJsonArray result;for(const auto &r:rules_){QJsonObject entry{{"syntax",r.syntax},{"anonymous",r.anonymous},{"label",r.label},{"action",r.action},{"app",r.app},{"title",r.title}};if(!r.layer.isEmpty())entry["layer"]=r.layer;if(!r.scratchpad.isEmpty())entry["scratchpad"]=r.scratchpad;if(!r.grid.isEmpty())entry["grid"]=r.grid;result.append(entry);}return result;
}
QJsonArray RuleSet::listPosix() const {
    QJsonArray result;int index=0;for(const auto &r:rules_)if(r.syntax=="posix"){QJsonObject entry{{"index",index++},{"label",r.anonymous?QString{}:r.label},{"app",r.app},{"title",r.title},{"one-shot",false}};if(r.action!="inherit")entry["manage"]=r.action=="tile";if(!r.layer.isEmpty()){entry["layer"]=r.layer;entry["sub-layer"]=r.layer;}if(!r.scratchpad.isEmpty())entry["scratchpad"]=r.scratchpad;if(!r.grid.isEmpty())entry["grid"]=r.grid;result.append(entry);}return result;
}
QJsonObject RuleSet::list() const {return {{"rules",state()},{"precedence","last-match-wins"},{"scope","admission"},{"default-action","tile"}};}
QJsonObject RuleSet::evaluate(const QJsonObject &window) const {
    const bool popup=floatPopups_&&(window["is-modal"].toBool()||window["is-transient"].toBool());
    QJsonObject result{{"action",popup?"float":"tile"},{"matched",QJsonValue(QJsonValue::Null)},{"automatic-popup",popup}};
    for(const auto &r:rules_) {
        bool match=r.syntax=="posix"?((!r.appRegex||r.appRegex->matches(window["app"].toString()))&&(!r.titleRegex||r.titleRegex->matches(window["title"].toString()))):(r.appMatcher.match(window["app"].toString()).hasMatch()&&r.titleMatcher.match(window["title"].toString()).hasMatch());
        if(match){if(r.action!="inherit")result["action"]=r.action;result["matched"]=r.label;if(!r.layer.isEmpty()){result["layer"]=r.layer;result["layer-rule"]=r.label;}if(!r.scratchpad.isEmpty()){result["scratchpad"]=r.scratchpad;result["scratchpad-rule"]=r.label;}if(!r.grid.isEmpty()){result["grid"]=r.grid;result["grid-rule"]=r.label;}}
    }
    return result;
}
QJsonObject RuleSet::test(const BridgeState &bridge,const QString &selector) const {
    need(bridge.connected(),"connected bridge required to test a rule");QJsonObject selected;int count=0;auto uuid=QUuid(selector);
    for(auto v:bridge.windows()["windows"].toArray()) {
        auto w=v.toObject();if((selector=="focused"&&w["focused"].toBool())||w["id"]==QJsonValue(selector)||(!uuid.isNull()&&QUuid(w["uuid"].toString())==uuid)){selected=w;++count;}
    }
    need(count==1,"rule test selector missing or ambiguous");auto result=evaluate(selected);result["id"]=selected["id"];result["uuid"]=selected["uuid"];result["app"]=selected["app"];result["title"]=selected["title"];result["eligible"]=selected["candidate"];result["scope"]="admission";result["windows-moved"]=false;return result;
}
RuleSet RuleSet::restore(const QJsonArray &rules) {
    RuleSet result;need(rules.size()<=64,"rule capacity exceeded");
    for(auto v:rules){need(v.isObject(),"invalid saved rule");auto r=v.toObject();auto grid=r.take("grid");need(grid.isUndefined()||grid.isString(),"invalid saved grid");auto scratchpad=r.take("scratchpad");need(scratchpad.isUndefined()||scratchpad.isString(),"invalid saved scratchpad");auto layer=r.take("layer");need(layer.isUndefined()||layer.isString(),"invalid saved layer");need(((r.size()==4&&!r.contains("syntax")&&!r.contains("anonymous"))||(r.size()==6&&r["syntax"].isString()&&r["anonymous"].isBool()))&&r["label"].isString()&&r["action"].isString()&&r["app"].isString()&&r["title"].isString(),"invalid saved rule fields");result.add(r["label"].toString(),r["action"].toString(),r["app"].toString(),r["title"].toString(),r.contains("syntax")?r["syntax"].toString():"glob",r["anonymous"].toBool(),layer.toString(),scratchpad.toString(),grid.toString());}
    return result;
}
}
