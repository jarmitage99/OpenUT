#pragma once

#include "core/object.h"

namespace ut {
    bool lineOfSightTo(UObject* self, UObject* other, bool showSelf, bool ignoreDistance);
}
