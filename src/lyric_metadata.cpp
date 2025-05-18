#include "stdafx.h"

#include "pfc/filetimetools.h"

#include "logging.h"
#include "lyric_data.h"
#include "lyric_metadb_index_client.h"
#include "sources/lyric_source.h"

// clang-format off: GUIDs should be one line
static const GUID GUID_METADBINDEX_LYRIC_METADATA = { 0x88da8d97, 0xb450, 0x4ff4, { 0xa8, 0x81, 0xf6, 0xf6, 0xad, 0x38, 0x36, 0xc1 } };
// clang-format on

DECLARE_OPENLYRICS_METADB_INDEX("lyric metadata", GUID_METADBINDEX_LYRIC_METADATA);
static constexpr size_t MAX_METADATA_BYTES = 4096;

struct lyric_metadata
{
    uint8_t version;

    GUID first_retrieval_source;
    t_filetimestamp first_retrieval_timestamp = pfc::filetimestamp_invalid;
    std::string first_retrieval_path;

    t_filetimestamp last_edit_timestamp = pfc::filetimestamp_invalid;
    uint32_t number_of_edits;
};

class lyric_metadb_index_maintenance : public metadb_io_edit_callback_v2
{
    void on_edited(metadb_handle_list_cref /*items*/, t_infosref /*before*/, t_infosref /*after*/) override {}

    void on_edited_v2(metadb_handle_list_cref /*items*/,
                      t_infosref /*before*/,
                      t_infosref after,
                      t_infosref beforeInMetadb) override
    {
        auto meta_index = metadb_index_manager_v2::get();
        char data_buffer[MAX_METADATA_BYTES] = {};

        metadb_index_transaction::ptr trans = meta_index->begin_transaction();

        assert(beforeInMetadb.size() == after.size());
        for(size_t i = 0; i < beforeInMetadb.size(); i++)
        {
            metadb_index_hash before_hash = lyric_metadb_index_client::hash(*beforeInMetadb[i]);
            metadb_index_hash after_hash = lyric_metadb_index_client::hash(*after[i]);
            if(before_hash == after_hash)
            {
                continue;
            }

            const size_t data_bytes = meta_index->get_user_data_here(GUID_METADBINDEX_LYRIC_METADATA,
                                                                     before_hash,
                                                                     data_buffer,
                                                                     sizeof(data_buffer));
            trans->set_user_data(GUID_METADBINDEX_LYRIC_METADATA, before_hash, nullptr, 0);
            trans->set_user_data(GUID_METADBINDEX_LYRIC_METADATA, after_hash, data_buffer, data_bytes);
        }
        trans->commit();
    }
};
static service_factory_single_t<lyric_metadb_index_maintenance> g_lyric_metadb_index_maintenance;

static lyric_metadata load_lyric_metadata(const metadb_v2_rec_t& track_info)
{
    char data_buffer[MAX_METADATA_BYTES] = {};

    auto meta_index = metadb_index_manager::get();
    metadb_index_hash our_index_hash = lyric_metadb_index_client::hash_handle(track_info);
    const size_t data_bytes = meta_index->get_user_data_here(GUID_METADBINDEX_LYRIC_METADATA,
                                                             our_index_hash,
                                                             data_buffer,
                                                             sizeof(data_buffer));
    if(data_bytes == 0)
    {
        LOG_INFO("No lyric metadata available for track");
        return {};
    }

    try
    {
        lyric_metadata result = {};
        stream_reader_formatter_simple<false> reader(data_buffer, data_bytes);
        reader >> result.version;

        if(result.version == 1)
        {
            reader >> result.first_retrieval_source;
            reader >> result.first_retrieval_timestamp;
            const pfc::string8 first_path = reader.read_string();
            reader >> result.last_edit_timestamp;
            reader >> result.number_of_edits;

            result.first_retrieval_path = std::string(first_path.c_str(), first_path.length());
        }
        else
        {
            LOG_WARN("Unexpected version number %u returned for lyric metadata consisting of %zu bytes",
                     result.version,
                     data_bytes);
        }
        return result;
    }
    catch(const std::exception& ex)
    {
        LOG_WARN("Failed to read lyric metadata info: %s", ex.what());
        return {};
    }
}

