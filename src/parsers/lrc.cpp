#include "stdafx.h"

#include "logging.h"
#include "lyric_data.h"
#include "openlyrics_algorithms.h"
#include "mvtf/mvtf.h"
#include "parsers.h"
#include "tag_util.h"
#include "win32_util.h"

static bool equals_ignore_case(std::string_view lhs, std::string_view rhs)
{
    const pfc::string_part_ref pfc_lhs = pfc::string_part(lhs.data(), lhs.length());
    const pfc::string_part_ref pfc_rhs = pfc::string_part(rhs.data(), rhs.length());
    const int comparison = pfc::stringCompareCaseInsensitiveEx(pfc_lhs, pfc_rhs);
    return comparison == 0;
}

template<typename TResult>
static std::optional<TResult> strtoint(std::string_view str)
{
    TResult output = 0;
    std::from_chars_result result = std::from_chars(str.data(), str.data() + str.length(), output);
    if(result.ec == std::errc{})
    {
        return {output};
    }
    return {};
}

namespace parsers::lrc
{

bool is_tag_line(std::string_view line)
{
    if(line.size() <= 0) return false;
    if(line[0] != '[') return false;
    if(line[line.size()-1] != ']') return false;

    size_t colon_index = line.find(':');
    if(colon_index == std::string::npos) return false;
    assert(colon_index != 0); // We've already checked that the first char is '['

    size_t tag_length = colon_index - 1; // Tag lines have the form [tag:value] so we -1 to count from the 't' to the ':'
    if(tag_length == 0) return false;

    std::string_view tag = line.substr(1, colon_index-1);

    if(   !equals_ignore_case(tag, "ar") // Artist
       && !equals_ignore_case(tag, "al") // Album
       && !equals_ignore_case(tag, "ti") // Title
       && !equals_ignore_case(tag, "by") // Lyric 'author' (person who made the lrc)
       && !equals_ignore_case(tag, "id") // LRC file ID
       && !equals_ignore_case(tag, "offset") // The offset to add to the given line timestamps
       && !equals_ignore_case(tag, "length") // Track length (e.g ''03:40')
       && !equals_ignore_case(tag, "t_time") // Track length (e.g '(2:57)')
       && !equals_ignore_case(tag, "encoding") // Lyrics encoding (e.g 'utf-8' or 'iso-8859-15')
       )
    {
        return false;
    }

    return true;
}

static std::optional<double> try_parse_offset_tag(std::string_view line)
{
    if(line.size() <= 0) return {};
    if(line[0] != '[') return {};
    if(line[line.size()-1] != ']') return {};

    size_t colon_index = line.find(':');
    if(colon_index == std::string::npos) return {};
    assert(colon_index != 0); // We've already checked that the first char is '['

    std::string_view tag_key(line.data() + 1, colon_index - 1); // +-1 to avoid the leading '['

    const size_t val_start = colon_index + 1;
    const size_t val_end = line.length() - 1;
    const size_t val_length = val_end - val_start;
    const std::string_view tag_val = trim_surrounding_whitespace(line.substr(val_start, val_length));

    if(tag_key != "offset") return {};
    std::optional<int64_t> maybe_offset = strtoint<int64_t>(tag_val);
    if(maybe_offset.has_value())
    {
        int64_t offset_ms = maybe_offset.value();
        double offset_sec = double(offset_ms)/1000.0;
        return offset_sec;
    }
    else
    {
        return {};
    }
}

void set_offset_tag(LyricData& lyrics, double offset_seconds)
{
    std::string new_tag = "[offset:" + std::to_string(static_cast<int>(offset_seconds*1000.0)) + "]";
    for(auto iter=lyrics.tags.begin(); iter!=lyrics.tags.end(); iter++)
    {
        if(try_parse_offset_tag(*iter).has_value())
        {
            *iter = std::move(new_tag);
            return;
        }
    }

    lyrics.tags.push_back(std::move(new_tag));
}

void remove_offset_tag(LyricData& lyrics)
{
    for(auto iter=lyrics.tags.begin(); iter!=lyrics.tags.end(); /*omitted*/)
    {
        if(try_parse_offset_tag(*iter).has_value())
        {
            iter = lyrics.tags.erase(iter);
        }
        else
        {
            iter++;
        }
    }
}

double get_line_first_timestamp(std::string_view line)
{
    double timestamp = DBL_MAX;
    size_t close_index = line.find(']');
    if((close_index != std::string_view::npos) && try_parse_timestamp(line.substr(0, close_index+1), timestamp))
    {
        return timestamp;
    }
    return timestamp;
}

struct ParsedLineContents
{
    std::vector<double> timestamps;
    std::string line;
};

struct LineTimeParseResult
{
    bool success;
    double timestamp;
    size_t charsConsumed;
};

std::string print_timestamp(double timestamp)
{
    assert(timestamp != DBL_MAX);
    double total_seconds_flt = std::floor(timestamp);
    int total_seconds = static_cast<int>(total_seconds_flt);
    int time_hours = total_seconds/3600;
    int time_minutes = (total_seconds - 3600*time_hours)/60;
    int time_seconds = total_seconds - (time_hours*3600) - (time_minutes*60);
    int time_centisec = static_cast<int>(std::round((timestamp - total_seconds_flt) * 100.0));

    // NOTE: We need this special case here because the `std::round` call above might round us up
    //       and give us 100 centiseconds, which visually should actually be just "the next second".
    //       In theory we could just replace the `round` with a `floor` and be done but that would
    //       cause timestamp parsing and printing to not roundtrip cleanly (at least not with the
    //       rest of this implementation as it is today).
    if(time_centisec == 100)
    {
        time_centisec = 0;
        time_seconds++;
        if(time_seconds == 60)
        {
            time_seconds = 0;
            time_minutes++;
            if(time_minutes == 60)
            {
                time_minutes = 0;
                time_hours++;
            }
        }
    }

    char temp[32];
    if(time_hours == 0)
    {
        snprintf(temp, sizeof(temp), "[%02d:%02d.%02d]", time_minutes, time_seconds, time_centisec);
    }
    else
    {
        snprintf(temp, sizeof(temp), "[%02d:%02d:%02d.%02d]", time_hours, time_minutes, time_seconds, time_centisec);
    }
    return std::string(temp);
}

bool try_parse_timestamp(std::string_view tag, double& out_timestamp)
{
    if((tag[0] != '[') ||
       (tag[tag.length()-1] != ']'))
    {
        // We require that the tag is the entire string
        return false;
    }

    size_t second_separator = tag.rfind('.');
    size_t minsec_separator = tag.rfind(':');
    size_t hourmin_separator = tag.rfind(':', minsec_separator-1);
    if((second_separator == std::string_view::npos) || (minsec_separator == std::string_view::npos))
    {
        // We don't have the required separators for seconds-milliseconds and minutes-seconds
        return false;
    }

    std::string_view subsec_str = tag.substr(second_separator + 1, tag.length() - second_separator - 2);
    std::string_view sec_str = tag.substr(minsec_separator + 1, second_separator - minsec_separator - 1);
    std::string_view min_str = {};
    std::string_view hour_str = {};
    if(hourmin_separator == std::string_view::npos)
    {
        min_str = tag.substr(1, minsec_separator - 1);
    }
    else
    {
        min_str = tag.substr(hourmin_separator + 1, minsec_separator - hourmin_separator - 1);
        hour_str = tag.substr(1, hourmin_separator - 1);
    }

    std::optional<uint64_t> maybe_subsec = strtoint<uint64_t>(subsec_str);
    std::optional<uint64_t> maybe_sec = strtoint<uint64_t>(sec_str);
    std::optional<uint64_t> maybe_min = strtoint<uint64_t>(min_str);
    std::optional<uint64_t> maybe_hour = strtoint<uint64_t>(hour_str);

    if(!maybe_subsec.has_value() ||
       !maybe_sec.has_value() ||
       !maybe_min.has_value())
    {
        // The substrings that should contain positive integers, don't.
        return false;
    }

    double subsec_coefficient = 1.0;
    for(size_t i=0; i<subsec_str.length(); i++)
    {
        subsec_coefficient *= 0.1;
    }

    double timestamp = 0;
    timestamp += double(maybe_subsec.value()) * subsec_coefficient;
    timestamp += double(maybe_sec.value());
    timestamp += double(maybe_min.value())*60.0;
    if(maybe_hour.has_value())
    {
        timestamp += double(maybe_hour.value())*3600.0;
    }
    out_timestamp = timestamp;
    return true;
}

static LineTimeParseResult parse_time_from_line(std::string_view line)
{
    size_t line_length = line.length();
    size_t index = 0;
    while(index < line_length)
    {
        if(line[index] != '[') break;

        size_t close_index = std::min(line_length, line.find(']', index));
        size_t tag_length = close_index - index + 1;
        std::string_view tag = line.substr(index, tag_length);

        double timestamp = -1.0;
        if(try_parse_timestamp(tag, timestamp))
        {
            return {true, timestamp, index + tag_length};
        }
        else
        {
            // If we find something that is not a well-formed timestamp then stop and just 
            break;
        }
    }

    // NOTE: It is important that we return `index` here so that we move forwards correctly
    //       in the calling parser function. In particular when it fails we need to extract
    //       the non-tag string correctly and without `index` here we'll include the last
    //       tag (if any) in that string (which is wrong, since we want to ignore metadata
    //       tags such as title and artist).
    return {false, 0.0, index};
}

static ParsedLineContents parse_line_times(std::string_view line)
{
    std::vector<double> result;
    size_t index = 0;
    while(index <= line.size())
    {
        LineTimeParseResult parse_result = parse_time_from_line(line.substr(index));
        index += parse_result.charsConsumed;

        if(parse_result.success)
        {
            result.push_back(parse_result.timestamp);
        }
        else
        {
            break;
        }
    }

    return {result, std::string(line.substr(index).data(), line.size()-index)};
}

std::vector<LyricDataLine> collapse_concurrent_lines(const std::vector<LyricDataLine>& input)
{
    return alg::collapse(input, [](const LyricDataLine& lhs, const LyricDataLine& rhs)
    {
        if((lhs.timestamp == DBL_MAX) || (lhs.timestamp != rhs.timestamp))
        {
            return std::pair{lhs, std::optional{rhs}};
        }

        LyricDataLine combined = {lhs.text + _T('\n') + rhs.text, lhs.timestamp};
        return std::pair{combined, std::optional<LyricDataLine>{}};
    });
}

LyricData parse(const LyricDataCommon& metadata, std::string_view text) // `text` is assumed to be utf-8
{
    LOG_INFO("Parsing LRC lyric text...");

    std::vector<LyricDataLine> lines;
    std::vector<std::string> tags;
    bool tag_section_passed = false; // We only want to count lines as "tags" if they appear at the top of the file
    double timestamp_offset = 0.0;

    size_t line_start_index = 0;
    while (line_start_index < text.length())
    {
        size_t line_end_index = line_start_index;
        while ((line_end_index < text.length()) &&
            (text[line_end_index] != '\0') &&
            (text[line_end_index] != '\n') &&
            (text[line_end_index] != '\r'))
        {
            line_end_index++;
        }
        size_t line_bytes = line_end_index - line_start_index;

        if(line_bytes >= 3)
        {
            // NOTE: We're consuming UTF-8 text here and sometimes files contain byte-order marks.
            //       We don't want to process them so just skip past them. Ordinarily we'd do this
            //       just once at the start of the file but I've seen files with BOMs at the start
            //       of random lines in the file, so just check every line.
            if((text[line_start_index] == '\u00EF') &&
               (text[line_start_index+1] == '\u00BB') &&
               (text[line_start_index+2] == '\u00BF'))
            {
                line_start_index += 3;
                line_bytes -= 3;
            }
        }

        const std::string_view line_view {text.data() + line_start_index, line_bytes};
        ParsedLineContents parse_output = parse_line_times(line_view);
        if(parse_output.timestamps.size() > 0)
        {
            tag_section_passed = true;
            for(double timestamp : parse_output.timestamps)
            {
                lines.push_back({to_tstring(parse_output.line), timestamp});
            }
        }
        else
        {
            // We don't have a timestamp, but rather than failing to parse the entire file,
            // we just keep the line around as "not having a timestamp". We represent this
            // as a line with a timestamp that is way out of the actual length of the track.
            // That way the line will never be highlighted and it neatly slots into the rest
            // of the system without special handling.
            // NOTE: It is important however, to note that this means we need to stable_sort
            //       below, to preserve the ordering of the "untimed" lines
            if(!tag_section_passed && is_tag_line(line_view))
            {
                tags.emplace_back(line_view);

                std::optional<double> maybe_offset = try_parse_offset_tag(line_view);
                if(maybe_offset.has_value())
                {
                    timestamp_offset = maybe_offset.value();
                    LOG_INFO("Found LRC offset: %dms", int(timestamp_offset*1000.0));
                }
            }
            else
            {
                tag_section_passed |= (line_bytes > 0);
                lines.push_back({to_tstring(line_view), DBL_MAX});
            }
        }

        if ((line_end_index + 1 < text.length()) &&
            (text[line_end_index] == '\r') &&
            (text[line_end_index + 1] == '\n'))
        {
            line_start_index = line_end_index + 2;
        }
        else
        {
            line_start_index = line_end_index + 1;
        }
    }

    std::stable_sort(lines.begin(), lines.end(), [](const LyricDataLine& a, const LyricDataLine& b)
    {
        return a.timestamp < b.timestamp;
    });
    lines = collapse_concurrent_lines(lines);

    LyricData result(metadata);
    result.tags = std::move(tags);
    result.lines = std::move(lines);
    result.timestamp_offset = timestamp_offset;
    return result;
}

std::tstring expand_text(const LyricData& data, bool merge_equivalent_lrc_lines)
{
    LOG_INFO("Expanding lyric text...");
    std::tstring expanded_text;
    expanded_text.reserve(data.tags.size() * 64); // NOTE: 64 is an arbitrary "probably longer than most lines" value
    for(const std::string& tag : data.tags)
    {
        expanded_text += to_tstring(tag);
        expanded_text += _T("\r\n");
    }
    if(!expanded_text.empty())
    {
        expanded_text += _T("\r\n");
    }
    // NOTE: We specifically do *not* generate a new tag for the offset because all changes to that
    //       must happen *in the text* (which is the default because you can change it in the editor)

    if(data.IsTimestamped())
    {
        // Split lines with the same timestamp
        std::vector<LyricDataLine> out_lines;
        out_lines.reserve(data.lines.size());
        for(const LyricDataLine& in_line : data.lines)
        {
            // NOTE: Ordinarily a single line is just a single line and contains no newlines.
            //       However if two lines in an lrc file have identical timestamps, then we merge them
            //       during parsing. In that case we need to split them out again here.
            size_t start_index = 0;
            while(start_index <= in_line.text.length()) // This is specifically less-or-equal so that empty lines do not get ignored and show up in the editor
            {
                size_t end_index = std::min(in_line.text.length(), in_line.text.find('\n', start_index));
                size_t length = end_index - start_index;
                std::tstring out_text(&in_line.text.c_str()[start_index], length);
                out_lines.push_back({ out_text, in_line.timestamp });

                start_index = end_index+1;
            }
        }

        if(merge_equivalent_lrc_lines)
        {
            std::vector<std::pair<size_t, LyricDataLine>> indexed_lines = alg::enumerate(std::move(out_lines));

            const auto lexicographic_sort = [](const auto& lhs, const auto& rhs){ return lhs.second.text < rhs.second.text; };
            std::stable_sort(indexed_lines.begin(), indexed_lines.end(), lexicographic_sort);
            decltype(indexed_lines)::iterator equal_begin = indexed_lines.begin();

            while(equal_begin != indexed_lines.end())
            {
                decltype(indexed_lines)::iterator equal_end = equal_begin + 1;
                while((equal_end != indexed_lines.end()) && (equal_begin->second.text == equal_end->second.text) && (equal_end->second.timestamp != DBL_MAX))
                {
                    equal_end++;
                }

                // NOTE: We don't need to move equal_begin back one because we don't add
                //       the first timestamp to the string. That'll happen as part of the
                //       normal printing below.
                for(auto iter=equal_end-1; iter!=equal_begin; iter--)
                {
                    equal_begin->second.text = to_tstring(parsers::lrc::print_timestamp(iter->second.timestamp)) + equal_begin->second.text;
                }
                equal_begin = indexed_lines.erase(equal_begin+1, equal_end);
            }

            const auto index_sort = [](const auto& lhs, const auto& rhs){ return lhs.first < rhs.first; };
            std::sort(indexed_lines.begin(), indexed_lines.end(), index_sort);
            out_lines = alg::denumerate(indexed_lines);
        }

        for(const LyricDataLine& line : out_lines)
        {
            // Even timestamped lyrics can still contain untimestamped lines
            if(line.timestamp != DBL_MAX)
            {
                expanded_text += to_tstring(print_timestamp(line.timestamp));
            }
            expanded_text += line.text;
            expanded_text += _T("\r\n");
        }
    }
    else
    {
        // Not timestamped
        for(const LyricDataLine& line : data.lines)
        {
            assert(line.timestamp == DBL_MAX);
            if(line.text.empty())
            {
                // NOTE: In the lyric editor, we automatically select the next line after synchronising the current one.
                //       If the new-selected line has no timestamp and is empty then visually there will be no selection, which is a little confusing.
                //       To avoid this we add a space to such lines when loading the lyrics, which will be removed when we shrink the text for saving.
                expanded_text += _T(" ");
            }
            else
            {
                expanded_text += line.text;
            }
            expanded_text += _T("\r\n");
        }
    }

    return expanded_text;
}

} // namespace parsers::lrc


