#include "stdafx.h"

#include "cJSON.h"

#include "logging.h"
#include "lyric_data.h"
#include "lyric_source.h"
#include "preferences.h"
#include "source_io.h"
#include "source_test_support.h"

static const GUID src_guid = { 0xf94ba31a, 0x7b33, 0x49e4, { 0x81, 0x9b, 0x0, 0xc, 0x36, 0x44, 0x29, 0xcd } };

class MusixmatchLyricsSource : public LyricSourceRemote
{
    const GUID& id() const final
    {
        return src_guid;
    }
    std::tstring_view friendly_name() const final
    {
        return _T("Musixmatch");
    }

    std::vector<LyricDataRaw> search(const LyricSearchParams& params, abort_callback& abort) final;
    bool lookup(LyricDataRaw& data, abort_callback& abort) final;

private:
    std::vector<LyricDataRaw> get_song_ids(const LyricSearchParams& params, abort_callback& abort) const;
    bool get_lyrics(LyricDataRaw& data,
                    int64_t track_id,
                    abort_callback& abort,
                    const char* method,
                    const char* body_entry_name,
                    const char* text_entry_name) const;
    bool get_unsynced_lyrics(LyricDataRaw& data, int64_t track_id, abort_callback& abort) const;
    bool get_synced_lyrics(LyricDataRaw& data, int64_t track_id, abort_callback& abort) const;
};
static const LyricSourceFactory<MusixmatchLyricsSource> src_factory;

static const char* g_api_url = "https://apic-appmobile.musixmatch.com/ws/1.1/";
static const char* g_common_params = "user_language=en&app_id=mac-ios-v2.0";

std::vector<LyricDataRaw> MusixmatchLyricsSource::get_song_ids(const LyricSearchParams& params,
                                                               abort_callback& abort) const
{
    // If this breaks, we can check the code used in ESLyric, which somebody may have fixed.
    // That currently lives at:
    // https://github.com/ESLyric/scripts/blob/82ea7351c8e0cb333fda1ae36b6917e68b6613c1/searcher/musixmatch.js

    const std::string_view apikey = source_io::musixmatch_token();
    std::string url = std::string(g_api_url) + "track.search?" + g_common_params + "&subtitle_format=lrc";
    url += "&q_artist=" + urlencode(params.artist);
    url += "&q_album=" + urlencode(params.album);
    url += "&q_track=" + urlencode(params.title);
    url += "&usertoken=";
    LOG_INFO("Querying for track ID from %s", url.c_str());
    url += apikey; // Add this after logging so we don't log sensitive info

    pfc::string8 content;
    try
    {
        http_request::ptr request = source_io::create_request("GET");

        // NOTE: Without adding the AWSELB and AWSELBCORS headers, we get a 301 (permanent redirect back)
        //       with a header instructing us to set those cookies to the given hash.
        //       The fb2k http API automatically follows the redirect but does not honour the Set-Cookie headers.
        //       The redirect goes to the same URL and the request then fails after a while (presumably because
        //       ELB thinks we're DoS'ing them and kills the connection).
        //       Setting the headers here to just *some* value (even if its not a useful one) seems to make it work.
        //       We may need to upgrade this in future to actually set the cookies that we're asked to set.
        request->add_header("cookie", "AWSELBCORS=0; AWSELB=0");

        file_ptr response_file = request->run(url.c_str(), abort);

        response_file->read_string_raw(content, abort);
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to make Musixmatch search request: %s", e.what());
        return {};
    }

    cJSON* json = cJSON_ParseWithLength(content.c_str(), content.get_length());
    cJSON* json_message = cJSON_GetObjectItem(json, "message");
    cJSON* json_body = cJSON_GetObjectItem(json_message, "body");
    cJSON* json_tracklist = cJSON_GetObjectItem(json_body, "track_list");
    if(!cJSON_IsArray(json_tracklist))
    {
        // If our user token is invalid we don't get a body, we get a header asking us to renew it.
        // In that case we should prompt the user to get a new token.
        cJSON* json_header = cJSON_GetObjectItem(json_message, "header");
        cJSON* json_hint = cJSON_GetObjectItem(json_header, "hint");
        if(cJSON_IsString(json_hint) && std::string_view(json_hint->valuestring) == "renew")
        {
            LOG_INFO("Musixmatch token is invalid or expired, please generate a new one");
            static bool should_prompt_for_new_token = true;
            if(should_prompt_for_new_token)
            {
                // Only prompt once for every time that you launch fb2k.
                // We *could* persist this to never ask again but searches will never work while this
                // continues so people can just take Musixmatch out of their config.
                should_prompt_for_new_token = false;
                fb2k::inMainThread2(
                    []
                    {
                        std::optional<std::string> token =
                            preferences::searching::musixmatch_prompt_for_api_key_generation(
                                "Your musixmatch token has expired or is invalid, searches against "
                                "musixmatch will fail until a new one is generated",
                                core_api::get_main_window());
                        if(token.has_value())
                        {
                            LOG_INFO("Musixmatch token generated?");
                        }
                    });
            }
        }
        else
        {
            LOG_WARN("Received musixmatch search result but track_list was malformed: %s", content.c_str());
        }

        cJSON_Delete(json);
        return {};
    }

    std::vector<LyricDataRaw> results;
    cJSON* json_track = nullptr;
    cJSON_ArrayForEach(json_track, json_tracklist)
    {
        cJSON* json_tracktrack = cJSON_GetObjectItem(json_track, "track");

        cJSON* json_artist = cJSON_GetObjectItem(json_tracktrack, "artist_name");
        cJSON* json_album = cJSON_GetObjectItem(json_tracktrack, "album_name");
        cJSON* json_title = cJSON_GetObjectItem(json_tracktrack, "track_name");
        cJSON* json_haslyrics = cJSON_GetObjectItem(json_tracktrack, "has_lyrics");
        cJSON* json_hassubtitles = cJSON_GetObjectItem(json_tracktrack, "has_subtitles");
        cJSON* json_trackid = cJSON_GetObjectItem(json_tracktrack, "commontrack_id");
        cJSON* json_duration = cJSON_GetObjectItem(json_tracktrack, "track_length");

        if(!cJSON_IsString(json_artist) || !cJSON_IsString(json_album) || !cJSON_IsString(json_title)
           || !cJSON_IsNumber(json_haslyrics) || !cJSON_IsNumber(json_hassubtitles) || !cJSON_IsNumber(json_trackid)
           || !cJSON_IsNumber(json_duration))
        {
            LOG_WARN("Received musixmatch search result but track info was malformed: %s", content.c_str());
            break;
        }

        LyricDataRaw data = {};
        data.source_id = id();
        data.artist = json_artist->valuestring;
        data.album = json_album->valuestring;
        data.title = json_title->valuestring;
        data.duration_sec = json_duration->valueint;

        if(json_hassubtitles->valueint != 0)
        {
            data.lookup_id = std::to_string(json_trackid->valueint);
            data.type = LyricType::Synced;
            results.push_back(data); // Don't move so we can use it again below
        }
        if(json_haslyrics->valueint != 0)
        {
            data.lookup_id = std::to_string(json_trackid->valueint);
            data.type = LyricType::Unsynced;
            results.push_back(std::move(data));
        }
    }

    cJSON_Delete(json);
    return results;
}