static void save_lyric_metadata(const metadb_v2_rec_t& track_info, lyric_metadata metadata)
{
    const uint8_t latest_metadata_version = 1;
    stream_writer_formatter_simple<false> writer;
    writer << latest_metadata_version;
    writer << metadata.first_retrieval_source;
    writer << metadata.first_retrieval_timestamp;
    writer.write_string(metadata.first_retrieval_path.c_str(), metadata.first_retrieval_path.length());
    writer << metadata.last_edit_timestamp;
    writer << metadata.number_of_edits;

    auto meta_index = metadb_index_manager::get();
    metadb_index_hash our_index_hash = lyric_metadb_index_client::hash_handle(track_info);
    meta_index->set_user_data(GUID_METADBINDEX_LYRIC_METADATA,
                              our_index_hash,
                              writer.m_buffer.get_ptr(),
                              writer.m_buffer.get_size());
}

void lyric_metadata_log_edit(const metadb_v2_rec_t& track_info)
{
    lyric_metadata metadata = load_lyric_metadata(track_info);
    metadata.number_of_edits++;
    metadata.last_edit_timestamp = pfc::fileTimeNow();
    save_lyric_metadata(track_info, metadata);
}

void lyric_metadata_log_retrieved(const metadb_v2_rec_t& track_info, const LyricData& lyrics)
{
    lyric_metadata metadata = load_lyric_metadata(track_info);
    if(metadata.first_retrieval_timestamp != filetimestamp_invalid)
    {
        // This track has been retrieved before
        return;
    }

    assert(lyrics.source_id != GUID {});
    metadata.first_retrieval_source = lyrics.source_id;
    metadata.first_retrieval_timestamp = pfc::fileTimeNow();
    metadata.first_retrieval_path = lyrics.source_path;

    save_lyric_metadata(track_info, metadata);
}

std::string get_lyric_metadata_string(const LyricData& lyrics, const metadb_v2_rec_t& track_info)
{
    std::string result;
    result += (lyrics.IsTimestamped() ? "Synced lyrics\n" : "Unsynced lyrics\n");

    LyricSourceBase* src = LyricSourceBase::get(lyrics.source_id);
    if(src != nullptr)
    {
        result += std::format("Retrieved from {} @ {}\n", from_tstring(src->friendly_name()), lyrics.source_path);
    }

    if(lyrics.save_source.has_value())
    {
        LyricSourceBase* saved_src = LyricSourceBase::get(lyrics.save_source.value());
        if(saved_src != nullptr)
        {
            result += std::format("Saved to {} @ {}\n", from_tstring(saved_src->friendly_name()), lyrics.save_path);
        }
    }

    const lyric_metadata metadata = load_lyric_metadata(track_info);
    const LyricSourceBase* first_src = LyricSourceBase::get(metadata.first_retrieval_source);

    if(first_src != nullptr)
    {
        const pfc::string_formatter first_retrieved_str = pfc::format_filetimestamp(metadata.first_retrieval_timestamp);
        result += std::format("First retrieved from {} at {} @ {}\n",
                              from_tstring(first_src->friendly_name()),
                              std::string_view(first_retrieved_str.c_str(), first_retrieved_str.length()),
                              metadata.first_retrieval_path);
    }
    else
    {
        result += "First retrieved from an unknown source\n";
    }

    if(metadata.number_of_edits == 0)
    {
        result += "Never edited\n";
    }
    else
    {
        const pfc::string_formatter last_edit_str = pfc::format_filetimestamp(metadata.last_edit_timestamp);
        if(metadata.number_of_edits == 1)
        {
            result += std::format("Edited 1 time, at {}\n",
                                  std::string_view(last_edit_str.c_str(), last_edit_str.length()));
        }
        else
        {
            result += std::format("Edited {} times, last edited at {}\n",
                                  metadata.number_of_edits,
                                  std::string_view(last_edit_str.c_str(), last_edit_str.length()));
        }
    }

    return result;
}
