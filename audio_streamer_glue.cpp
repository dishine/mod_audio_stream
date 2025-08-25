#include <string>
#include <cstring>
#include "mod_audio_stream.h"
//#include <ixwebsocket/IXWebSocket.h>
#include "WebSocketClient.h"
#include <speex/speex_resampler.h>
#include <vector>
#include <switch_json.h>
#include <fstream>
#include <switch_buffer.h>
#include <unordered_map>
#include <unordered_set>
#include "base64.h"


#define FRAME_SIZE_8000  320 /* 1000x0.02 (20ms)= 160 x(16bit= 2 bytes) 320 frame size*/


class AudioStreamer {
public:

    AudioStreamer(const char* uuid, const char* wsUri, responseHandler_t callback, int deflate, int heart_beat,
                    bool suppressLog, const char* extra_headers, bool no_reconnect,
                    const char* tls_cafile, const char* tls_keyfile, const char* tls_certfile,
                    bool tls_disable_hostname_validation): m_sessionId(uuid), m_notify(callback),
                    m_suppress_log(suppressLog), m_extra_headers(extra_headers), m_playFile(0){

        WebSocketHeaders hdrs;
        WebSocketTLSOptions tls;

        if (m_extra_headers) {
            cJSON *headers_json = cJSON_Parse(m_extra_headers);
            if (headers_json) {
                cJSON *iterator = headers_json->child;
                while (iterator) {
                    if (iterator->type == cJSON_String && iterator->valuestring != nullptr) {
                        hdrs.set(iterator->string, iterator->valuestring);
                    }
                    iterator = iterator->next;
                }
                cJSON_Delete(headers_json);
            }
        }

        client.setUrl(wsUri);

        // Setup eventual TLS options.
        // tls_cafile may hold the special values
        // NONE, which disables validation and SYSTEM which uses
        // the system CAs bundle
        if (tls_cafile) {
            tls.caFile = tls_cafile;
        }

        if (tls_keyfile) {
            tls.keyFile = tls_keyfile;
        }

        if (tls_certfile) {
            tls.certFile = tls_certfile;
        }

        tls.disableHostnameValidation = tls_disable_hostname_validation;
        client.setTLSOptions(tls);

        // Optional heart beat, sent every xx seconds when there is not any traffic
        // to make sure that load balancers do not kill an idle connection.
        if(heart_beat)
            client.setPingInterval(heart_beat);

        // Per message deflate connection is enabled by default. You can tweak its parameters or disable it
        if(deflate)
            client.enableCompression(false);

        // Set extra headers if any
        if(!hdrs.empty())
            client.setHeaders(hdrs);

        // Setup a callback to be fired when a message or an event (open, close, error) is received
        client.setMessageCallback([this](const std::string& message) {
            eventCallback(MESSAGE, message.c_str());
        });

        client.setOpenCallback([this]() {
            cJSON *root;
            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "connected");
            char *json_str = cJSON_PrintUnformatted(root);
            eventCallback(CONNECT_SUCCESS, json_str);
            cJSON_Delete(root);
            switch_safe_free(json_str);
        });

