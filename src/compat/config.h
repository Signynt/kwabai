#pragma once
#include "color.h"
namespace kwabai::compat {
inline bool isLayoutSetting(const QString &name) {return name=="float_popups"||name=="layout"||name=="top_padding"||name=="bottom_padding"||name=="left_padding"||name=="right_padding"||name=="window_gap";}
}