bool MusixmatchLyricsSource::get_lyrics(LyricDataRaw& data,
                                        int64_t track_id,
                                        abort_callback& abort,
                                        const char* method,
                                        const char* body_entry_name,
                                        const char* text_entry_name) const
{
    assert(data.source_id == id());

    const std::string_view apikey = source_io::musixmatch_token();
    std::string url = std::string(g_api_url) + method + "?" + g_common_params
                      + "&commontrack_id=" + std::to_string(track_id) + "&usertoken=";
    LOG_INFO("Get Musixmatch lyrics from %s", url.c_str());
    url += apikey; // Add this after logging so we don't log sensitive info
    data.source_path = url;

    pfc::string8 content;
    try
    {
        http_request::ptr request = source_io::create_request("GET");
        request->add_header("cookie",
                            "AWSELBCORS=0; AWSELB=0"); // NOTE: See the comment on the cookie in the track ID query

        file_ptr response_file = request->run(url.c_str(), abort);

        response_file->read_string_raw(content, abort);
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to make Musixmatch %s request: %s", method, e.what());
        return false;
    }

    cJSON* json = cJSON_ParseWithLength(content.c_str(), content.get_length());
    cJSON* json_message = cJSON_GetObjectItem(json, "message");
    cJSON* json_body = cJSON_GetObjectItem(json_message, "body");
    cJSON* json_lyrics = cJSON_GetObjectItem(json_body, body_entry_name);
    cJSON* json_lyricstext = cJSON_GetObjectItem(json_lyrics, text_entry_name);
    if(!cJSON_IsString(json_lyricstext))
    {
        LOG_WARN("Received musixmatch %s response but %s was malformed: %s", method, text_entry_name, content.c_str());
        cJSON_Delete(json);
        return false;
    }

    if(strlen(json_lyricstext->valuestring) == 0)
    {
        LOG_INFO("Received an empty lyric string from musixmatch, no lyrics will be returned");
        cJSON_Delete(json);
        return false;
    }

    data.text_bytes = string_to_raw_bytes(json_lyricstext->valuestring);
    cJSON_Delete(json);
    return true;
}

bool MusixmatchLyricsSource::get_unsynced_lyrics(LyricDataRaw& data, int64_t track_id, abort_callback& abort) const
{
    return get_lyrics(data, track_id, abort, "track.lyrics.get", "lyrics", "lyrics_body");
}

bool MusixmatchLyricsSource::get_synced_lyrics(LyricDataRaw& data, int64_t track_id, abort_callback& abort) const
{
    return get_lyrics(data, track_id, abort, "track.subtitle.get", "subtitle", "subtitle_body");
}