        client.setErrorCallback([this](int code, const std::string &msg) {
            cJSON *root, *message;
            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "error");
            message = cJSON_CreateObject();
            cJSON_AddNumberToObject(message, "code", code);
            cJSON_AddStringToObject(message, "error", msg.c_str());
            cJSON_AddItemToObject(root, "message", message);

            char *json_str = cJSON_PrintUnformatted(root);

            eventCallback(CONNECT_ERROR, json_str);

            cJSON_Delete(root);
            switch_safe_free(json_str);
        });

        client.setCloseCallback([this](int code, const std::string &reason) {
            cJSON *root, *message;
            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "disconnected");
            message = cJSON_CreateObject();
            cJSON_AddNumberToObject(message, "code", code);
            cJSON_AddStringToObject(message, "reason", reason.c_str());
            cJSON_AddItemToObject(root, "message", message);
            char *json_str = cJSON_PrintUnformatted(root);

            eventCallback(CONNECTION_DROPPED, json_str);

            cJSON_Delete(root);
            switch_safe_free(json_str);
        });

        // Now that our callback is setup, we can start our background thread and receive messages
        client.connect();
    }

    switch_media_bug_t *get_media_bug(switch_core_session_t *session) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        if(!channel) {
            return nullptr;
        }
        auto *bug = (switch_media_bug_t *) switch_channel_get_private(channel, MY_BUG_NAME);
        return bug;
    }

    inline void media_bug_close(switch_core_session_t *session) {
        auto *bug = get_media_bug(session);
        if(bug) {
            auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
            tech_pvt->close_requested = 1;
            switch_core_media_bug_close(&bug, SWITCH_FALSE);
        }
    }

    inline void send_initial_metadata(switch_core_session_t *session) {
        auto *bug = get_media_bug(session);
        if(bug) {
            auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
            if(tech_pvt && strlen(tech_pvt->initialMetadata) > 0) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                                          "sending initial metadata %s\n", tech_pvt->initialMetadata);
                writeText(tech_pvt->initialMetadata);
            }
        }
    }

    void eventCallback(notifyEvent_t event, const char* message) {
        switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
        if(psession) {
            switch (event) {
                case CONNECT_SUCCESS:
                    send_initial_metadata(psession);
                    m_notify(psession, EVENT_CONNECT, message);
                    break;
                case CONNECTION_DROPPED:
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_INFO, "connection closed\n");
                    m_notify(psession, EVENT_DISCONNECT, message);
                    break;
                case CONNECT_ERROR:
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_INFO, "connection error\n");
                    m_notify(psession, EVENT_ERROR, message);

                    media_bug_close(psession);

                    break;
                case MESSAGE:
                    std::string msg(message);
                    if(processMessage(psession, msg) != SWITCH_TRUE) {
                        m_notify(psession, EVENT_JSON, msg.c_str());
                    }
                    if(!m_suppress_log)
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "response: %s\n", msg.c_str());
                    break;
            }
            switch_core_session_rwunlock(psession);
        }
    }

    switch_bool_t processMessage(switch_core_session_t* session, std::string& message) {
        cJSON* json = cJSON_Parse(message.c_str());
        switch_bool_t status = SWITCH_FALSE;
        if (!json) {
            return status;
        }
        const char* jsType = cJSON_GetObjectCstr(json, "type");
        const char* dialogId = cJSON_GetObjectCstr(json, "dialogId");
        if(jsType && strcmp(jsType, "streamAudio") == 0) {
            cJSON* jsonData = cJSON_GetObjectItem(json, "data");
            if(jsonData) {
                cJSON* jsonFile = nullptr;
                cJSON* jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioData");
                const char* jsAudioDataType = cJSON_GetObjectCstr(jsonData, "audioDataType");
                std::string fileType;
                int sampleRate;
                if (0 == strcmp(jsAudioDataType, "raw")) {
                    cJSON* jsonSampleRate = cJSON_GetObjectItem(jsonData, "sampleRate");
                    sampleRate = jsonSampleRate && jsonSampleRate->valueint ? jsonSampleRate->valueint : 0;
                    std::unordered_map<int, const char*> sampleRateMap = {
                            {8000, ".r8"},
                            {16000, ".r16"},
                            {24000, ".r24"},
                            {32000, ".r32"},
                            {48000, ".r48"},
                            {64000, ".r64"}
                    };
                    auto it = sampleRateMap.find(sampleRate);
                    fileType = (it != sampleRateMap.end()) ? it->second : "";
                } else if (0 == strcmp(jsAudioDataType, "wav")) {
                    fileType = ".wav";
                } else if (0 == strcmp(jsAudioDataType, "mp3")) {
                    fileType = ".mp3";
                } else if (0 == strcmp(jsAudioDataType, "ogg")) {
                    fileType = ".ogg";
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - unsupported audio type: %s\n",
                                      m_sessionId.c_str(), jsAudioDataType);
                }

                if(jsonAudio && jsonAudio->valuestring != nullptr && !fileType.empty()) {
                    char filePath[256];
                    std::string rawAudio;
                    try {
                        rawAudio = base64_decode(jsonAudio->valuestring);
                    } catch (const std::exception& e) {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - base64 decode error: %s\n",
                                          m_sessionId.c_str(), e.what());
                        cJSON_Delete(jsonAudio); cJSON_Delete(json);
                        return status;
                    }
                    switch_snprintf(filePath, 256, "%s%s%s_%d.tmp%s", SWITCH_GLOBAL_dirs.temp_dir,
                                    SWITCH_PATH_SEPARATOR, m_sessionId.c_str(), m_playFile++, fileType.c_str());
                    std::ofstream fstream(filePath, std::ofstream::binary);
                    fstream << rawAudio;
                    fstream.close();
                    m_Files.insert(filePath);
                    jsonFile = cJSON_CreateString(filePath);
                    cJSON_AddItemToObject(jsonData, "file", jsonFile);
                }

                if(jsonFile) {
                    char *jsonString = cJSON_PrintUnformatted(jsonData);
                    m_notify(session, EVENT_PLAY, jsonString);
                    message.assign(jsonString);
                    free(jsonString);
                    status = SWITCH_TRUE;
                }
                if (jsonAudio)
                    cJSON_Delete(jsonAudio);
            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - no data in streamAudio\n", m_sessionId.c_str());
            }
        } else if (jsType && strcmp(jsType, "inlineAudio") == 0) {
            cJSON* jsonData = cJSON_GetObjectItem(json, "data");
            if (jsonData) {

               const char* jsonStatus = cJSON_GetObjectCstr(jsonData, "status");

               // 如果 status 是 "start" 或 "finish"，直接返回成功
               if (jsonStatus && (strcmp(jsonStatus, "start") == 0 || strcmp(jsonStatus, "finish") == 0)) {
                   notifyTaskStatus(dialogId, jsonStatus);
                   status = SWITCH_TRUE;
               } else {
                   cJSON* jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioData");
                   int sampleRate = 8000;
                   int channels = 1;

                   cJSON* jsonSampleRate = cJSON_GetObjectItem(jsonData, "sampleRate");
                   cJSON* jsonChannels = cJSON_GetObjectItem(jsonData, "channels");

                   if (jsonSampleRate) sampleRate = jsonSampleRate->valueint;
                   if (jsonChannels) channels = jsonChannels->valueint;

                   if (jsonAudio && jsonAudio->valuestring != nullptr) {
                      std::string rawAudio;
                      try {
                          rawAudio = base64_decode(jsonAudio->valuestring);
                      } catch (const std::exception& e) {
                          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                 "(%s) inlineAudio decode error: %s\n", m_sessionId.c_str(), e.what());
                          cJSON_Delete(json);
                          return status;
                      }
//                       //--- 新增逻辑: 将音频数据写入缓冲区 (生产者) ---
//                      auto *bug = get_media_bug(session);
//                      if (bug) {
//                          auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
//                          if (tech_pvt && tech_pvt->tts_buffer) {
//                             switch_buffer_write(tech_pvt->tts_buffer, rawAudio.data(), rawAudio.size());
//                             status = SWITCH_TRUE;
//                          }
//                      }
//                      if (status == SWITCH_FALSE) {
//                          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
//                                 "(%s) Could not get media bug to write audio data\\n", m_sessionId.c_str());
//                      }
//                       --- 新增逻辑结束 ---
                      notifyTaskStatus(dialogId, jsonStatus);
                      inject_audio_data_v2(session, (const uint8_t*)rawAudio.data(), rawAudio.size(), sampleRate, channels);
                      char *jsonString = cJSON_PrintUnformatted(jsonData);
                      message.assign(jsonString);
                      free(jsonString);
                      status = SWITCH_TRUE;
                   }
                   if (jsonAudio) cJSON_Delete(jsonAudio);
               }
            }
        } else if (jsType && strcmp(jsType, "playFile") == 0) {
           cJSON* jsonData = cJSON_GetObjectItem(json, "data");
           if (jsonData) {
              const char* filePath = cJSON_GetObjectCstr(jsonData, "file");
              if (filePath && strlen(filePath) > 0) {
                notifyTaskStatus(dialogId, "start");
                notifyTaskStatus(dialogId, "loading");
                playAudioFile(session, filePath);
                notifyTaskStatus(dialogId, "finish");
              } else {
                 switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                    "(%s) playFile: 'file' field is missing\n", m_sessionId.c_str());
              }
           }
        } else if(jsType && strcmp(jsType, "playStreamAudio") == 0) {
            cJSON* jsonData = cJSON_GetObjectItem(json, "data");
            if(jsonData) {
                cJSON* jsonFile = nullptr;
                cJSON* jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioData");
                const char* jsAudioDataType = cJSON_GetObjectCstr(jsonData, "audioDataType");
                std::string fileType;
                int sampleRate;
                if (0 == strcmp(jsAudioDataType, "raw")) {
                    cJSON* jsonSampleRate = cJSON_GetObjectItem(jsonData, "sampleRate");
                    sampleRate = jsonSampleRate && jsonSampleRate->valueint ? jsonSampleRate->valueint : 0;
                    std::unordered_map<int, const char*> sampleRateMap = {
                            {8000, ".r8"},
                            {16000, ".r16"},
                            {24000, ".r24"},
                            {32000, ".r32"},
                            {48000, ".r48"},
                            {64000, ".r64"}
                    };
                    auto it = sampleRateMap.find(sampleRate);
                    fileType = (it != sampleRateMap.end()) ? it->second : "";
                } else if (0 == strcmp(jsAudioDataType, "wav")) {
                    fileType = ".wav";
                } else if (0 == strcmp(jsAudioDataType, "mp3")) {
                    fileType = ".mp3";
                } else if (0 == strcmp(jsAudioDataType, "ogg")) {
                    fileType = ".ogg";
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - unsupported audio type: %s\n",
                                      m_sessionId.c_str(), jsAudioDataType);
                }

                if(jsonAudio && jsonAudio->valuestring != nullptr && !fileType.empty()) {
                    char filePath[256];
                    std::string rawAudio;
                    try {
                        rawAudio = base64_decode(jsonAudio->valuestring);
                    } catch (const std::exception& e) {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - base64 decode error: %s\n",
                                          m_sessionId.c_str(), e.what());
                        cJSON_Delete(jsonAudio); cJSON_Delete(json);
                        return status;
                    }
                    switch_snprintf(filePath, 256, "%s%s%s_%d.tmp%s", SWITCH_GLOBAL_dirs.temp_dir,
                                    SWITCH_PATH_SEPARATOR, m_sessionId.c_str(), m_playFile++, fileType.c_str());
                    std::ofstream fstream(filePath, std::ofstream::binary);
                    fstream << rawAudio;
                    fstream.close();
                    m_Files.insert(filePath);
                    jsonFile = cJSON_CreateString(filePath);
                    cJSON_AddItemToObject(jsonData, "file", jsonFile);
                }
                if(jsonFile) {
                    char *jsonString = cJSON_PrintUnformatted(jsonData);
                    const char* filePath = cJSON_GetObjectCstr(jsonData, "file");
                    if (filePath && strlen(filePath) > 0) {
                       notifyTaskStatus(dialogId, "start");
                       notifyTaskStatus(dialogId, "loading");
                       playAudioFile(session, filePath);
                       notifyTaskStatus(dialogId, "finish");
                    } else {
                       switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                          "(%s) playFile: 'file' field is missing\n", m_sessionId.c_str());
                    }
                    message.assign(jsonString);
                    free(jsonString);
                    status = SWITCH_TRUE;
                }
                if (jsonAudio)
                    cJSON_Delete(jsonAudio);
            }else {
               switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - no data in streamAudio\n", m_sessionId.c_str());
           }
        }

        cJSON_Delete(json);
        return status;
    }

    ~AudioStreamer()= default;

    void disconnect() {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "disconnecting...\n");
        client.disconnect();
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "disconnect success\n");
    }

    bool isConnected() {
        return client.isConnected();
    }

    void writeBinary(uint8_t* buffer, size_t len) {
        if(!this->isConnected()) return;
        client.sendBinary(buffer, len);
    }

    void writeText(const char* text) {
        if(!this->isConnected()) return;
        client.sendMessage(text, strlen(text));
        // 打印发送的数据
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending message to WebSocket: %s\n", text);
    }

    void writeAudio(const char* text) {
        if(!this->isConnected()) return;
        client.sendMessage(text, strlen(text));
    }

    void sendDTMFEvent(const char* digit, const char* duration_ms) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "dtmf");
        cJSON_AddStringToObject(json, "digit", digit);
        cJSON_AddStringToObject(json, "duration", duration_ms);

        char *json_str = cJSON_PrintUnformatted(json);
        writeText(json_str);  // 使用现有的 writeText 方法发送 JSON

        cJSON_Delete(json);
        free(json_str);
    }

    void notifyTaskStatus(const char* dialogId, const char* status) {
        // 创建一个 JSON 对象
        cJSON *jsonMsg = cJSON_CreateObject();
        if (jsonMsg == nullptr) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create JSON object for task notification\n");
            return;
        }

        // 添加 dialogId 和 status 字段到 JSON 对象中
        cJSON_AddStringToObject(jsonMsg, "dialogId", dialogId);
        cJSON_AddStringToObject(jsonMsg, "status", status);
        cJSON_AddStringToObject(jsonMsg, "type", "playback");

        // 将 JSON 对象转换为字符串
        char *jsonStr = cJSON_PrintUnformatted(jsonMsg);
        if (jsonStr == nullptr) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to convert JSON object to string for task notification\n");
            cJSON_Delete(jsonMsg);
            return;
        }

        // 使用 writeText 函数发送 JSON 消息
        writeText(jsonStr);

        // 释放资源
        free(jsonStr);
        cJSON_Delete(jsonMsg);
    }

    void deleteFiles() {
        if(m_playFile >0) {
            for (const auto &fileName: m_Files) {
                remove(fileName.c_str());
            }
        }
    }

    void inject_audio_data(switch_core_session_t *session, const uint8_t* pcm_data, size_t len, int sample_rate, int channels) {
        if (!session || !pcm_data || len == 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Invalid input params\n");
            return;
        }

        switch_channel_t *channel = switch_core_session_get_channel(session);
        if (!channel || !switch_channel_ready(channel)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Channel not ready\n");
            return;
        }

        // 获取当前通话的 write codec（通话使用的编码格式，如 PCMU）
        switch_codec_t *write_codec = switch_core_session_get_write_codec(session);
        if (!write_codec || !switch_core_codec_ready(write_codec)) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                              "Write codec not ready or invalid\n");
            return;
        }

        int target_rate = write_codec->implementation->actual_samples_per_second;
        int target_channels = write_codec->implementation->number_of_channels;
        int samples_per_frame = write_codec->implementation->samples_per_packet;
        int bytes_per_sample = 2;  // 16-bit PCM

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                          "Write codec: %s, %d Hz, %d channels\n",
                          write_codec->implementation->iananame,
                          write_codec->implementation->actual_samples_per_second,
                          write_codec->implementation->number_of_channels);

        size_t pcm_frame_bytes = samples_per_frame * bytes_per_sample;
        size_t offset = 0;

        while (offset + pcm_frame_bytes <= len) {
            const uint8_t* input_frame = pcm_data + offset;

            uint8_t encoded_data[SWITCH_RECOMMENDED_BUFFER_SIZE];
            uint32_t encoded_len = sizeof(encoded_data);
            uint32_t samples = samples_per_frame;
            unsigned int flags = 0;

            // 编码 PCM 到 session write_codec（通常为 PCMU）
            switch_status_t status = switch_core_codec_encode(
                write_codec,
                NULL,
                (void*)input_frame,
                pcm_frame_bytes,
                target_rate,
                encoded_data,
                &encoded_len,
                &samples,
                &flags
            );

            if (status != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                  "Encode failed at offset %zu, status: %d\n", offset, status);
                break;
            }

            // 分配编码后数据的内存（必须由 session 的 pool 分配）
            uint8_t* frame_data = (uint8_t*)switch_core_session_alloc(session, encoded_len);
            memcpy(frame_data, encoded_data, encoded_len);

            switch_frame_t frame = {0};
            frame.data = frame_data;
            frame.datalen = encoded_len;
            frame.samples = samples;
            frame.rate = target_rate;
            frame.codec = write_codec;
            frame.payload = write_codec->implementation->ianacode;
            frame.channels = target_channels;

            if (switch_core_session_write_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0) != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                  "Write frame failed at offset %zu\n", offset);
                break;
            }

            offset += pcm_frame_bytes;
        }

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                          "Audio injection finished. Total injected bytes: %zu\n", offset);
    }

    void playAudioFile(switch_core_session_t *session, const char *filePath) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        if (channel) {
            switch_status_t s = switch_ivr_play_file(session, NULL, filePath, NULL);
            if (s == SWITCH_STATUS_SUCCESS) {
                return;
            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                  "(%s) playFile failed to play: %s\n", m_sessionId.c_str(), filePath);
            }
        }
        return ;
    }

    void inject_audio_data_v2(switch_core_session_t *session,
                             const uint8_t* pcm_data,
                             size_t len,
                             int sample_rate,
                             int channels) {
        // 1. 参数有效性检查
        if (!session || !pcm_data || len == 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                            "Invalid input parameters: session=%p, data=%p, len=%zu\n",
                            session, pcm_data, len);
            return;
        }

        switch_channel_t *channel = switch_core_session_get_channel(session);
        if (!channel || !switch_channel_ready(channel)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                             "Channel not ready or invalid\n");
            return;
        }

        // 2. 获取目标编解码器参数
        switch_codec_t *write_codec = switch_core_session_get_write_codec(session);
        if (!write_codec || !switch_core_codec_ready(write_codec)) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                            "Write codec not ready or invalid\n");
            return;
        }

        const int target_rate = write_codec->implementation->actual_samples_per_second;
        const int target_channels = write_codec->implementation->number_of_channels;
        const int samples_per_frame = write_codec->implementation->samples_per_packet;
        const int bytes_per_sample = 2; // 16-bit PCM
        const size_t pcm_frame_bytes = samples_per_frame * bytes_per_sample * channels;


        // 3. 采样率转换处理（修复点1：确保转换后的数据被使用）
        std::vector<int16_t> resampled_buffer;
        const uint8_t* audio_to_inject = pcm_data;
        size_t audio_length = len;
        int effective_sample_rate = sample_rate;

        if (sample_rate != target_rate) {
            int err = 0;
            SpeexResamplerState *resampler = speex_resampler_init(
                channels,
                sample_rate,
                target_rate,
                SWITCH_RESAMPLE_QUALITY,
                &err);

            if (err == RESAMPLER_ERR_SUCCESS && resampler) {
                spx_uint32_t in_len = len / (bytes_per_sample * channels);
                spx_uint32_t out_len = (in_len * target_rate / sample_rate) + 1;
                resampled_buffer.resize(out_len * channels);

                speex_resampler_process_interleaved_int(
                    resampler,
                    (const spx_int16_t*)pcm_data,
                    &in_len,
                    resampled_buffer.data(),
                    &out_len);

                // 使用转换后的数据
                audio_to_inject = (const uint8_t*)resampled_buffer.data();
                audio_length = out_len * bytes_per_sample * channels;
                effective_sample_rate = target_rate;

                speex_resampler_destroy(resampler);

                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                                "Resampled %u->%u samples\n", in_len, out_len);
            }
        }

        // 4. 音频数据注入
        size_t offset = 0;
        // 添加间隔检测


