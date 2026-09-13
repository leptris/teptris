// Copyright 2019 Ulf Adams
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
// Ported into teptris from ulfjack/ryu (src/ryu): the double-precision
// parse path (experimental upstream); wrappers removed, entry point
// prefixed teptris_ryu_.
#ifndef TEPTRIS_RYU_PARSE_H
#define TEPTRIS_RYU_PARSE_H

#ifdef __cplusplus
extern "C" {
#endif

enum teptris_ryu_status {
    TEPTRIS_RYU_SUCCESS,
    TEPTRIS_RYU_INPUT_TOO_SHORT,
    TEPTRIS_RYU_INPUT_TOO_LONG,
    TEPTRIS_RYU_MALFORMED_INPUT
};

/* Parses buffer[0..len) as [-]digits[.digits][eE[+-]digits] into result.
 * No leading '+' (upstream contract; callers strip it). */
enum teptris_ryu_status teptris_ryu_s2d_n(const char *buffer, const int len,
                                          double *result);

#ifdef __cplusplus
}
#endif

#endif /* TEPTRIS_RYU_PARSE_H */
