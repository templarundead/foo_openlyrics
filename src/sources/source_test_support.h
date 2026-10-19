#pragma once

#include "mvtf/mvtf.h"

#if MVTF_TESTS_ENABLED
#include "lyric_source.h"
#include "parsers.h"
#include "source_io.h"
#include <stdexcept>
#pragma warning(push, 0)
#include "foobar2000/SDK/metadb_info_container_impl.h"
#pragma warning(pop)

namespace source_tests
{
    inline metadb_v2_rec_t track_info(const file_info& info)
    {
        auto container = fb2k::service_new<metadb_info_container_const_impl>();
        container->m_info = info;
        container->m_stats = filestats2_invalid;
        metadb_v2_rec_t record = {};
        record.info = container;
        return record;
    }

    inline LyricSearchParams params()
    {
        return LyricSearchParams("Artist", "Album", "Song", {});
    }
    inline std::string text(const LyricDataRaw& data)
    {
        return { data.text_bytes.begin(), data.text_bytes.end() };
    }

    inline void search_success(LyricSourceRemote& source,
                               const char* body,
                               const char* expected,
                               LyricType type,
                               const char* url_fragment,
                               int* mvtf_error_count)
    {
        source_io::FixtureIO io;
        // Bandcamp attempts numbered pages; a 404 on page 2 must retain the first result.
        io.responses = { { body }, { "", 404 } };
        abort_callback_dummy abort;
        const auto result = source.search(params(), abort);
        ASSERT(result.size() == 1);
        CHECK(result[0].source_id == source.id());
        CHECK(result[0].artist == "Artist");
        CHECK(result[0].title == "Song");
        CHECK(result[0].type == type);
        CHECK(text(result[0]) == expected);
        CHECK(!result[0].source_path.empty());
        ASSERT(!io.requests.empty());
        CHECK(io.requests[0].url.find(url_fragment) != std::string::npos);
        CHECK(io.requests[0].method == "GET");
        const auto parsed = parsers::lrc::parse(result[0], text(result[0]));
        CHECK(!parsed.IsEmpty());
        CHECK(parsed.IsTimestamped() == (type == LyricType::Synced));
    }

    inline void search_rejects_bad_response(LyricSourceRemote& source, int failure_kind, int* mvtf_error_count)
    {
        source_io::FixtureIO io;
        io.responses = { { "{}", failure_kind == 2 ? 503L : 200L, failure_kind != 1 } };
        abort_callback_dummy abort;
        try
        {
            CHECK(source.search(params(), abort).empty());
        }
        catch(const std::runtime_error&)
        {
            CHECK(failure_kind == 0);
        } // Scrapers report layout changes.
        CHECK(io.requests.size() >= 1);
    }

    inline void search_lookup_success(LyricSourceRemote& source,
                                      const char* search_body,
                                      const char* lookup_body,
                                      const char* expected,
                                      size_t count,
                                      const char* method,
                                      int* mvtf_error_count,
                                      const char* synced_expected = nullptr)
    {
        source_io::FixtureIO io;
        io.responses.push_back({ search_body });
        for(size_t i = 0; i < count; ++i)
            io.responses.push_back({ lookup_body });
        abort_callback_dummy abort;
        auto results = source.search(params(), abort);
        ASSERT(results.size() == count);
        ASSERT(io.requests.size() == 1);
        CHECK(io.requests[0].method == method);
        for(auto& data : results)
        {
            CHECK(data.source_id == source.id());
            CHECK(data.artist == "Artist");
            CHECK(data.title == "Song");
            CHECK(!data.lookup_id.empty());
            ASSERT(source.lookup(data, abort));
            ASSERT(!io.requests.empty());
            CHECK(io.requests.back().url.find("42") != std::string::npos);
            CHECK(io.requests.back().method == method);
            const char* expected_text = (data.type == LyricType::Synced && synced_expected != nullptr) ? synced_expected
                                                                                                       : expected;
            CHECK(text(data) == expected_text);
            const auto parsed = parsers::lrc::parse(data, text(data));
            CHECK(!parsed.IsEmpty());
            CHECK(parsed.IsTimestamped() == (data.type == LyricType::Synced));
            CHECK(!data.source_path.empty());
            if(data.duration_sec.has_value()) CHECK(data.duration_sec.value() == 222);
        }
        CHECK(io.next_response == count + 1);
    }

    inline void search_cancelled(LyricSourceRemote& source, int* mvtf_error_count)
    {
        source_io::FixtureIO io;
        abort_callback_impl abort;
        abort.abort();
        try
        {
            CHECK(source.search(params(), abort).empty());
        }
        catch(const exception_aborted&)
        {
        }
        CHECK(io.requests.empty());
        CHECK(io.next_response == 0);
    }

    inline void lookup_rejects_bad_response(LyricSourceRemote& source,
                                            bool transport_failure,
                                            const char* malformed,
                                            int* mvtf_error_count)
    {
        source_io::FixtureIO io;
        io.responses = { { malformed, transport_failure ? 503L : 200L, !transport_failure } };
        abort_callback_dummy abort;
        LyricDataRaw data = {};
        data.source_id = source.id();
        data.lookup_id = "42";
        data.type = LyricType::Synced;
        CHECK(!source.lookup(data, abort));
        CHECK(data.text_bytes.empty());
        CHECK(io.requests.size() == 1);
    }
}

// Each translation unit owns its source factory, avoiding any dependency on fb2k's initquit lifecycle.
#define SOURCE_FAILURE_TESTS(TYPE, PREFIX)                                                                             \
    MVTF_TEST(PREFIX##_rejects_transport_failure)                                                                      \
    {                                                                                                                  \
        LyricSourceFactory<TYPE> factory;                                                                              \
        source_tests::search_rejects_bad_response(factory.get_static_instance(), 1, mvtf_error_count);                 \
    }                                                                                                                  \
    MVTF_TEST(PREFIX##_rejects_malformed_response)                                                                     \
    {                                                                                                                  \
        LyricSourceFactory<TYPE> factory;                                                                              \
        source_tests::search_rejects_bad_response(factory.get_static_instance(), 0, mvtf_error_count);                 \
    }                                                                                                                  \
    MVTF_TEST(PREFIX##_rejects_http_error_status)                                                                      \
    {                                                                                                                  \
        LyricSourceFactory<TYPE> factory;                                                                              \
        source_tests::search_rejects_bad_response(factory.get_static_instance(), 2, mvtf_error_count);                 \
    }                                                                                                                  \
    MVTF_TEST(PREFIX##_honours_cancellation)                                                                           \
    {                                                                                                                  \
        LyricSourceFactory<TYPE> factory;                                                                              \
        source_tests::search_cancelled(factory.get_static_instance(), mvtf_error_count);                               \
    }

#define SOURCE_LOOKUP_FAILURE_TESTS(TYPE, PREFIX, MALFORMED)                                                           \
    MVTF_TEST(PREFIX##_lookup_rejects_transport_failure)                                                               \
    {                                                                                                                  \
        LyricSourceFactory<TYPE> factory;                                                                              \
        source_tests::lookup_rejects_bad_response(factory.get_static_instance(), true, MALFORMED, mvtf_error_count);   \
    }                                                                                                                  \
    MVTF_TEST(PREFIX##_lookup_rejects_malformed_response)                                                              \
    {                                                                                                                  \
        LyricSourceFactory<TYPE> factory;                                                                              \
        source_tests::lookup_rejects_bad_response(factory.get_static_instance(), false, MALFORMED, mvtf_error_count);  \
    }
#endif