//        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
//                        "Injecting audio: input %dHz/%dch, target %dHz/%dch, frame %zu bytes\n",
//                        sample_rate, channels, target_rate, target_channels, pcm_frame_bytes);

        while (offset < audio_length) {
            // 4.1 处理不完整帧
            const size_t remaining = audio_length - offset;
            const size_t this_frame_size = (remaining < pcm_frame_bytes) ? remaining : pcm_frame_bytes;
            const uint8_t* frame_data = audio_to_inject + offset;

            // 4.2 编码音频帧
            uint8_t encoded_data[SWITCH_RECOMMENDED_BUFFER_SIZE];
            uint32_t encoded_len = sizeof(encoded_data);
            uint32_t samples = samples_per_frame;
            unsigned int flags = 0;

            switch_status_t status = switch_core_codec_encode(
                write_codec,
                nullptr,
                const_cast<void*>(reinterpret_cast<const void*>(frame_data)),
                this_frame_size,
                effective_sample_rate,
                encoded_data,
                &encoded_len,
                &samples,
                &flags
            );

            if (status != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                "Encode failed at offset %zu/%zu, status: %d\n",
                                offset, audio_length, status);
                break;
            }

            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                "Encoded frame: samples=%u, rate=%d, datalen=%d\n",
                samples, target_rate, encoded_len);
            // 4.3 准备并写入帧
            switch_frame_t frame = {0};
            uint8_t* session_frame_data = static_cast<uint8_t*>(
                switch_core_session_alloc(session, encoded_len));

            if (!session_frame_data) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                "Failed to allocate frame data\n");
                break;
            }

            memcpy(session_frame_data, encoded_data, encoded_len);

            frame.data = session_frame_data;
            frame.datalen = encoded_len;
            frame.samples = samples;
            frame.rate = target_rate;
            frame.channels = target_channels;
            frame.codec = write_codec;
            frame.payload = write_codec->implementation->ianacode;
            // 设置时间戳和序号
            frame.seq = m_seq++;
