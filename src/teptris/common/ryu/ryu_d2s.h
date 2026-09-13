// Copyright 2018 Ulf Adams
//
// The contents of this file may be used under the terms of the Apache License,
// Version 2.0.
//
//    (See accompanying file LICENSE-Apache or copy at
//     http://www.apache.org/licenses/LICENSE-2.0)
//
// Alternatively, the contents of this file may be used under the terms of
// the Boost Software License, Version 1.0.
//
//    (See accompanying file LICENSE-Boost or copy at
//     https://www.boost.org/LICENSE_1_0.txt)
//
// Unless required by applicable law or agreed to in writing, this software
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.
//
// Ported into teptris from ulfjack/ryu (src/ryu): only the double-precision
// print path; malloc wrappers removed, entry point prefixed teptris_ryu_.
#ifndef TEPTRIS_RYU_D2S_H
#define TEPTRIS_RYU_D2S_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

/* Writes the shortest round-trip representation of f in scientific form
 * "D[.DDD]E[X]" (e.g. "1.234E5", "-5E-1") into result (>= 25 bytes) and
 * returns the number of characters written. */
int teptris_ryu_d2s_buffered_n(double f, char* result);

#ifdef __cplusplus
}
#endif

#endif /* TEPTRIS_RYU_D2S_H */
