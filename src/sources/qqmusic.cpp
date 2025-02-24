#include "stdafx.h"

#include "cJSON.h"

#include "logging.h"
#include "lyric_data.h"
#include "lyric_source.h"

static const GUID src_guid = { 0x4b0b5722, 0x3a84, 0x4b8e, { 0x82, 0x7a, 0x26, 0xb9, 0xea, 0xb3, 0xb4, 0xe8 } };

class QQMusicLyricsSource : public LyricSourceRemote
{
    const GUID& id() const final { return src_guid; }
    std::tstring_view friendly_name() const final { return _T("QQ Music"); }

    std::vector<LyricDataRaw> search(const LyricSearchParams& params, abort_callback& abort) final;
    bool lookup(LyricDataRaw& data, abort_callback& abort) final;

private:
    std::vector<LyricDataRaw> parse_song_ids(cJSON* json) const;
};
static const LyricSourceFactory<QQMusicLyricsSource> src_factory;

static http_request::ptr make_get_request()
{
    http_request::ptr request = http_client::get()->create_request("GET");
    request->add_header("Referer", "http://y.qq.com/portal/player.html");
    return request;
}

std::vector<LyricDataRaw> QQMusicLyricsSource::parse_song_ids(cJSON* json) const
{
    if((json == nullptr) || (json->type != cJSON_Object))
    {
        LOG_INFO("Root object is null or not an object");
        return {};
    }

    cJSON* result_obj = cJSON_GetObjectItem(json, "data");
    if((result_obj == nullptr) || (result_obj->type != cJSON_Object))
    {
        LOG_INFO("No valid 'data' property available");
        return {}; 
    }
    cJSON* song_obj = cJSON_GetObjectItem(result_obj, "song");
    if((song_obj == nullptr) || (song_obj->type != cJSON_Object))
    {
        LOG_INFO("No valid 'song' property available");
        return {};
    }

    cJSON* song_arr = cJSON_GetObjectItem(song_obj, "itemlist");
    if((song_arr == nullptr) || (song_arr->type != cJSON_Array))
    {
        LOG_INFO("No valid 'list' property available");
        return {};
    }

    int song_arr_len = cJSON_GetArraySize(song_arr);
    if(song_arr_len <= 0)
    {
        LOG_INFO("Songs array has no items available");
        return {};
    }

    std::vector<LyricDataRaw> output;
    for(int song_index=0; song_index<song_arr_len; song_index++)
    {
        cJSON* song_item = cJSON_GetArrayItem(song_arr, song_index);
        if((song_item == nullptr) || (song_item->type != cJSON_Object))
        {
            LOG_INFO("Song array entry %d not available or invalid", song_index);
            continue;
        }

        const char* result_artist = nullptr;
        const char* result_title = nullptr;

        cJSON* artist_item = cJSON_GetObjectItem(song_item, "singer");
        if((artist_item != nullptr) && (artist_item->type == cJSON_String))
        {
            result_artist = artist_item->valuestring;
        }

        cJSON* title_item = cJSON_GetObjectItem(song_item, "name");
        if((title_item != nullptr) && (title_item->type == cJSON_String))
        {
            result_title = title_item->valuestring;
        }

        cJSON* song_id_item = cJSON_GetObjectItem(song_item, "mid");
        if((song_id_item == nullptr) || (song_id_item->type != cJSON_String))
        {
            LOG_INFO("Song item ID field is not available or invalid");
            continue;
        }

        LyricDataRaw data = {};
        data.source_id = src_guid;
        if(result_artist != nullptr) data.artist = result_artist;
        if(result_title != nullptr) data.title = result_title;
        data.lookup_id = song_id_item->valuestring;
        data.type = LyricType::Synced;
        output.push_back(std::move(data));
    }

    return output;
}

std::vector<LyricDataRaw> QQMusicLyricsSource::search(const LyricSearchParams& params, abort_callback& abort)
{
    std::string url = "https://c.y.qq.com/splcloud/fcgi-bin/smartbox_new.fcg?inCharset=utf-8&outCharset=utf-8&key=" + urlencode(params.artist) + '+' + urlencode(params.title);
    LOG_INFO("Querying for song ID from %s…", url.c_str());

    pfc::string8 content;
    try
    {
        http_request::ptr request = make_get_request();
        file_ptr response_file = request->run(url.c_str(), abort);

        response_file->read_string_raw(content, abort);
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to download QQMusic page %s: %s", url.c_str(), e.what());
        return {};
    }

    cJSON* json = cJSON_ParseWithLength(content.c_str(), content.get_length());
    std::vector<LyricDataRaw> song_ids = parse_song_ids(json);
    cJSON_Delete(json);

    return song_ids;
}

bool QQMusicLyricsSource::lookup(LyricDataRaw& data, abort_callback& abort)
{
    assert(data.source_id == id());
    if(data.lookup_id.empty())
    {
        return false;
    }

    std::string url = "http://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?g_tk=5381&format=json&inCharset=utf-8&outCharset=utf-8&songmid=" + data.lookup_id;
    data.source_path = url;
    LOG_INFO("Get QQMusic lyrics for song ID %s from %s…", data.lookup_id.c_str(), url.c_str());

    pfc::string8 content;
    try
    {
        http_request::ptr request = make_get_request();
        file_ptr response_file = request->run(url.c_str(), abort);

        response_file->read_string_raw(content, abort);
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to download QQMusic page %s: %s", url.c_str(), e.what());
        return false;
    }

    bool success = false;
    cJSON* json = cJSON_ParseWithLength(content.c_str(), content.get_length());
    if((json != nullptr) && (json->type == cJSON_Object))
    {
        cJSON* lyric_item = cJSON_GetObjectItem(json, "lyric");
        if((lyric_item != nullptr) && (lyric_item->type == cJSON_String))
        {
            pfc::string8 lyric_str;
            pfc::base64_decode_to_string(lyric_str, lyric_item->valuestring);

            data.text_bytes = string_to_raw_bytes(std::string_view(lyric_str.c_str(), lyric_str.length()));
            success = true;
        }
    }
    cJSON_Delete(json);

    return success;
}

