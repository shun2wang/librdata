# librdata - Read and write R data frames from C
[![Build Status](https://github.com/WizardMac/librdata/workflows/build/badge.svg)](https://github.com/WizardMac/librdata/actions)
[![Build status](https://ci.appveyor.com/api/projects/status/xrao0cdroh5xn950?svg=true)](https://ci.appveyor.com/project/evanmiller/librdata)

Originally part of [ReadStat](https://github.com/WizardMac/ReadStat), librdata
is a small C library for reading and writing R data frames.

Features:

* Read both RData and RDS formats
* Read compressed files (requires bzip2, zlib, and lzma)
* Write factors, timestamps, logical vectors, and more

## Installation

```
./autogen.sh
./configure
make
make install
```

If you're on Mac and see errors about `AM_ICONV` when you run `./autogen.sh`,
you'll need to install [gettext](https://www.gnu.org/software/gettext/).

### Building on Windows

Use [MSYS2](https://www.msys2.org/) with the MinGW-w64 toolchain (this is how
the Windows CI build works). From a MINGW64 shell:

```
pacman -S autoconf automake libtool gettext mingw-w64-x86_64-toolchain
./autogen.sh
./configure
make
```

The GnuWin32 autoconf packages are not sufficient, since they lack the Perl
modules that `autoreconf` needs.

## Language bindings

* Python: [pyreadr](https://github.com/ofajardo/pyreadr)

## Read API

Example usage:

```c
#include "rdata.h"

static int handle_table(const char *name, void *ctx) {
    printf("Read table: %s\n", name);

    return 0; /* non-zero to abort processing */
}

// Called once for all columns with the following caveats:
// * `name` is NULL for some columns (see handle_column_name below)
// * `data` is NULL for text columns (see handle_text_value below)
static int handle_column(const char *name, rdata_type_t type,
                         void *data, long count, void *ctx) {
    /* Do something... */
    return 0;
}

// Some column names appear in the file after the data
static int handle_column_name(const char *name, int index, void *ctx) {
    if (debug) printf("Read column name: %s\n", name);
    /* Do something... */
    return 0;
}

// Called once per row for a text column
static int handle_text_value(const char *value, int index, void *ctx) {
    /* Do something... */
    return 0;
}

// Called for factor variables, once for each level
static int handle_value_label(const char *value, int index, void *ctx) {
    /* Do something... */
    return 0;
}

rdata_parser_t *parser = rdata_parser_init();

rdata_set_table_handler(parser, &handle_table);
rdata_set_column_handler(parser, &handle_column);
rdata_set_text_value_handler(parser, &handle_text_value);
rdata_set_value_label_handler(parser, &handle_value_label);

rdata_parse(parser, "/path/to/something.rdata", NULL);
```

See [`rdata.h`](src/rdata.h) for the full API.

### Text encoding

All strings are delivered to the handlers as UTF-8. Strings that R has marked
as UTF-8, ASCII, or Latin-1 are converted (or passed through) automatically.
Strings in R's "native" encoding are converted from the encoding declared in
the file, which only version 3 files (R >= 3.5) record. If the file declares
the wrong encoding, or is an older file without a declaration, you can supply
the encoding of native strings yourself:

```c
rdata_set_file_character_encoding(parser, "WINDOWS-1252");
```

Pass `"UTF-8"` to hand native strings through untouched.

### Unsupported objects

Objects that librdata doesn't understand (lists, S4 objects, functions,
environments, and so on) are skipped. `.Random.seed` is skipped as well.

## Write API

Example usage:

```c
#include "rdata.h"

static ssize_t write_data(const void *bytes, size_t len, void *ctx) {
    int fd = *(int *)ctx;
    return write(fd, bytes, len);
}

int row_count = 3;
int fd = open("/path/to/somewhere.rdata", O_CREAT | O_WRONLY, 0644);
rdata_writer_t *writer = rdata_writer_init(&write_data, RDATA_WORKSPACE);

rdata_column_t *col1 = rdata_add_column(writer, "column1", RDATA_TYPE_REAL);
rdata_column_t *col2 = rdata_add_column(writer, "column2", RDATA_TYPE_STRING);

rdata_begin_file(writer, &fd);
rdata_begin_table(writer, "my_table");

rdata_begin_column(writer, col1, row_count);
rdata_append_real_value(writer, 0.0);
rdata_append_real_value(writer, 100.0);
rdata_append_real_value(writer, NAN);
rdata_end_column(writer, col1);

rdata_begin_column(writer, col2, row_count);
rdata_append_string_value(writer, "hello");
rdata_append_string_value(writer, "goodbye");
rdata_append_string_value(writer, NULL);
rdata_end_column(writer, col2);

rdata_end_table(writer, row_count, "My data set");
rdata_end_file(writer);

close(fd);

```

See [`rdata.h`](src/rdata.h) for the full API.

### Multiple tables

Columns added after `rdata_end_table` belong to the next table. To write two
data frames to one RData file, add the columns of the first table, write it,
then add the columns of the second table and write that:

```c
rdata_column_t *col1 = rdata_add_column(writer, "column1", RDATA_TYPE_REAL);
rdata_begin_table(writer, "first");
/* ... write col1 ... */
rdata_end_table(writer, row_count, "First data set");

rdata_column_t *col2 = rdata_add_column(writer, "column2", RDATA_TYPE_STRING);
rdata_begin_table(writer, "second");
/* ... write col2 ... */
rdata_end_table(writer, row_count, "Second data set");
```

Within a table, columns must be written in the order they were added.

### Row names

By default rows are named "1", "2", and so on. To use your own row names,
call `rdata_append_row_name` once per row at any point before
`rdata_end_table`:

```c
rdata_append_row_name(writer, "first row");
rdata_append_row_name(writer, "second row");
rdata_append_row_name(writer, "third row");
rdata_end_table(writer, 3, "My data set");
```

### Compression

The writer produces uncompressed output by default. To write a gzip-compressed
file, as R's `save()` does, call this before `rdata_begin_file` (requires
zlib):

```c
rdata_writer_set_compression(writer, RDATA_COMPRESSION_GZIP);
```