//            frame.timestamp = m_timestamp; //fixme 如何正确设置每一帧音频的时间戳
            m_timestamp += samples;  // 增加样本数，如每帧160

            if (switch_core_session_write_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0)
                != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                "Failed to write frame at offset %zu\n", offset);
                break;
            }

            offset += this_frame_size;
        }

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                         "Injected %zu/%zu bytes of audio data\n", offset, audio_length);
    }

private:
    std::string m_sessionId;
    responseHandler_t m_notify;
    WebSocketClient client;
    bool m_suppress_log;
    const char* m_extra_headers;
    int m_playFile;
    std::unordered_set<std::string> m_Files;
    // 用于追踪帧的序号和时间戳
    uint32_t m_seq = 0;
    uint32_t m_timestamp = 0;
};


namespace {

    switch_status_t stream_data_init(private_t *tech_pvt, switch_core_session_t *session, char *wsUri, char *audio_type,
                                     uint32_t sampling, int desiredSampling, int channels, char *metadata, responseHandler_t responseHandler,
                                     int deflate, int heart_beat, bool suppressLog, int rtp_packets, const char* extra_headers,
                                     bool no_reconnect, const char *tls_cafile, const char *tls_keyfile,
                                     const char *tls_certfile, bool tls_disable_hostname_validation)
    {
        int err; //speex

        switch_memory_pool_t *pool = switch_core_session_get_pool(session);

        memset(tech_pvt, 0, sizeof(private_t));

        strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
        strncpy(tech_pvt->ws_uri, wsUri, MAX_WS_URI);
        strncpy(tech_pvt->audio_type, audio_type, MAX_AUDIO_TYPE);
        tech_pvt->sampling = desiredSampling;
        tech_pvt->responseHandler = responseHandler;
        tech_pvt->rtp_packets = rtp_packets;
        tech_pvt->channels = channels;
        tech_pvt->audio_paused = 0;

        if (metadata) strncpy(tech_pvt->initialMetadata, metadata, MAX_METADATA_LEN);

        //size_t buflen = (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PERIOD * BUFFERED_SEC);
        const size_t buflen = (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * rtp_packets);

        auto* as = new AudioStreamer(tech_pvt->sessionId, wsUri, responseHandler, deflate, heart_beat,
                                        suppressLog, extra_headers, no_reconnect,
                                        tls_cafile, tls_keyfile, tls_certfile, tls_disable_hostname_validation);

        tech_pvt->pAudioStreamer = static_cast<void *>(as);

        switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, pool);

