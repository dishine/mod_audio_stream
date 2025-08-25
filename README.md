# mod_audio_stream

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GitHub release](https://img.shields.io/github/v/release/amigniter/mod_audio_stream)](https://github.com/amigniter/mod_audio_stream/releases)

A FreeSWITCH module that streams L16 audio from a channel to a websocket endpoint. If websocket sends back responses (eg. JSON) it can be effectively used with ASR engines such as IBM Watson etc., or any other purpose you find applicable.

## Features

- Streams audio from FreeSWITCH channels to WebSocket endpoints
- Supports various audio mixing options (mono, mixed, stereo)
- Bi-directional streaming with playback support
- Supports both base64 encoded audio and raw binary stream
- Playback tracking, pause, and resume capabilities
- DTMF digit detection and forwarding
- JSON response handling with custom events
- TLS/SSL support for secure WebSocket connections
- Automatic reconnection capability
- Configurable buffer sizes and heartbeats

## Installation

### Dependencies

The module requires the following dependencies on Debian/Ubuntu systems:

```bash
sudo apt-get install libfreeswitch-dev libssl-dev zlib1g-dev libspeexdsp-dev
```

### Building

1. Clone the repository:
   ```bash
   git clone https://github.com/amigniter/mod_audio_stream.git
   cd mod_audio_stream
   ```

2. Initialize and update submodules:
   ```bash
   git submodule init
   git submodule update
   ```

3. Build the module:
   ```bash
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make
   sudo make install
   ```

4. To build with TLS support:
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Release -DUSE_TLS=ON ..
   ```

### Debian Package

To build a Debian package:
```bash
cpack -G DEB
```

The package will be created in the `_packages` directory.

## Usage

### API Commands

The module exposes the following API commands:

#### Start Streaming
```
uuid_audio_stream <uuid> start <wss-url> <mix-type> <sampling-rate> <metadata>
```

Parameters:
- `uuid`: FreeSWITCH channel unique ID
- `wss-url`: WebSocket URL (`ws://` or `wss://`)
- `mix-type`: Audio mixing option
  - `mono`: Single channel containing caller's audio
  - `mixed`: Single channel containing both caller and callee audio
  - `stereo`: Two channels with caller audio in one and callee audio in the other
- `sampling-rate`: Audio sampling rate
  - `8k`: 8000 Hz sample rate
  - `16k`: 16000 Hz sample rate
- `metadata` (optional): UTF-8 text to send before audio streaming starts

#### Send Text
```
uuid_audio_stream <uuid> send_text <metadata>
```

Sends text to the WebSocket server.

#### Stop Streaming
```
uuid_audio_stream <uuid> stop <metadata>
```

Stops audio streaming and closes the WebSocket connection.

#### Pause Streaming
```
uuid_audio_stream <uuid> pause
```

Pauses audio streaming.

#### Resume Streaming
```
uuid_audio_stream <uuid> resume
```

Resumes audio streaming.

### Channel Variables

The following channel variables can be used to configure the WebSocket connection:

| Variable | Description | Default |
|----------|-------------|---------|
| `STREAM_MESSAGE_DEFLATE` | Disable per-message deflate compression | off |
| `STREAM_HEART_BEAT` | Heartbeat interval in seconds | off |
| `STREAM_SUPPRESS_LOG` | Suppress logging | off |
| `STREAM_BUFFER_SIZE` | Buffer duration in milliseconds | 20 |
| `STREAM_EXTRA_HEADERS` | JSON object for additional headers | none |
| `STREAM_NO_RECONNECT` | Disable automatic reconnection | off |
| `STREAM_TLS_CA_FILE` | CA certificate file | SYSTEM |
| `STREAM_TLS_KEY_FILE` | Client key for WSS connections | none |
| `STREAM_TLS_CERT_FILE` | Client certificate for WSS connections | none |
| `STREAM_TLS_DISABLE_HOSTNAME_VALIDATION` | Disable hostname validation | false |

### Events

The module generates the following event types:

- `mod_audio_stream::connect`: Successfully connected to WebSocket server
- `mod_audio_stream::disconnect`: Disconnected from WebSocket server
- `mod_audio_stream::error`: Connection error occurred
- `mod_audio_stream::json`: JSON response received from WebSocket server
- `mod_audio_stream::play`: Playback event for received audio

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Support

For commercial support, licensing options, or questions, please contact [amsoftswitch@gmail.com](mailto:amsoftswitch@gmail.com).