std::vector<LyricDataRaw> MusixmatchLyricsSource::search(const LyricSearchParams& params, abort_callback& abort)
{
    if(source_io::musixmatch_token().empty())
    {
        // An API key is required.
        // Skip the search if we don't have one so we don't accidentally spam their servers with obviously-bad requests.
        LOG_INFO("Skipping request to the Musixmatch source because no API key is available");
        return {};
    }

    return get_song_ids(params, abort);
}

bool MusixmatchLyricsSource::lookup(LyricDataRaw& data, abort_callback& abort)
{
    const int64_t track_id = strtoll(data.lookup_id.c_str(), nullptr, 10);
    if(track_id == 0)
    {
        LOG_WARN("Attempt to lookup musixmatch lyrics with null lookup track ID: %s", data.lookup_id.c_str());
        return false;
    }

    switch(data.type)
    {
        case LyricType::Synced: return get_synced_lyrics(data, track_id, abort);
        case LyricType::Unsynced: return get_unsynced_lyrics(data, track_id, abort);
        default: return false;
    }
}

std::string musixmatch_get_token(abort_callback& abort)
{
    std::string url = std::string(g_api_url) + "token.get?" + g_common_params;
    LOG_INFO("Attempting to get Musixmatch token from %s...", url.c_str());

    pfc::string8 content;
    try
    {
        http_request::ptr request = source_io::create_request("GET");
        request->add_header("cookie",
                            "AWSELBCORS=0; AWSELB=0"); // NOTE: See the comment on the cookie in the track ID query

        file_ptr response_file = request->run(url.c_str(), abort);

        response_file->read_string_raw(content, abort);
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to get Musixmatch token from %s: %s", url.c_str(), e.what());
        return "";
    }

    cJSON* json = cJSON_ParseWithLength(content.c_str(), content.get_length());
    if((json == nullptr) || (json->type != cJSON_Object))
    {
        LOG_WARN("Received musixmatch token response but root was malformed: %s", content.c_str());
        cJSON_Delete(json);
        return "";
    }

    cJSON* json_message = cJSON_GetObjectItem(json, "message");
    if((json_message == nullptr) || (json_message->type != cJSON_Object))
    {
        LOG_WARN("Received musixmatch token response but message was malformed: %s", content.c_str());
        cJSON_Delete(json);
        return "";
    }

    cJSON* json_body = cJSON_GetObjectItem(json_message, "body");
    if((json_body == nullptr) || (json_body->type != cJSON_Object))
    {
        LOG_WARN("Received musixmatch token response but body was malformed: %s", content.c_str());
        cJSON_Delete(json);
        return "";
    }

    cJSON* json_token = cJSON_GetObjectItem(json_body, "user_token");
    if((json_token == nullptr) || (json_token->type != cJSON_String))
    {
        LOG_WARN("Received musixmatch token response but user_token was malformed: %s", content.c_str());
        cJSON_Delete(json);
        return "";
    }

    std::string result = json_token->valuestring;
    cJSON_Delete(json);
    return result;
}

#if MVTF_TESTS_ENABLED
SOURCE_FAILURE_TESTS(MusixmatchLyricsSource, musixmatch)
SOURCE_LOOKUP_FAILURE_TESTS(MusixmatchLyricsSource, musixmatch, "{}")
MVTF_TEST(musixmatch_fetches_search_metadata_and_looks_up_lyrics)
{
    LyricSourceFactory<MusixmatchLyricsSource> factory;
    source_tests::search_lookup_success(
        factory.get_static_instance(),
        R"fixture({"message":{"body":{"track_list":[{"track":{"artist_name":"Artist","album_name":"Album","track_name":"Song","has_lyrics":1,"has_subtitles":1,"commontrack_id":42,"track_length":222}}]}}})fixture",
        R"fixture({"message":{"body":{"subtitle":{"subtitle_body":"[00:01.00]Line one\n[00:02.00]Line two"},"lyrics":{"lyrics_body":"Line one\nLine two"}}}})fixture",
        "Line one\nLine two",
        2,
        "GET",
        mvtf_error_count,
        "[00:01.00]Line one\n[00:02.00]Line two");
}
#endif
#if MVTF_TESTS_ENABLED
MVTF_TEST(musixmatch_without_token_does_not_send_requests)
{
    source_io::FixtureIO io;
    io.token.clear();
    LyricSourceFactory<MusixmatchLyricsSource> factory;
    LyricSourceRemote& source = factory.get_static_instance();
    abort_callback_dummy abort;
    CHECK(source.search(source_tests::params(), abort).empty());
    CHECK(io.requests.empty());
}

MVTF_TEST(musixmatch_token_response_is_parsed_and_malformed_response_rejected)
{
    source_io::FixtureIO io;
    io.responses = { { R"fixture({"message":{"body":{"user_token":"synthetic-test-token"}}})fixture" }, { "{}" } };
    abort_callback_dummy abort;
    CHECK(musixmatch_get_token(abort) == "synthetic-test-token");
    CHECK(musixmatch_get_token(abort).empty());
    CHECK(io.requests.size() == 2);
}
#endif
