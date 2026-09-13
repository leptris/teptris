#ifndef TEPTRIS_EMITTER_H
#define TEPTRIS_EMITTER_H

#include "teptris/teptris.h"

teptris_status teptris_emit_document(const teptris_document *doc, char **buf,
                                     size_t *len);
teptris_status teptris_emit_document_json(const teptris_document *doc,
                                          char **buf, size_t *len);

#endif /* TEPTRIS_EMITTER_H */
