#pragma once
#include <QStringList>
#include <cmath>
#include <stdexcept>
namespace kwabai::compat {
struct EdgeResize {QString handle;double dx,dy;};
inline EdgeResize parseEdgeResize(const QString &text) {
    const QStringList handles{"top","left","bottom","right","top_left","top_right","bottom_right","bottom_left"};
    auto parts=text.split(':');
    if(parts.size()!=3||!handles.contains(parts[0]))throw std::invalid_argument("expected EDGE:DX:DY or CORNER:DX:DY; absolute and floating resize are not implemented");
    bool x=false,y=false;double dx=parts[1].toDouble(&x),dy=parts[2].toDouble(&y);
    if(!x||!y||!std::isfinite(dx)||!std::isfinite(dy))throw std::invalid_argument("resize deltas must be finite numbers");
    return {parts[0],dx,dy};
}
}
