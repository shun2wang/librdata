#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include "../rdata.h"

#include "test_buffer.h"
#include "test_buffer_io.h"

#ifdef _WIN32
#define timegm _mkgmtime
#endif

/* The test writes a workspace with two data frames, then reads it back and
 * checks everything that came out. Table 1 has four columns and custom row
 * names; table 2 has a factor and a non-ASCII string, with default row names.
 * The round trip runs once uncompressed and once gzip-compressed. */

#define TABLE1_ROWS 3
#define TABLE2_ROWS 2

typedef struct test_rdata_ctx_s {
    time_t timestamp;
    struct tm date;

    int table_index;        /* 0 = table1, 1 = table2, -1 = none yet */
    int column_index;       /* position of the current column within the table */
    int text_column_index;  /* position of the current string column within the table */
    int failures;
} test_rdata_ctx_t;

#define FAIL(ctx, ...) do { printf(__VA_ARGS__); printf("\n"); (ctx)->failures++; } while (0)

static void handle_error(const char *error_message, void *ctx) {
    printf("%s\n", error_message);
}

static ssize_t write_data(const void *bytes, size_t len, void *ctx) {
    rt_buffer_t *buffer = (rt_buffer_t *)ctx;
    buffer_grow(buffer, len);
    if (buffer->bytes == NULL) {
        return -1;
    }
    memcpy(buffer->bytes + buffer->used, bytes, len);
    buffer->used += len;
    return len;
}

static int handle_table(const char *name, void *ctx) {
    test_rdata_ctx_t *test_ctx = (test_rdata_ctx_t *)ctx;
    test_ctx->table_index++;
    test_ctx->column_index = 0;
    test_ctx->text_column_index = -1;
    if (name == NULL) {
        FAIL(test_ctx, "Table without a name");
    } else if (test_ctx->table_index == 0 && strcmp(name, "table1") != 0) {
        FAIL(test_ctx, "Unexpected first table name: %s", name);
    } else if (test_ctx->table_index == 1 && strcmp(name, "table2") != 0) {
        FAIL(test_ctx, "Unexpected second table name: %s", name);
    } else if (test_ctx->table_index > 1) {
        FAIL(test_ctx, "Unexpected extra table: %s", name);
    }
    return 0;
}

static int handle_column_name(const char *name, int index, void *ctx) {
    test_rdata_ctx_t *test_ctx = (test_rdata_ctx_t *)ctx;
    static const char *table1_names[] = { "column1", "column2", "column3", "column4" };
    static const char *table2_names[] = { "column5", "column6" };
    const char *expected = NULL;

    if (test_ctx->table_index == 0 && index < 4) {
        expected = table1_names[index];
    } else if (test_ctx->table_index == 1 && index < 2) {
        expected = table2_names[index];
    }

    if (expected == NULL) {
        FAIL(test_ctx, "Unexpected column name at index %d: %s", index, name);
    } else if (strcmp(name, expected) != 0) {
        FAIL(test_ctx, "Column name mismatch at index %d: got %s, expected %s", index, name, expected);
    }
    return 0;
}

static int handle_row_name(const char *name, int index, void *ctx) {
    test_rdata_ctx_t *test_ctx = (test_rdata_ctx_t *)ctx;
    static const char *table1_names[] = { "a", "b", "c" };
    static const char *table2_names[] = { "1", "2" };
    const char *expected = NULL;

    if (test_ctx->table_index == 0 && index < TABLE1_ROWS) {
        expected = table1_names[index];
    } else if (test_ctx->table_index == 1 && index < TABLE2_ROWS) {
        expected = table2_names[index];
    }

    if (expected == NULL) {
        FAIL(test_ctx, "Unexpected row name at index %d: %s", index, name);
    } else if (strcmp(name, expected) != 0) {
        FAIL(test_ctx, "Row name mismatch at index %d: got %s, expected %s", index, name, expected);
    }
    return 0;
}

