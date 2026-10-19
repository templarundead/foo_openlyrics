#pragma once

#include "mvtf/mvtf.h"
#include "stdafx.h"

// Thin seams around host-provided I/O. In production these delegate unchanged to
// the SDK. Tests substitute bytes here, exercising the real source fetch/parsing code.
namespace source_io
{
    http_request::ptr create_request(const char* method);
    bool exists(const char* path, abort_callback& abort);
    file_ptr open_read(const char* path, abort_callback& abort);
    std::string local_search_path(metadb_handle_ptr track, const metadb_v2_rec_t& info);
    std::string_view musixmatch_token();

#if MVTF_TESTS_ENABLED
    struct Response
    {
        std::string body;
        long status = 200;
        bool transport_success = true;
    };
    struct Request
    {
        std::string method;
        std::string url;
        std::vector<std::string> headers;
    };
    struct FixtureIO
    {
        FixtureIO();
        ~FixtureIO();
        FixtureIO(const FixtureIO&) = delete;
        FixtureIO& operator=(const FixtureIO&) = delete;

        std::vector<Response> responses;
        std::vector<Request> requests;
        size_t next_response = 0;
        std::string path_prefix = "fixture/track";
        std::string token = "fixture-token-not-a-real-credential";
        std::vector<std::pair<std::string, std::string>> files;

        Response fetch(Request request, abort_callback& abort);

    private:
        FixtureIO* m_previous;
    };
    FixtureIO* fixture();
#endif
}