        if (switch_buffer_create(pool, &tech_pvt->sbuffer, buflen) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "%s: Error creating switch buffer.\n", tech_pvt->sessionId);
            return SWITCH_STATUS_FALSE;
        }

          // 创建一个可以缓存约4秒音频的缓冲区
        const size_t tts_buffer_len = (desiredSampling / 1000) * 2 * channels * 4000;
        if (switch_buffer_create(pool, &tech_pvt->tts_buffer, tts_buffer_len) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "%s: Error creating tts_buffer for inbound audio.\\n", tech_pvt->sessionId);
            return SWITCH_STATUS_FALSE;
        }

        if (desiredSampling != sampling) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%s) resampling from %u to %u\n", tech_pvt->sessionId, sampling, desiredSampling);
            tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
            if (0 != err) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
                return SWITCH_STATUS_FALSE;
            }
        }
        else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%s) no resampling needed for this call\n", tech_pvt->sessionId);
        }

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%s) stream_data_init\n", tech_pvt->sessionId);

        return SWITCH_STATUS_SUCCESS;
    }

    void destroy_tech_pvt(private_t* tech_pvt) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s destroy_tech_pvt\n", tech_pvt->sessionId);
        if (tech_pvt->resampler) {
            speex_resampler_destroy(tech_pvt->resampler);
            tech_pvt->resampler = nullptr;
        }
        if (tech_pvt->mutex) {
            switch_mutex_destroy(tech_pvt->mutex);
            tech_pvt->mutex = nullptr;
        }
        if (tech_pvt->pAudioStreamer) {
            auto* as = (AudioStreamer *) tech_pvt->pAudioStreamer;
            delete as;
            tech_pvt->pAudioStreamer = nullptr;
        }
    }

    void finish(private_t* tech_pvt) {
        std::shared_ptr<AudioStreamer> aStreamer;
        aStreamer.reset((AudioStreamer *)tech_pvt->pAudioStreamer);
        tech_pvt->pAudioStreamer = nullptr;

        std::thread t([aStreamer]{
            aStreamer->disconnect();
        });
        t.detach();
    }

}

