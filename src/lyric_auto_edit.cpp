#include "stdafx.h"

#include "parsers.h"
#include "logging.h"
#include "lyric_auto_edit.h"
#include "lyric_io.h"

LyricUpdateHandle auto_edit::CreateInstrumental(metadb_handle_ptr track)
{
    LyricData lyrics;
    lyrics.lines.push_back({_T("[Instrumental]"), DBL_MAX});
    lyrics.text = "[Instrumental]";

    LyricUpdateHandle result(LyricUpdateHandle::Type::Edit, track, fb2k::noAbort);
    result.set_started();
    result.set_result(std::move(lyrics), true);
    return result;
}

LyricUpdateHandle auto_edit::RemoveRepeatedSpaces(metadb_handle_ptr track, const LyricData& lyrics)
{
    size_t spaces_erased = 0;
    LyricData new_lyrics = lyrics;
    for(LyricDataLine& line : new_lyrics.lines)
    {
        size_t search_start = 0;
        while(search_start < line.text.length())
        {
            size_t next_space = line.text.find_first_of(_T(' '), search_start);

            // NOTE: If the line was empty we would not enter this loop.
            //       We subtract 1 from the length to avoid overflowing when next_space == npos == (size_t)-1
            assert(line.text.length() > 0);
            if(next_space > line.text.length()-1)
            {
                break;
            }

            size_t erase_start = next_space + 1;
            size_t erase_end = line.text.find_first_not_of(_T(' '), erase_start);

            if((erase_end != std::tstring::npos) && (erase_end > erase_start))
            {
                size_t erase_count = erase_end - erase_start;
                line.text.erase(erase_start, erase_count);
                spaces_erased += erase_count;
            }
            search_start = next_space + 1;
        }
    }
    LOG_INFO("Auto-removal removed %u unnecessary spaces", spaces_erased);

    LyricUpdateHandle result(LyricUpdateHandle::Type::Edit, track, fb2k::noAbort);
    result.set_started();
    if(spaces_erased > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        result.set_result(std::move(new_lyrics), true);
    }
    else
    {
        result.set_result({}, true);
    }
    return result;
}

LyricUpdateHandle auto_edit::RemoveRepeatedBlankLines(metadb_handle_ptr track, const LyricData& lyrics)
{
    size_t lines_removed = 0;
    bool previous_blank = true;
    LyricData new_lyrics = lyrics;
    for(auto iter=new_lyrics.lines.begin(); iter != new_lyrics.lines.end(); /*Omitted*/)
    {
        size_t first_non_space = iter->text.find_first_not_of(' ');
        bool is_blank = (first_non_space == std::tstring::npos);
        if(is_blank && previous_blank)
        {
            iter = new_lyrics.lines.erase(iter);
            lines_removed++;
        }
        else
        {
            iter++;
        }
        previous_blank = is_blank;
    }
    LOG_INFO("Auto-removal removed %u blank lines", lines_removed);

    LyricUpdateHandle result(LyricUpdateHandle::Type::Edit, track, fb2k::noAbort);
    result.set_started();
    if(lines_removed > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        result.set_result(std::move(new_lyrics), true);
    }
    else
    {
        result.set_result({}, true);
    }
    return result;
}

LyricUpdateHandle auto_edit::RemoveAllBlankLines(metadb_handle_ptr track, const LyricData& lyrics)
{
    LyricData new_lyrics = lyrics;
    auto line_is_empty = [](const LyricDataLine& line) { return line.text.find_first_not_of(' ') == std::tstring::npos; };
    auto new_end = std::remove_if(new_lyrics.lines.begin(), new_lyrics.lines.end(), line_is_empty);
    int lines_removed = std::distance(new_end, new_lyrics.lines.end());
    new_lyrics.lines.erase(new_end, new_lyrics.lines.end());
    LOG_INFO("Auto-removal removed %d blank lines", lines_removed);

    LyricUpdateHandle result(LyricUpdateHandle::Type::Edit, track, fb2k::noAbort);
    result.set_started();
    if(lines_removed > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        result.set_result(std::move(new_lyrics), true);
    }
    else
    {
        result.set_result({}, true);
    }
    return result;
}