// ============
// Tests
// ============
#if MVTF_TESTS_ENABLED
MVTF_TEST(lrcparse_title_tag_extracted_from_lyrics)
{
    const std::string input = "[Ti:thetitle]\n[00:00.00]line1";

    const LyricData lyrics = parsers::lrc::parse({}, input);
    ASSERT(lyrics.lines.size() == 1);
    ASSERT(lyrics.lines[0].text == _T("line1"));
    ASSERT(lyrics.tags.size() == 1);
    ASSERT(lyrics.tags[0] == "[Ti:thetitle]");
}

MVTF_TEST(lrcparse_empty_title_tag_extracted_from_lyrics)
{
    const std::string input = "[ti:]\n[00:00.00]line1";

    const LyricData lyrics = parsers::lrc::parse({}, input);
    ASSERT(lyrics.lines.size() == 1);
    ASSERT(lyrics.lines[0].text == _T("line1"));
    ASSERT(lyrics.tags.size() == 1);
    ASSERT(lyrics.tags[0] == "[ti:]");
}

MVTF_TEST(lrcparse_title_case_encoding_tag_extracted_from_lyrics)
{
    // Checks for https://github.com/jacquesh/foo_openlyrics/issues/322
    const std::string input = "[Encoding:iso-8859-15]\n[00:00.00]line1";

    const LyricData lyrics = parsers::lrc::parse({}, input);
    ASSERT(lyrics.lines.size() == 1);
    ASSERT(lyrics.lines[0].text == _T("line1"));
    ASSERT(lyrics.tags.size() == 1);
    ASSERT(lyrics.tags[0] == "[Encoding:iso-8859-15]");
}

