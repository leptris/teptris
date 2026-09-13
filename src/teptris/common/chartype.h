#ifndef TEPTRIS_CHARTYPE_H
#define TEPTRIS_CHARTYPE_H

#include <stdbool.h>
#include <stdint.h>

/* Declared-once truth tables (MECE: the parser never calls is* directly). */
#define TEP_CT_BAREKEY 0x01u /* [A-Za-z0-9_-] */
#define TEP_CT_WS 0x02u      /* space, tab */
#define TEP_CT_DEC 0x04u     /* [0-9] */
#define TEP_CT_HEX 0x08u     /* [0-9a-fA-F] */
#define TEP_CT_ALPHA 0x10u   /* [A-Za-z] */

extern const uint8_t tep_chartype_table[256];

static inline bool tep_ct_has(unsigned char c, uint8_t flags)
{
    return (tep_chartype_table[c] & flags) != 0;
}

static inline bool tep_is_barekey(unsigned char c)
{
    return tep_ct_has(c, TEP_CT_BAREKEY);
}

static inline bool tep_is_ws(unsigned char c)
{
    return tep_ct_has(c, TEP_CT_WS);
}

static inline bool tep_is_dec(unsigned char c)
{
    return tep_ct_has(c, TEP_CT_DEC);
}

static inline bool tep_is_hex(unsigned char c)
{
    return tep_ct_has(c, TEP_CT_HEX);
}

#endif /* TEPTRIS_CHARTYPE_H */