static int handle_column(const char *name, rdata_type_t type,
                         void *data, long count, void *ctx) {
    test_rdata_ctx_t *test_ctx = (test_rdata_ctx_t *)ctx;
    int column_index = test_ctx->column_index++;
    int i;

    if (name != NULL) {
        FAIL(test_ctx, "Data frame column unexpectedly has a name: %s", name);
    }

    if (test_ctx->table_index == 0) {
        rdata_type_t expected_types[] = { RDATA_TYPE_REAL, RDATA_TYPE_STRING, RDATA_TYPE_TIMESTAMP, RDATA_TYPE_DATE };
        if (column_index >= 4) {
            FAIL(test_ctx, "Unexpected extra column in table1");
            return 0;
        }
        if (type != expected_types[column_index]) {
            FAIL(test_ctx, "Unexpected type for table1 column %d: %d", column_index, type);
            return 0;
        }
        if (count != TABLE1_ROWS) {
            FAIL(test_ctx, "Unexpected row count in table1: %ld", count);
            return 0;
        }
    } else if (test_ctx->table_index == 1) {
        rdata_type_t expected_types[] = { RDATA_TYPE_INT32, RDATA_TYPE_STRING };
        if (column_index >= 2) {
            FAIL(test_ctx, "Unexpected extra column in table2");
            return 0;
        }
        if (type != expected_types[column_index]) {
            FAIL(test_ctx, "Unexpected type for table2 column %d: %d", column_index, type);
            return 0;
        }
        if (count != TABLE2_ROWS) {
            FAIL(test_ctx, "Unexpected row count in table2: %ld", count);
            return 0;
        }
    } else {
        FAIL(test_ctx, "Column outside of any table");
        return 0;
    }

    if (type == RDATA_TYPE_STRING) {
        test_ctx->text_column_index = column_index;
        return 0;
    }

    if (data == NULL) {
        FAIL(test_ctx, "Missing data for column %d", column_index);
        return 0;
    }

    if (type == RDATA_TYPE_REAL) {
        double *dp = data;
        if (dp[0] != 0.0)
            FAIL(test_ctx, "Unexpected real value[0]: %lf", dp[0]);
        if (dp[1] != 100.0)
            FAIL(test_ctx, "Unexpected real value[1]: %lf", dp[1]);
        if (!isnan(dp[2]))
            FAIL(test_ctx, "Unexpected real value[2]: %lf", dp[2]);
    }
    if (type == RDATA_TYPE_TIMESTAMP) {
        double *dp = data;
        for (i=0; i<count; i++) {
            if (dp[i] != test_ctx->timestamp)
                FAIL(test_ctx, "Unexpected timestamp value[%d]: %lf", i, dp[i]);
        }
    }
    if (type == RDATA_TYPE_DATE) {
        double *dp = data;
        for (i=0; i<count; i++) {
            if (dp[i] * 86400 != timegm(&test_ctx->date))
                FAIL(test_ctx, "Unexpected date value[%d]: %lf", i, dp[i]);
        }
    }
    if (type == RDATA_TYPE_INT32) {
        int32_t *ip = data;
        for (i=0; i<count; i++) {
            if (ip[i] != i + 1)
                FAIL(test_ctx, "Unexpected integer value[%d]: %d", i, ip[i]);
        }
    }

    return 0;
}

static int handle_text_value(const char *value, int index, void *ctx) {
    test_rdata_ctx_t *test_ctx = (test_rdata_ctx_t *)ctx;
    static const char *table1_values[] = { "hello", "goodbye", NULL };
    static const char *table2_values[] = { "h\xc3\xa9llo", "" };
    const char *expected = NULL;
    int valid = 0;

    if (test_ctx->table_index == 0 && test_ctx->text_column_index == 1 && index < TABLE1_ROWS) {
        expected = table1_values[index];
        valid = 1;
    } else if (test_ctx->table_index == 1 && test_ctx->text_column_index == 1 && index < TABLE2_ROWS) {
        expected = table2_values[index];
        valid = 1;
    }

    if (!valid) {
        FAIL(test_ctx, "Unexpected text value at index %d: %s", index, value ? value : "(null)");
    } else if ((expected == NULL) != (value == NULL)) {
        FAIL(test_ctx, "Text value NULL-ness mismatch at index %d", index);
    } else if (expected && strcmp(value, expected) != 0) {
        FAIL(test_ctx, "Text value mismatch at index %d: got %s, expected %s", index, value, expected);
    }
    return 0;
}