MVTF_TEST(lrcparse_timestamp_parsing_and_printing_roundtrips)
{
    // Checks for the timestamp-modifying part of https://github.com/jacquesh/foo_openlyrics/issues/354
    double parsed = 0.0;
    const std::string input = "[00:18.31]";
    const bool parse_success = parsers::lrc::try_parse_timestamp(input, parsed);
    const std::string output = parsers::lrc::print_timestamp(parsed);

    ASSERT(parse_success);
    ASSERT(parsed == 18.31);
    ASSERT(output == input);
}

MVTF_TEST(lrcparse_parsing_merges_lines_with_matching_timestamps)
{
    const std::string input = "[02:29.75]linePart1\n[02:29.75]linePart2";
    const LyricData parsed = parsers::lrc::parse({}, input);

    ASSERT(parsed.lines.size() == 1);
    ASSERT(parsed.lines[0].text == _T("linePart1\nlinePart2"));
}

MVTF_TEST(lrcparse_parsing_duplicates_lines_with_multiple_timestamps)
{
    const std::string input = "[00:36.28][01:25.09]dupe-line";
    const LyricData parsed = parsers::lrc::parse({}, input);

    ASSERT(parsed.lines.size() == 2);
    ASSERT(parsed.lines[0].text == _T("dupe-line"));
    ASSERT(parsed.lines[0].timestamp == 36.28);
    ASSERT(parsed.lines[1].text == _T("dupe-line"));
    ASSERT(parsed.lines[1].timestamp == 85.09);
}

