#include "stdafx.h"
#include <cctype>

#include "cJSON.h"

#include "logging.h"
#include "lyric_source.h"
#include "tag_util.h"

static const GUID src_guid = { 0xb4cf497f, 0xd2c, 0x45ff, { 0xaa, 0x46, 0xf1, 0x45, 0xa7, 0xf, 0x90, 0x14 } };

constexpr int RESULT_LIMIT = 3;
// The Genius API client access key for foo_openlyrics
constexpr auto API_KEY_HEADER = "Authorization: Bearer ZTejoT_ojOEasIkT9WrMBhBQOz6eYKK5QULCMECmOhvwqjRZ6WbpamFe3geHnvp3";

class GeniusComSource : public LyricSourceRemote
{
    const GUID& id() const final { return src_guid; }
    std::tstring_view friendly_name() const final { return _T("Genius.com"); }

    std::vector<LyricDataRaw> search(const LyricSearchParams& params, abort_callback& abort) final;
    bool lookup(LyricDataRaw& data, abort_callback& abort) final;
};
static const LyricSourceFactory<GeniusComSource> src_factory;

static std::string remove_chars_for_url(const std::string_view input)
{
    std::string transliterated = from_tstring(normalise_utf8(to_tstring(input)));

    std::string output;
    output.reserve(transliterated.length() + 3); // We add a bit to allow for one or two & or @ replacements without re-allocation
    for(char c : transliterated)
    {
        if(pfc::char_is_ascii_alphanumeric(c))
        {
            output += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else if((c == ' ') || (c == '-'))
        {
            output += '-';
        }
        else if(c == '&')
        {
            output += "and";
        }
        else if(c == '@')
        {
            output += "at";
        }
    }

    return output;
}

std::vector<LyricDataRaw> GeniusComSource::search(const LyricSearchParams& params, abort_callback& abort)
{
    auto request = http_client::get()->create_request("GET");
    request->add_header(API_KEY_HEADER);

    std::string url = "https://api.genius.com/search?q=";
    url += remove_chars_for_url(params.artist);
    url += ' ';
    url += remove_chars_for_url(params.title);

    pfc::string8 content;
    try
    {
        file_ptr response_file = request->run(url.c_str(), abort);
        response_file->read_string_raw(content, abort);
        // NOTE: We're assuming here that the response is encoded in UTF-8 
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to download genius.com page %s: %s", url.c_str(), e.what());
        return {};
    }

    std::vector<LyricDataRaw> song_metadata;
    {   // Parser gets its own scope
        cJSON* json = cJSON_Parse(content.c_str());
        if (json == nullptr) {
            LOG_WARN("Failed to parse genius.com search result %s", content.c_str());
            cJSON_Delete(json);
            return {};
        }

        const cJSON* search_response = cJSON_GetObjectItem(json, "response");
        const cJSON* search_hits = cJSON_GetObjectItem(search_response, "hits");

        if (!cJSON_IsArray(search_hits)) {
            LOG_INFO("Failed to download genius.com page: Invalid search payload");
            cJSON_Delete(json);
            return {};
        }

        int results = 0;
        const cJSON* search_hit = nullptr;
        cJSON_ArrayForEach(search_hit, search_hits) {
            results++;
            if (results > RESULT_LIMIT) {
                break;
            }

			const cJSON* search_result = cJSON_GetObjectItem(search_hit, "result");
			const cJSON* search_path = cJSON_GetObjectItem(search_result, "api_path");
			const cJSON* search_title = cJSON_GetObjectItem(search_result, "title");
            // "artist_names" returns a list of all the artists involved, properly attributed
			const cJSON* search_artist = cJSON_GetObjectItem(search_result, "artist_names");

            if (search_path == nullptr || search_title == nullptr || search_artist == nullptr) {
                LOG_WARN("Failed to download from genius.com: Missing search data!");
                cJSON_Delete(json);
                return {};
            }

            if (search_artist->type != cJSON_String || search_title->type != cJSON_String || search_path->type != cJSON_String) {
                LOG_WARN("Failed to download from genius.com: Invalid search data!");
                cJSON_Delete(json);
                return {};
            }

			LyricDataRaw result = {};
			result.source_id = id();
			result.lookup_id = search_path->valuestring;
            result.source_path = search_path->valuestring;
            result.title = search_title->valuestring;
            result.artist = search_artist->valuestring;

			song_metadata.push_back(result);
        }

        cJSON_Delete(json);
    }

    return song_metadata;
}

bool GeniusComSource::lookup(LyricDataRaw& data, abort_callback& abort)
{
    auto request = http_client::get()->create_request("GET");
    request->add_header(API_KEY_HEADER);

    std::string url = std::format("https://api.genius.com{}?text_format=plain", data.lookup_id);

    pfc::string8 content;
    try
    {
        file_ptr response_file = request->run(url.c_str(), abort);
        response_file->read_string_raw(content, abort);
        // NOTE: We're assuming here that the response is encoded in UTF-8 
    }
    catch (const std::exception& e)
    {
        LOG_WARN("Failed to download genius.com page %s: %s", url.c_str(), e.what());
        return false;
    }

    LOG_INFO("Successfully retrieved lyrics from %s", url.c_str());

    {   // Parser gets its own scope
        cJSON* json = cJSON_Parse(content.c_str());
        if (json == nullptr) {
            LOG_WARN("Received genius.com API response but content was malformed: %s", content.c_str());
            cJSON_Delete(json);
            return false;
        }

        const cJSON* song_response = cJSON_GetObjectItem(json, "response");
        const cJSON* song_song = cJSON_GetObjectItem(song_response, "song");
        const cJSON* song_lyrics = cJSON_GetObjectItem(song_song, "lyrics");
        const cJSON* song_lyrics_plain = cJSON_GetObjectItem(song_lyrics, "plain");

        if (song_lyrics_plain == nullptr) {
            LOG_WARN("Failed to download from genius.com: No lyrics data!");
            cJSON_Delete(json);
            return {};
        }

        if (song_lyrics_plain->type != cJSON_String) {
            LOG_WARN("Failed to download from genius.com: Invalid lyrics data!");
            cJSON_Delete(json);
            return {};
        }

        data.text_bytes = string_to_raw_bytes(std::string_view(song_lyrics_plain->valuestring));
        cJSON_Delete(json);
        return true;
    }
    return false;
}
