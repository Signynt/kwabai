#pragma once
#include <QJsonObject>
#include <QStringList>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace kwabai::compat {
struct Grid {quint32 rows,columns,x,y,width,height;};
inline Grid parseGrid(const QString &text){
    auto fields=text.split(':');if(fields.size()!=6)throw std::invalid_argument("grid requires ROWS:COLS:X:Y:WIDTH:HEIGHT");
    std::array<quint32,6> values{};
    for(int i=0;i<6;++i){bool digits=!fields[i].isEmpty();for(auto c:fields[i])digits=digits&&c>='0'&&c<='9';bool ok=false;auto n=fields[i].toULongLong(&ok);
        if(!digits||!ok||n>std::numeric_limits<quint32>::max())throw std::invalid_argument("grid values must be unsigned 32-bit decimal integers");
        values[i]=quint32(n);}
    if(!values[0]||!values[1])throw std::invalid_argument("grid rows and columns must be positive");
    return {values[0],values[1],values[2],values[3],values[4],values[5]};
}
inline QJsonObject gridFrame(Grid g,const QJsonObject &area,double top,double right,double bottom,double left,double gap){
    g.x=std::min(g.x,g.columns-1);g.y=std::min(g.y,g.rows-1);
    g.width=std::min(std::max(g.width,quint32(1)),g.columns-g.x);g.height=std::min(std::max(g.height,quint32(1)),g.rows-g.y);
    double x=area["x"].toDouble()+left,y=area["y"].toDouble()+top,w=area["w"].toDouble()-left-right,h=area["h"].toDouble()-top-bottom;
    // Yabai adjusts the usable bounds for the selected rectangle's inner edges
    // before dividing by row/column counts; it does not gap every grid cell.
    if(g.x){x+=gap;w-=gap;}if(g.y){y+=gap;h-=gap;}
    if(g.width<g.columns-g.x)w-=gap;
    if(g.height<g.rows-g.y)h-=gap;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0)throw std::invalid_argument("padding and gaps leave no usable grid area");
    return {{"x",x+w*double(g.x)/g.columns},{"y",y+h*double(g.y)/g.rows},{"w",w*double(g.width)/g.columns},{"h",h*double(g.height)/g.rows}};
}
}