static int handle_value_label(const char *value, int index, void *ctx) {
    test_rdata_ctx_t *test_ctx = (test_rdata_ctx_t *)ctx;
    static const char *levels[] = { "x", "y" };

    if (test_ctx->table_index != 1 || index >= 2) {
        FAIL(test_ctx, "Unexpected value label at index %d: %s", index, value);
    } else if (strcmp(value, levels[index]) != 0) {
        FAIL(test_ctx, "Value label mismatch at index %d: got %s, expected %s", index, value, levels[index]);
    }
    return 0;
}

static int check(rdata_error_t err, const char *what) {
    if (err != RDATA_OK) {
        printf("%s failed: %s\n", what, rdata_error_message(err));
        return 1;
    }
    return 0;
}

static int write_workspace(rt_buffer_t *buffer, test_rdata_ctx_t *ctx, rdata_compression_t compression) {
    int failures = 0;
    rdata_writer_t *writer = rdata_writer_init(&write_data, RDATA_WORKSPACE);

    failures += check(rdata_writer_set_compression(writer, compression), "set_compression");

    /* Table 1 */
    rdata_column_t *col1 = rdata_add_column(writer, "column1", RDATA_TYPE_REAL);
    rdata_column_t *col2 = rdata_add_column(writer, "column2", RDATA_TYPE_STRING);
    rdata_column_t *col3 = rdata_add_column(writer, "column3", RDATA_TYPE_TIMESTAMP);
    rdata_column_t *col4 = rdata_add_column(writer, "column4", RDATA_TYPE_DATE);

    failures += check(rdata_begin_file(writer, buffer), "begin_file");
    failures += check(rdata_begin_table(writer, "table1"), "begin_table 1");

    /* Writing columns out of order is an error */
    if (rdata_begin_column(writer, col2, TABLE1_ROWS) != RDATA_ERROR_COLUMN_MISMATCH) {
        printf("Out-of-order column was not rejected\n");
        failures++;
    }

    failures += check(rdata_begin_column(writer, col1, TABLE1_ROWS), "begin_column 1");
    rdata_append_real_value(writer, 0.0);
    rdata_append_real_value(writer, 100.0);
    rdata_append_real_value(writer, NAN);
    failures += check(rdata_end_column(writer, col1), "end_column 1");

    failures += check(rdata_begin_column(writer, col2, TABLE1_ROWS), "begin_column 2");
    rdata_append_string_value(writer, "hello");
    rdata_append_string_value(writer, "goodbye");
    rdata_append_string_value(writer, NULL);
    failures += check(rdata_end_column(writer, col2), "end_column 2");

    failures += check(rdata_begin_column(writer, col3, TABLE1_ROWS), "begin_column 3");
    rdata_append_timestamp_value(writer, ctx->timestamp);
    rdata_append_timestamp_value(writer, ctx->timestamp);
    rdata_append_timestamp_value(writer, ctx->timestamp);
    failures += check(rdata_end_column(writer, col3), "end_column 3");

    failures += check(rdata_begin_column(writer, col4, TABLE1_ROWS), "begin_column 4");
    rdata_append_date_value(writer, &ctx->date);
    rdata_append_date_value(writer, &ctx->date);
    rdata_append_date_value(writer, &ctx->date);
    failures += check(rdata_end_column(writer, col4), "end_column 4");

    rdata_append_row_name(writer, "a");
    rdata_append_row_name(writer, "b");

    /* Row name count must match the row count */
    if (rdata_end_table(writer, TABLE1_ROWS, "My data set") != RDATA_ERROR_ROW_NAME_COUNT) {
        printf("Short row name list was not rejected\n");
        failures++;
    }

    rdata_append_row_name(writer, "c");
    failures += check(rdata_end_table(writer, TABLE1_ROWS, "My data set"), "end_table 1");

    /* Table 2: columns added after end_table belong to the next table */
    rdata_column_t *col5 = rdata_add_column(writer, "column5", RDATA_TYPE_INT32);
    rdata_column_add_factor(col5, "x");
    rdata_column_add_factor(col5, "y");
    rdata_column_t *col6 = rdata_add_column(writer, "column6", RDATA_TYPE_STRING);

    failures += check(rdata_begin_table(writer, "table2"), "begin_table 2");

    failures += check(rdata_begin_column(writer, col5, TABLE2_ROWS), "begin_column 5");
    rdata_append_int32_value(writer, 1);
    rdata_append_int32_value(writer, 2);
    failures += check(rdata_end_column(writer, col5), "end_column 5");

    failures += check(rdata_begin_column(writer, col6, TABLE2_ROWS), "begin_column 6");
    rdata_append_string_value(writer, "h\xc3\xa9llo");
    rdata_append_string_value(writer, "");
    failures += check(rdata_end_column(writer, col6), "end_column 6");

    failures += check(rdata_end_table(writer, TABLE2_ROWS, "Second data set"), "end_table 2");
    failures += check(rdata_end_file(writer), "end_file");

    rdata_writer_free(writer);

    return failures;
}

