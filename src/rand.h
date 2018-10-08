// Copyright 17 Wu Tao
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

namespace yaraft {

// setRandSeed uses the provided seed value to initialize the generator to a deterministic state.
extern void setRandSeed(int32_t seed);

// Intn returns, as an uint64_t, a non-negative pseudo-random number in [0,n).
extern uint64_t randIntn(uint64_t n);

// Float64 returns, as a double, a pseudo-random number in [0.0,1.0).
extern double randFloat64();

}  // namespace yaraft
