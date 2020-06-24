#pragma once

#include <libsystem/Common.h>
#include <libsystem/unicode/Codepoint.h>
#include <libsystem/utils/Slice.h>

typedef __big_endian struct
{
    uint8_t tag[4];
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} TrueTypeTable;

typedef __big_endian struct
{
    uint32_t version;

    uint16_t table_count;
    uint16_t search_range;
    uint16_t entry_selector;
    uint16_t range_shift;

    TrueTypeTable tables[];
} TrueTypeHeader;

Slice truetype_get_table(Slice fontdata, const char *tag);

uint32_t truetype_get_glyph_index(Slice fontdata, Codepoint codepoint);
