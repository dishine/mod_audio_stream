#ifndef AUDIO_STREAMER_GLUE_H
#define AUDIO_STREAMER_GLUE_H
#include "mod_audio_stream.h"



int validate_ws_uri(const char* url, char *wsUri);
switch_status_t is_valid_utf8(const char *str);
switch_status_t stream_session_send_text(switch_core_session_t *session, char* text);
switch_status_t stream_session_pauseresume(switch_core_session_t *session, int pause);
switch_status_t stream_session_init(switch_core_session_t *session, responseHandler_t responseHandler,
    uint32_t samples_per_second, char *wsUri, char *audioType, int sampling, int channels, char* metadata, void **ppUserData);
switch_bool_t stream_frame(switch_media_bug_t *bug);
switch_bool_t stream_frameread(switch_media_bug_t *bug);
switch_bool_t stream_framewrite(switch_media_bug_t *bug);
switch_status_t stream_session_cleanup(switch_core_session_t *session, char* text, int channelIsClosing);
switch_bool_t stream_write_frame(switch_media_bug_t *bug);

#endif //AUDIO_STREAMER_GLUE_H
