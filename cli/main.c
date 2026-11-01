#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/teptris.h"

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "usage: %s <command> [file]\n"
            "\n"
            "commands:\n"
            "  parse|to-json   parse and dump the typed-JSON tree\n"
            "    --natural     dump natural JSON instead (real numbers,\n"
            "                  booleans, RFC 3339 datetime strings;\n"
            "                  non-finite floats become null)\n"
            "  format          parse and re-emit canonical TOML\n"
            "  validate        exit 0 when valid, 1 with line:col otherwise\n"
            "  set <file> <key.path> <value>\n"
            "                  format-preserving in-place edit: rewrite\n"
            "                  exactly that value's bytes (single-line\n"
            "                  scalars; comments and formatting kept)\n"
            "  version         print the library version\n"
            "\n"
            "Reads stdin when file is '-' or omitted (not set).\n",
            prog);
}

static char *read_all(const char *path, size_t *len)
{
    FILE *f;
    if (path == NULL || strcmp(path, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(path, "rb");
        if (f == NULL) {
            return NULL;
        }
    }
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        if (f != stdin) {
            fclose(f);
        }
        return NULL;
    }
    for (;;) {
        if (n == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                if (f != stdin) {
                    fclose(f);
                }
                return NULL;
            }
            buf = nb;
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (r == 0) {
            break;
        }
    }
    if (f != stdin) {
        fclose(f);
    }
    *len = n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr, argv[0]);
        return 2;
    }
    const char *cmd = argv[1];

    if (strcmp(cmd, "version") == 0) {
        printf("%s\n", teptris_version_string());
        return 0;
    }
    if (strcmp(cmd, "set") == 0) {
        if (argc != 5) {
            fprintf(stderr,
                    "usage: %s set <file> <key.path> <value>\n", argv[0]);
            return 2;
        }
        size_t slen = 0;
        char *src = read_all(argv[2], &slen);
        if (src == NULL) {
            fprintf(stderr, "teptris: cannot read %s\n", argv[2]);
            return 2;
        }
        char *out = NULL;
        size_t olen = 0;
        teptris_status st = teptris_edit_scalar(src, slen, argv[3],
                                                argv[4], &out, &olen);
        free(src);
        if (st == TEPTRIS_ERR_STATE) {
            fprintf(stderr, "teptris: key not found: %s\n", argv[3]);
            return 1;
        }
        if (st == TEPTRIS_ERR_ARG) {
            fprintf(stderr,
                    "teptris: cannot edit %s (the replacement must be "
                    "one valid TOML value; malformed spans reject with "
                    "the document untouched)\n",
                    argv[3]);
            return 2;
        }
        if (st != TEPTRIS_OK) {
            fprintf(stderr, "teptris: edit failed (status %d)\n", (int)st);
            return 2;
        }
        FILE *f = fopen(argv[2], "wb");
        if (f == NULL) {
            fprintf(stderr, "teptris: cannot write %s\n", argv[2]);
            free(out);
            return 2;
        }
        fwrite(out, 1, olen, f);
        fclose(f);
        free(out);
        return 0;
    }
    if (strcmp(cmd, "parse") != 0 && strcmp(cmd, "to-json") != 0 &&
        strcmp(cmd, "format") != 0 && strcmp(cmd, "validate") != 0) {
        usage(stderr, argv[0]);
        return 2;
    }

    int natural = 0;
    const char *path = "-";
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--natural") == 0) {
            natural = 1;
        } else {
            path = argv[i];
        }
    }
    if (natural && strcmp(cmd, "to-json") != 0) {
        fprintf(stderr, "teptris: --natural applies to to-json only\n");
        return 2;
    }
    size_t len = 0;
    char *data = read_all(path, &len);
    if (data == NULL) {
        fprintf(stderr, "teptris: cannot read %s\n", path);
        return 2;
    }

    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(data, len, NULL, &doc);
    if (st != TEPTRIS_OK) {
        const teptris_error *e = teptris_document_error(doc);
        fprintf(stderr, "%s:%zu:%zu: error: %s\n",
                (strcmp(path, "-") == 0) ? "<stdin>" : path, e->line, e->column,
                e->message);
        teptris_document_free(doc);
        free(data);
        return 1;
    }

    int rc = 0;
    if (strcmp(cmd, "parse") == 0 || strcmp(cmd, "to-json") == 0) {
        char *buf = NULL;
        size_t blen = 0;
        teptris_status est = natural
            ? teptris_document_emit_json_natural(doc, &buf, &blen)
            : teptris_document_emit_json(doc, &buf, &blen);
        if (est == TEPTRIS_OK) {
            fwrite(buf, 1, blen, stdout);
            putchar('\n');
            free(buf);
        } else {
            rc = 1;
        }
    } else if (strcmp(cmd, "format") == 0) {
        char *buf = NULL;
        size_t blen = 0;
        if (teptris_document_emit(doc, &buf, &blen) == TEPTRIS_OK) {
            fwrite(buf, 1, blen, stdout);
            free(buf);
        } else {
            rc = 1;
        }
    }
    /* validate: silent success */

    teptris_document_free(doc);
    free(data);
    return rc;
}
