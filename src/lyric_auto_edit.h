#pragma once

#include "lyric_io.h"

namespace auto_edit
{
    LyricUpdateHandle CreateInstrumental(metadb_handle_ptr track);
    LyricUpdateHandle RemoveRepeatedSpaces(metadb_handle_ptr track, const LyricData& lyrics);
    LyricUpdateHandle RemoveRepeatedBlankLines(metadb_handle_ptr track, const LyricData& lyrics);
    LyricUpdateHandle RemoveAllBlankLines(metadb_handle_ptr track, const LyricData& lyrics);
}