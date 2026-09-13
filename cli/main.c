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
            "  format          parse and re-emit canonical TOML\n"
            "  validate        exit 0 when valid, 1 with line:col otherwise\n"
            "  version         print the library version\n"
            "\n"
            "Reads stdin when file is '-' or omitted.\n",
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
    if (strcmp(cmd, "parse") != 0 && strcmp(cmd, "to-json") != 0 &&
        strcmp(cmd, "format") != 0 && strcmp(cmd, "validate") != 0) {
        usage(stderr, argv[0]);
        return 2;
    }

    const char *path = (argc > 2) ? argv[2] : "-";
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
        if (teptris_document_emit_json(doc, &buf, &blen) == TEPTRIS_OK) {
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