extern "C" {
    int validate_ws_uri(const char* url, char* wsUri) {
        const char* scheme = nullptr;
        const char* hostStart = nullptr;
        const char* hostEnd = nullptr;
        const char* portStart = nullptr;

        // Check scheme
        if (strncmp(url, "ws://", 5) == 0) {
            scheme = "ws";
            hostStart = url + 5;
        } else if (strncmp(url, "wss://", 6) == 0) {
            scheme = "wss";
            hostStart = url + 6;
        } else {
            return 0;
        }

        // Find host end or port start
        hostEnd = hostStart;
        while (*hostEnd && *hostEnd != ':' && *hostEnd != '/') {
            if (!std::isalnum(*hostEnd) && *hostEnd != '-' && *hostEnd != '.') {
                return 0;
            }
            ++hostEnd;
        }

        // Check if host is empty
        if (hostStart == hostEnd) {
            return 0;
        }

        // Check for port
        if (*hostEnd == ':') {
            portStart = hostEnd + 1;
            while (*portStart && *portStart != '/') {
                if (!std::isdigit(*portStart)) {
                    return 0;
                }
                ++portStart;
            }
        }

        // Copy valid URI to wsUri
        std::strncpy(wsUri, url, MAX_WS_URI);
        return 1;
    }

    switch_status_t is_valid_utf8(const char *str) {
        switch_status_t status = SWITCH_STATUS_FALSE;
        while (*str) {
            if ((*str & 0x80) == 0x00) {
                // 1-byte character
                str++;
            } else if ((*str & 0xE0) == 0xC0) {
                // 2-byte character
                if ((str[1] & 0xC0) != 0x80) {
                    return status;
                }
                str += 2;
            } else if ((*str & 0xF0) == 0xE0) {
                // 3-byte character
                if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80) {
                    return status;
                }
                str += 3;
            } else if ((*str & 0xF8) == 0xF0) {
                // 4-byte character
                if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80 || (str[3] & 0xC0) != 0x80) {
                    return status;
                }
                str += 4;
            } else {
                // invalid character
                return status;
            }
        }
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_send_text(switch_core_session_t *session, char* text) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        auto *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
        if (!bug) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "stream_session_send_text failed because no bug\n");
            return SWITCH_STATUS_FALSE;
        }
        auto *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);

        if (!tech_pvt) return SWITCH_STATUS_FALSE;
        auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);
        if (pAudioStreamer && text) pAudioStreamer->writeText(text);

        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_pauseresume(switch_core_session_t *session, int pause) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        auto *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
        if (!bug) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "stream_session_pauseresume failed because no bug\n");
            return SWITCH_STATUS_FALSE;
        }
        auto *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);

        if (!tech_pvt) return SWITCH_STATUS_FALSE;

        switch_core_media_bug_flush(bug);
        tech_pvt->audio_paused = pause;
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_init(switch_core_session_t *session,
                                        responseHandler_t responseHandler,
                                        uint32_t samples_per_second,
                                        char *wsUri,
                                        char *audioType,
                                        int sampling,
                                        int channels,
                                        char* metadata,
                                        void **ppUserData)
    {
        int deflate, heart_beat;
        bool suppressLog = false;
        const char* buffer_size;
        const char* extra_headers;
        int rtp_packets = 1; //20ms burst
        bool no_reconnect = false;
        const char* tls_cafile = NULL;;
        const char* tls_keyfile = NULL;;
        const char* tls_certfile = NULL;;
        bool tls_disable_hostname_validation = false;

        switch_channel_t *channel = switch_core_session_get_channel(session);

        if (switch_channel_var_true(channel, "STREAM_MESSAGE_DEFLATE")) {
            deflate = 1;
        }

        if (switch_channel_var_true(channel, "STREAM_SUPPRESS_LOG")) {
            suppressLog = true;
        }

        if (switch_channel_var_true(channel, "STREAM_NO_RECONNECT")) {
            no_reconnect = true;
        }

        tls_cafile = switch_channel_get_variable(channel, "STREAM_TLS_CA_FILE");
        tls_keyfile = switch_channel_get_variable(channel, "STREAM_TLS_KEY_FILE");
        tls_certfile = switch_channel_get_variable(channel, "STREAM_TLS_CERT_FILE");

        if (switch_channel_var_true(channel, "STREAM_TLS_DISABLE_HOSTNAME_VALIDATION")) {
            tls_disable_hostname_validation = true;
        }

        const char* heartBeat = switch_channel_get_variable(channel, "STREAM_HEART_BEAT");
        if (heartBeat) {
            char *endptr;
            long value = strtol(heartBeat, &endptr, 10);
            if (*endptr == '\0' && value <= INT_MAX && value >= INT_MIN) {
                heart_beat = (int) value;
            }
        }

        if ((buffer_size = switch_channel_get_variable(channel, "STREAM_BUFFER_SIZE"))) {
            int bSize = atoi(buffer_size);
            if(bSize % 20 != 0) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "%s: Buffer size of %s is not a multiple of 20ms. Using default 20ms.\n",
                                  switch_channel_get_name(channel), buffer_size);
            } else if(bSize >= 20){
                rtp_packets = bSize/20;
            }
        }

        extra_headers = switch_channel_get_variable(channel, "STREAM_EXTRA_HEADERS");

        // allocate per-session tech_pvt
        auto* tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));

        if (!tech_pvt) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
            return SWITCH_STATUS_FALSE;
        }
        if (SWITCH_STATUS_SUCCESS != stream_data_init(tech_pvt, session, wsUri, audioType, samples_per_second, sampling, channels, metadata, responseHandler, deflate, heart_beat,
                                                        suppressLog, rtp_packets, extra_headers, no_reconnect, tls_cafile, tls_keyfile, tls_certfile, tls_disable_hostname_validation)) {
            destroy_tech_pvt(tech_pvt);
            return SWITCH_STATUS_FALSE;
        }

        *ppUserData = tech_pvt;

        return SWITCH_STATUS_SUCCESS;
    }

    switch_bool_t stream_frame(switch_media_bug_t *bug) {
        auto *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
        if (!tech_pvt || tech_pvt->audio_paused) return SWITCH_TRUE;

        if (switch_mutex_trylock(tech_pvt->mutex) != SWITCH_STATUS_SUCCESS) {
            return SWITCH_TRUE;
        }

        auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);

        if (!pAudioStreamer || !pAudioStreamer->isConnected()) {
            switch_mutex_unlock(tech_pvt->mutex);
            return SWITCH_TRUE;
        }

        auto flush_sbuffer = [&]() {
            switch_size_t inuse = switch_buffer_inuse(tech_pvt->sbuffer);
            if (inuse > 0) {
                std::vector<uint8_t> tmp(inuse);
                switch_buffer_read(tech_pvt->sbuffer, tmp.data(), inuse);
                switch_buffer_zero(tech_pvt->sbuffer);
                pAudioStreamer->writeBinary(tmp.data(), inuse);
            }
        };

        uint8_t data_buf[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = {0};
        frame.data = data_buf;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
            if (!tech_pvt->resampler) {
                if (tech_pvt->rtp_packets == 1) {
                    pAudioStreamer->writeBinary((uint8_t *)frame.data, frame.datalen);
                } else {
                    size_t write_len = frame.datalen;
                    const uint8_t *write_data = (const uint8_t *)frame.data;
                    switch_size_t free_space = switch_buffer_freespace(tech_pvt->sbuffer);
                    if (write_len > free_space) {
                        flush_sbuffer();
                    }
                    switch_buffer_write(tech_pvt->sbuffer, write_data, write_len);
                    if (switch_buffer_freespace(tech_pvt->sbuffer) == 0) {
                        flush_sbuffer();
                    }
                }
                continue;
            }

            size_t available = switch_buffer_freespace(tech_pvt->sbuffer);
            spx_uint32_t in_len = frame.samples;
            spx_uint32_t out_len = available / (tech_pvt->channels * sizeof(spx_int16_t));
            if (out_len == 0) {
                flush_sbuffer();
                available = switch_buffer_freespace(tech_pvt->sbuffer);
                out_len = available / (tech_pvt->channels * sizeof(spx_int16_t));
            }

            spx_int16_t outbuf[out_len * tech_pvt->channels];

            if (tech_pvt->channels == 1) {
                speex_resampler_process_int(
                    tech_pvt->resampler,
                    0,
                    (const spx_int16_t *)frame.data,
                    &in_len,
                    outbuf,
                    &out_len
                );
            } else {
                speex_resampler_process_interleaved_int(
                    tech_pvt->resampler,
                    (const spx_int16_t *)frame.data,
                    &in_len,
                    outbuf,
                    &out_len
                );
            }

            size_t bytes_written = out_len * tech_pvt->channels * sizeof(spx_int16_t);
            if (bytes_written > 0) {
                switch_buffer_write(
                    tech_pvt->sbuffer,
                    reinterpret_cast<const uint8_t *>(outbuf),
                    bytes_written
                );
                if (switch_buffer_freespace(tech_pvt->sbuffer) == 0) {
                    flush_sbuffer();
                }
            }
        }

        flush_sbuffer();

        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
    }

    switch_bool_t stream_frameread(switch_media_bug_t *bug)
    {
        private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
        if (!tech_pvt || tech_pvt->audio_paused) return SWITCH_TRUE;

        if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
            if (!tech_pvt->pAudioStreamer) {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);

            if(!pAudioStreamer->isConnected()) {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
            switch_frame_t frame = {0};
            frame.data = data;
            frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

            if (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && frame.datalen > 0) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "audio");
                cJSON_AddStringToObject(json, "direction", "read");

                // Base64编码音频数据
                std::string base64_data = base64_encode((uint8_t *)frame.data, frame.datalen,false);
                cJSON_AddStringToObject(json, "data", base64_data.c_str());

                // 添加时间戳
                char timestamp[64];
                switch_snprintf(timestamp, sizeof(timestamp), "%" SWITCH_TIME_T_FMT, switch_time_now());
                cJSON_AddStringToObject(json, "timestamp", timestamp);

                char *json_str = cJSON_PrintUnformatted(json);

                if (pAudioStreamer) pAudioStreamer->writeAudio(json_str);

                cJSON_Delete(json);
                free(json_str);
            }
            switch_mutex_unlock(tech_pvt->mutex);
        }
        return SWITCH_TRUE;
    }

    switch_bool_t stream_framewrite(switch_media_bug_t *bug)
    {
        private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
        if (!tech_pvt || tech_pvt->audio_paused) return SWITCH_TRUE;

        if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {

            if (!tech_pvt->pAudioStreamer) {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);

            if(!pAudioStreamer->isConnected()) {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
            switch_frame_t frame = {0};
            frame.data = data;
            frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

            if (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && frame.datalen > 0)  {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "audio");
                cJSON_AddStringToObject(json, "direction", "write");

                // Base64编码音频数据
                std::string base64_data = base64_encode((uint8_t *)frame.data, frame.datalen, false);
                cJSON_AddStringToObject(json, "data", base64_data.c_str());


                // 添加时间戳
                char timestamp[64];
                switch_snprintf(timestamp, sizeof(timestamp), "%" SWITCH_TIME_T_FMT, switch_time_now());
                cJSON_AddStringToObject(json, "timestamp", timestamp);

                char *json_str = cJSON_PrintUnformatted(json);
                if (pAudioStreamer) pAudioStreamer->writeAudio(json_str);

                cJSON_Delete(json);
                free(json_str);
            }
            switch_mutex_unlock(tech_pvt->mutex);
        }
        return SWITCH_TRUE;
    }
    // fixme 这里的编码和写入逻辑需要优化
    switch_bool_t stream_write_frame(switch_media_bug_t *bug) {
        switch_core_session_t *session = switch_core_media_bug_get_session(bug);
        auto *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);

        if (!tech_pvt || tech_pvt->audio_paused) {
           return SWITCH_TRUE;
        }

        // 1. 获取通话的目标编码器信息
        switch_codec_t *codec = switch_core_session_get_write_codec(session);
        if (!codec || !switch_core_codec_ready(codec)) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "No valid codec or implementation\n");
            return SWITCH_FALSE;
        }


        const int target_rate = codec->implementation->actual_samples_per_second;
        const int target_channels = codec->implementation->number_of_channels;
        const int samples_per_frame = codec->implementation->samples_per_packet;
        const int bytes_per_sample = 2; // 16-bit PCM
        const size_t bytes_per_frame = samples_per_frame * bytes_per_sample * target_channels;


        uint8_t pcm_data[bytes_per_frame];
        switch_size_t read_len = 0;

        // 2. 从我们的TTS缓冲区读取一帧的PCM数据
        // 注意: switch_buffer 是线程安全的，这里无需加锁
        if (switch_buffer_inuse(tech_pvt->tts_buffer) >= bytes_per_frame) {
            read_len = switch_buffer_read(tech_pvt->tts_buffer, pcm_data, bytes_per_frame);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Read %zu bytes from tts_buffer\n", read_len);
        } else {
            return SWITCH_TRUE;
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "TTS buffer underflow, sending silence\n");
        }

        uint8_t encoded_data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        uint32_t encoded_len = sizeof(encoded_data);
        unsigned int flags = 0;

        // 3. 如果从缓冲区读到了数据，就编码它；如果没读到（缓冲区为空），就编码静音
        void* data_to_encode = (read_len > 0) ? pcm_data : nullptr;
        uint32_t samples_to_encode = (read_len > 0) ? (read_len / (tech_pvt->channels * sizeof(int16_t))) : samples_per_frame;

        // TODO: 在此添加重采样逻辑（如果需要）
        // 如果TTS采样率(tech_pvt->sampling)和通话目标采样率(codec->implementation->actual_samples_per_second)不同
        // 需要在此处对 pcm_data 进行重采样，类似于 stream_frame 函数中的做法。

        switch_status_t status = switch_core_codec_encode(codec,
                                        nullptr, // prev_codec
                                        data_to_encode,
                                        bytes_per_frame,
                                        tech_pvt->sampling, // 输入采样率
                                        encoded_data,
                                        &encoded_len,
                                        &samples_to_encode,
                                        &flags);

        if (status != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Codec encode failed with status %d\\n", status);
            return SWITCH_TRUE;
        }

        // 4. 将编码后的数据（或静音）写入 bug 的 write_frame
        if (encoded_len > 0) {
            switch_frame_t *write_frame = switch_core_media_bug_get_native_write_frame(bug);
            if (!write_frame) {
            // fixme
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Failed to get native write frame\n");
                return SWITCH_TRUE;
            }

            // 检查和调整缓冲区大小
            if (write_frame->buflen < encoded_len) {
                // 手动分配新缓冲区
                void *new_buffer = switch_core_alloc(switch_core_session_get_pool(session), encoded_len);
                if (!new_buffer) {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Failed to allocate buffer\n");
                    return SWITCH_TRUE;
                }
                // 复制现有数据（如果有），并更新缓冲区
                if (write_frame->data && write_frame->datalen > 0) {
                    memcpy(new_buffer, write_frame->data, write_frame->datalen);
                }
                write_frame->data = new_buffer;
                write_frame->buflen = encoded_len;
            }

            // 写入编码后的数据
            memcpy(write_frame->data, encoded_data, encoded_len);
            write_frame->datalen = encoded_len;
            write_frame->samples = samples_to_encode;
            write_frame->payload = codec->implementation->ianacode;
            write_frame->codec = codec;

            // 4.3 准备并写入帧
            switch_frame_t frame = {0};
            uint8_t* session_frame_data = static_cast<uint8_t*>(
                switch_core_session_alloc(session, encoded_len));

            if (!session_frame_data) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                "Failed to allocate frame data\n");
                break;
            }

            memcpy(session_frame_data, encoded_data, encoded_len);

            frame.data = session_frame_data;
            frame.datalen = encoded_len;
            frame.samples = samples;
            frame.rate = target_rate;
            frame.channels = target_channels;
            frame.codec = codec;
            frame.payload = codec->implementation->ianacode;
            // 设置时间戳和序号
            frame.seq = m_seq++;