MVTF_TEST(lrcparse_expanding_splits_lines_with_matching_timestamps)
{
    LyricData input = {};
    input.lines.push_back({_T("line1Part1\nline1Part2"), 149.75});
    input.lines.push_back({_T("line2Part1\nline2Part2\nline2Part3"), 153.09});

    const std::tstring output = parsers::lrc::expand_text(input, false);
    ASSERT(output == _T("[02:29.75]line1Part1\r\n[02:29.75]line1Part2\r\n[02:33.09]line2Part1\r\n[02:33.09]line2Part2\r\n[02:33.09]line2Part3\r\n"));
}

MVTF_TEST(lrcparse_expanding_splits_lines_with_matching_timestamps_and_then_merges_matching_lines)
{
    // Checks for the timestamp-modifying part of https://github.com/jacquesh/foo_openlyrics/issues/354
    LyricData input = {};
    input.lines.push_back({_T("linePart1\nlinePart2"), 149.75});
    input.lines.push_back({_T("linePart1\nlinePart2\nlinePart3"), 153.09});

    const std::tstring output = parsers::lrc::expand_text(input, true);
    ASSERT(output == _T("[02:29.75][02:33.09]linePart1\r\n[02:29.75][02:33.09]linePart2\r\n[02:33.09]linePart3\r\n"));
}

