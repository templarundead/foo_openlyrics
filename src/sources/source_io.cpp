#include "stdafx.h"

#include "preferences.h"
#include "source_io.h"

#if MVTF_TESTS_ENABLED
#pragma warning(push, 0)
#include "foobar2000/helpers/readers.h"
#pragma warning(pop)
#include <stdexcept>

namespace source_io
{
    static thread_local FixtureIO* active_fixture = nullptr;
    FixtureIO* fixture()
    {
        return active_fixture;
    }
    FixtureIO::FixtureIO()
        : m_previous(active_fixture)
    {
        active_fixture = this;
    }
    FixtureIO::~FixtureIO()
    {
        active_fixture = m_previous;
    }

    Response FixtureIO::fetch(Request request, abort_callback& abort)
    {
        abort.check();
        requests.push_back(std::move(request));
        if(next_response >= responses.size()) throw std::runtime_error("Unexpected fixture request");
        return responses[next_response++];
    }

    class FixtureRequest : public http_request_post_v2
    {
    public:
        explicit FixtureRequest(const char* method)
        {
            m_request.method = method;
        }
        void add_header(const char* line) override
        {
            m_request.headers.emplace_back(line);
        }
        void add_post_data(const char*, const void*, t_size, const char*, const char*) override {}
        void set_post_data(const void*, size_t, const char*) override {}
        file_ptr run(const char* url, abort_callback& abort) override
        {
            return read(url, abort, false);
        }
        file_ptr run_ex(const char* url, abort_callback& abort) override
        {
            return read(url, abort, true);
        }

    private:
        file_ptr read(const char* url, abort_callback& abort, bool allow_error_status)
        {
            m_request.url = url;
            const Response response = fixture()->fetch(m_request, abort);
            if(!response.transport_success
               || (!allow_error_status && (response.status < 200 || response.status >= 300)))
                throw std::runtime_error("Fixture HTTP failure");
            return fb2k::service_new<reader_membuffer_simple>(response.body.data(), response.body.size());
        }
        Request m_request;
    };
}
#endif

http_request::ptr source_io::create_request(const char* method)
{
#if MVTF_TESTS_ENABLED
    if(fixture()) return fb2k::service_new<FixtureRequest>(method);
    // Never fall through to real networking in the standalone DLL test runner.
    if(mvtf_is_running_tests()) throw std::runtime_error("Missing HTTP fixture");
#endif
    return http_client::get()->create_request(method);
}

bool source_io::exists(const char* path, abort_callback& abort)
{
#if MVTF_TESTS_ENABLED
    if(auto* io = fixture())
    {
        abort.check();
        return std::any_of(io->files.begin(), io->files.end(), [path](const auto& file) { return file.first == path; });
    }
#endif
    return filesystem::g_exists(path, abort);
}

file_ptr source_io::open_read(const char* path, abort_callback& abort)
{
#if MVTF_TESTS_ENABLED
    if(auto* io = fixture())
    {
        abort.check();
        for(const auto& file : io->files)
            if(file.first == path)
                return fb2k::service_new<reader_membuffer_simple>(file.second.data(), file.second.size());
        throw std::runtime_error("Fixture file not found");
    }
#endif
    file_ptr file;
    filesystem::g_open_read(file, path, abort);
    return file;
}

std::string source_io::local_search_path(metadb_handle_ptr track, const metadb_v2_rec_t& info)
{
#if MVTF_TESTS_ENABLED
    if(auto* io = fixture()) return io->path_prefix;
#endif
    return preferences::saving::filename(track, info);
}

std::string_view source_io::musixmatch_token()
{
#if MVTF_TESTS_ENABLED
    if(auto* io = fixture()) return io->token;
#endif
    return preferences::searching::musixmatch_api_key();
}
