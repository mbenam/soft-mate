#pragma once
#include "M8Device.h"

namespace m8 {
namespace dev {

int runDaemon(M8Device& dev, int defaultHoldMs, int defaultGapMs, int defaultSettleMs);

} // namespace dev
} // namespace m8