MVTF_TEST(lrcparse_expanding_splits_lines_with_matching_timestamps_in_their_original_order)
{
    LyricData input = {};
    input.lines.push_back({_T("lineBBBB\nlineAAAA"), 149.75});
    // These lines should remain in their given order, even though this is not lexicographic order,
    // which the code might conceivably change if it involved a sort to check for equivalent lines

    const std::tstring output = parsers::lrc::expand_text(input, true);
    ASSERT(output == _T("[02:29.75]lineBBBB\r\n[02:29.75]lineAAAA\r\n"));
}

MVTF_TEST(lrcparse_expanding_does_not_merge_matching_lines_when_not_requested)
{
    LyricData input = {};
    input.lines.push_back({_T("thebestline"), 5.0});
    input.lines.push_back({_T("thebestline"), 10.0});
    input.lines.push_back({_T("anotherline"), 12.0});
    input.lines.push_back({_T("anotherline"), 14.0});

    const std::tstring output = parsers::lrc::expand_text(input, false);
    ASSERT(output == _T("[00:05.00]thebestline\r\n[00:10.00]thebestline\r\n[00:12.00]anotherline\r\n[00:14.00]anotherline\r\n"));
}

MVTF_TEST(lrcparse_expanding_merges_matching_lines)
{
    LyricData input = {};
    input.lines.push_back({_T("thebestline"), 5.0});
    input.lines.push_back({_T("thebestline"), 10.0});
    input.lines.push_back({_T("anotherline"), 12.0});
    input.lines.push_back({_T("anotherline"), 14.0});

    const std::tstring output = parsers::lrc::expand_text(input, true);
    ASSERT(output == _T("[00:05.00][00:10.00]thebestline\r\n[00:12.00][00:14.00]anotherline\r\n"));
}

