#pragma once
#include <QStringList>
#include <QList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace kwabai::compat {
struct SpacingEdit {bool relative;QList<qint32> values;};
inline SpacingEdit parseSpacing(const QString &text,int count) {
    auto fields=text.split(':');
    if((count!=1&&count!=4)||fields.size()!=count+1||(fields[0]!="abs"&&fields[0]!="rel"))throw std::invalid_argument("expected abs|rel:INTEGER for gap or abs|rel:TOP:BOTTOM:LEFT:RIGHT for padding");
    SpacingEdit edit{fields[0]=="rel",{}};
    for(int i=1;i<fields.size();++i) {
        const auto &field=fields[i];int start=field.startsWith('+')||field.startsWith('-')?1:0;
        bool digits=start<field.size();for(int n=start;n<field.size();++n)digits=digits&&field[n]>='0'&&field[n]<='9';
        bool ok=false;auto value=field.toLongLong(&ok);
        if(!digits||!ok||value<std::numeric_limits<qint32>::min()||value>std::numeric_limits<qint32>::max())throw std::invalid_argument("spacing values must be signed 32-bit decimal integers");
        if(!edit.relative&&value<0)throw std::invalid_argument("absolute spacing must be nonnegative");
        edit.values.append(qint32(value));
    }
    return edit;
}
inline QList<double> resolveSpacing(const SpacingEdit &edit,const QList<double> &current) {
    if(current.size()!=edit.values.size())throw std::invalid_argument("spacing field count mismatch");
    QList<double> result;
    for(int i=0;i<current.size();++i) {
        if(!std::isfinite(current[i])||current[i]<0)throw std::invalid_argument("invalid current spacing");
        const double value=edit.relative?std::max(0.0,current[i]+double(edit.values[i])):double(edit.values[i]);
        if(!std::isfinite(value)||value>std::numeric_limits<qint32>::max())throw std::invalid_argument("resulting spacing exceeds the supported integer range");
        result.append(value);
    }
    return result;
}
}