static int read_workspace(rt_buffer_t *buffer, test_rdata_ctx_t *ctx) {
    rt_buffer_ctx_t *buffer_ctx = buffer_ctx_init(buffer);

    rdata_parser_t *parser = rdata_parser_init();
    rdata_set_open_handler(parser, rt_open_handler);
    rdata_set_close_handler(parser, rt_close_handler);
    rdata_set_seek_handler(parser, rt_seek_handler);
    rdata_set_read_handler(parser, rt_read_handler);
    rdata_set_update_handler(parser, rt_update_handler);
    rdata_set_io_ctx(parser, buffer_ctx);

    rdata_set_table_handler(parser, &handle_table);
    rdata_set_column_handler(parser, &handle_column);
    rdata_set_column_name_handler(parser, &handle_column_name);
    rdata_set_row_name_handler(parser, &handle_row_name);
    rdata_set_text_value_handler(parser, &handle_text_value);
    rdata_set_value_label_handler(parser, &handle_value_label);
    rdata_set_error_handler(parser, &handle_error);

    ctx->table_index = -1;
    ctx->column_index = 0;
    ctx->text_column_index = -1;

    rdata_error_t err = rdata_parse(parser, "example.RData", ctx);

    rdata_parser_free(parser);
    free(buffer_ctx);

    if (err != RDATA_OK) {
        printf("Parse failed: %s\n", rdata_error_message(err));
        return 1;
    }
    if (ctx->table_index != 1) {
        printf("Expected 2 tables, read %d\n", ctx->table_index + 1);
        return 1;
    }
    return ctx->failures;
}

static void dump_buffer(rt_buffer_t *buffer, const char *path) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd == -1)
        return;
    write(fd, buffer->bytes, buffer->used);
    close(fd);
    printf("Wrote test file out to %s\n", path);
}

/* Optional argument: a path prefix. The generated files are written to
 * <prefix>_none.RData and <prefix>_gzip.RData so they can be checked in R. */
int main(int argc, char *argv[]) {
    struct timeval time;
    gettimeofday(&time, NULL);

    int total_failures = 0;
    rdata_compression_t compressions[] = { RDATA_COMPRESSION_NONE, RDATA_COMPRESSION_GZIP };
    const char *compression_names[] = { "none", "gzip" };
    size_t i;

    for (i=0; i<sizeof(compressions)/sizeof(compressions[0]); i++) {
        test_rdata_ctx_t ctx = { .timestamp = time.tv_sec,
            .date = { .tm_year = 95, .tm_mon = 7, .tm_mday = 15 } };
        rt_buffer_t *buffer = buffer_init();
        int failures = 0;

        printf("Round trip with compression: %s\n", compression_names[i]);

        failures += write_workspace(buffer, &ctx, compressions[i]);
        if (failures == 0)
            failures += read_workspace(buffer, &ctx);

        if (failures != 0) {
            char path[256];
            snprintf(path, sizeof(path), "/tmp/rdata_test_%s.RData", compression_names[i]);
            dump_buffer(buffer, path);
        } else if (argc > 1) {
            char path[1024];
            snprintf(path, sizeof(path), "%s_%s.RData", argv[1], compression_names[i]);
            dump_buffer(buffer, path);
        }

        buffer_free(buffer);
        total_failures += failures;
    }

    return (total_failures != 0);
}