MVTF_TEST(lrcparse_expanding_merges_matching_lines_with_matching_timestamps)
{
    LyricData input = {};
    input.lines.push_back({_T("thebestline-part1"), 5.0});
    input.lines.push_back({_T("thebestline-part2"), 5.0});
    input.lines.push_back({_T("anotherline-part1"), 10.0});
    input.lines.push_back({_T("anotherline-part2"), 10.0});
    input.lines.push_back({_T("thebestline-part1"), 15.0});
    input.lines.push_back({_T("thebestline-part2"), 15.0});

    const std::tstring output = parsers::lrc::expand_text(input, true);
    ASSERT(output == _T("[00:05.00][00:15.00]thebestline-part1\r\n[00:05.00][00:15.00]thebestline-part2\r\n[00:10.00]anotherline-part1\r\n[00:10.00]anotherline-part2\r\n"));
}

MVTF_TEST(lrcparse_expanding_merges_matching_lines_in_timestamp_order)
{
    // Unfortunately I couldn't find a smaller example.
    // This is the result of std::sort not being std::stable_sort, so it depends on std::sort doing "unstable" things
    // which is not really something that is easily controlled from outside std::sort
    LyricData input = {};
    input.lines.push_back({_T(""), 0.0});
    input.lines.push_back({_T("13"), 0.83});
    input.lines.push_back({_T("14"), 10.79});
    input.lines.push_back({_T("15"), 18.31});
    input.lines.push_back({_T(""), 20.96});
    input.lines.push_back({_T("16"), 35.27});
    input.lines.push_back({_T("17"), 44.97});
    input.lines.push_back({_T("18"), 50.21});
    input.lines.push_back({_T(""), 54.53});
    input.lines.push_back({_T("19"), 54.66});
    input.lines.push_back({_T("20"), 60.05});
    input.lines.push_back({_T("21"), 64.40});
    input.lines.push_back({_T("22"), 69.90});
    input.lines.push_back({_T(""), 75.51});
    input.lines.push_back({_T("23"), 79.39});
    input.lines.push_back({_T("24"), 89.12});
    input.lines.push_back({_T("1"), 94.28});
    input.lines.push_back({_T(""), 98.51});
    input.lines.push_back({_T("2"), 98.72});
    input.lines.push_back({_T("3"), 104.10});
    input.lines.push_back({_T("4"), 108.52});
    input.lines.push_back({_T("22"), 113.96});
    input.lines.push_back({_T(""), 119.64});
    input.lines.push_back({_T("5"), 137.93});
    input.lines.push_back({_T("6"), 148.06});
    input.lines.push_back({_T("7"), 154.95});
    input.lines.push_back({_T(""), 161.98});
    input.lines.push_back({_T("20"), 167.83});
    input.lines.push_back({_T(""), 172.02});
    input.lines.push_back({_T("8"), 172.14});
    input.lines.push_back({_T("9"), 177.64});
    input.lines.push_back({_T("10"), 182.76});
    input.lines.push_back({_T("11"), 186.76});
    input.lines.push_back({_T(""), 189.80});

    const std::tstring output = parsers::lrc::expand_text(input, true);
    const TCHAR* expected = _T(
        "[00:00.00][00:20.96][00:54.53][01:15.51][01:38.51][01:59.64][02:41.98][02:52.02][03:09.80]\r\n"
        "[00:00.83]13\r\n"
        "[00:10.79]14\r\n"
        "[00:18.31]15\r\n"
        "[00:35.27]16\r\n"
        "[00:44.97]17\r\n"
        "[00:50.21]18\r\n"
        "[00:54.66]19\r\n"
        "[01:00.05][02:47.83]20\r\n"
        "[01:04.40]21\r\n"
        "[01:09.90][01:53.96]22\r\n"
        "[01:19.39]23\r\n"
        "[01:29.12]24\r\n"
        "[01:34.28]1\r\n"
        "[01:38.72]2\r\n"
        "[01:44.10]3\r\n"
        "[01:48.52]4\r\n"
        "[02:17.93]5\r\n"
        "[02:28.06]6\r\n"
        "[02:34.95]7\r\n"
        "[02:52.14]8\r\n"
        "[02:57.64]9\r\n"
        "[03:02.76]10\r\n"
        "[03:06.76]11\r\n");
    ASSERT(output == expected);
}