//            frame.timestamp = m_timestamp; //fixme 如何正确设置每一帧音频的时间戳
            m_timestamp += samples;  // 增加样本数，如每帧160

            if (switch_core_session_write_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0)
                != SWITCH_STATUS_SUCCESS) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                "Failed to write frame at offset %zu\n", offset);
                break;
            }


        }
        return SWITCH_TRUE;
    }

    switch_status_t stream_session_cleanup(switch_core_session_t *session, char* text, int channelIsClosing) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        auto *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
        if(bug)
        {
            auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
            char sessionId[MAX_SESSION_ID];
            strcpy(sessionId, tech_pvt->sessionId);

            switch_mutex_lock(tech_pvt->mutex);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%s) stream_session_cleanup\n", sessionId);

            switch_channel_set_private(channel, MY_BUG_NAME, nullptr);
            if (!channelIsClosing) {
                switch_core_media_bug_remove(session, &bug);
            }

            if (tech_pvt->tts_buffer) {
                    switch_buffer_destroy(&tech_pvt->tts_buffer);
            }

            auto* audioStreamer = (AudioStreamer *) tech_pvt->pAudioStreamer;
            if(audioStreamer) {
                audioStreamer->deleteFiles();
                if (text) audioStreamer->writeText(text);
                finish(tech_pvt);
            }

            destroy_tech_pvt(tech_pvt);

            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%s) stream_session_cleanup: connection closed\n", sessionId);
            return SWITCH_STATUS_SUCCESS;
        }

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "stream_session_cleanup: no bug - websocket connection already closed\n");
        return SWITCH_STATUS_FALSE;
    }
}

