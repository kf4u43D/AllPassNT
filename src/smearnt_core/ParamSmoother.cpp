#include "ParamSmoother.h"

constexpr float ParamSmoother::kSnapThreshold;

// The implementation stays inline; this translation unit owns the C++11
// definition of the non-integral constexpr member.
