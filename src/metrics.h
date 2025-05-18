#pragma once

namespace metrics
{
    // 'Log' to internal tracking that a feature was used, so we can later report metrics
    // on when last the feature was used, when it was first used, how often, etc
    void log_used_bulk_search();
    void log_used_manual_search();
    void log_used_lyric_editor();
    void log_used_auto_edit();
    void log_used_mark_instrumental();
    void log_used_show_lyrics();
    void log_used_external_window();
    void log_used_manual_scroll();
    void log_used_lyric_upload();
    void log_searched_for_lyrics_for_a_remote_track();
    void log_hidden_search();
}