MVTF_TEST(lrcparse_expanding_places_untimestamped_lines_at_the_end_with_no_timestamp)
{
    LyricData input = {};
    input.lines.push_back({_T("timeline1"), 1.0});
    input.lines.push_back({_T("timeline2"), 2.0});
    input.lines.push_back({_T("untimed"), DBL_MAX});

    const std::tstring output = parsers::lrc::expand_text(input, true);
    ASSERT(output == _T("[00:01.00]timeline1\r\n[00:02.00]timeline2\r\nuntimed\r\n"));
}

MVTF_TEST(lrcparse_parseoffset_parses_positive_values)
{
    const std::string_view input = "[offset:1234]";
    const std::optional<double> output = parsers::lrc::try_parse_offset_tag(input);
    ASSERT(output.has_value());
    ASSERT(output.value() == 1.234);
}

MVTF_TEST(lrcparse_parseoffset_parses_negative_values)
{
    const std::string_view input = "[offset:-567]";
    const std::optional<double> output = parsers::lrc::try_parse_offset_tag(input);
    ASSERT(output.has_value());
    ASSERT(output.value() == -0.567);
}

MVTF_TEST(lrcparse_print_timestamp_correctly_rounds_up_when_given_input_very_near_to_the_next_second)
{
    // Checks for https://github.com/jacquesh/foo_openlyrics/issues/417
    // This would likely only happen when synchronising lines in the editor, since then the timestamp does
    // not come from text originally so it's not bound to 2 decimal places of precision.
    const std::string output = parsers::lrc::print_timestamp(5.999);
    ASSERT(output == "[00:06.00]");
}
#endif
