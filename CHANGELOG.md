# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-11-28

### Added
- Initial release of mod_audio_stream
- Audio streaming from FreeSWITCH channels to WebSocket endpoints
- Support for mono, mixed, and stereo audio mixing
- JSON response handling with custom events
- TLS/SSL support for secure WebSocket connections
- Automatic reconnection capability
- Configurable buffer sizes and heartbeats
- DTMF digit detection and forwarding
- Playback tracking, pause, and resume capabilities
- Support for both base64 encoded audio and raw binary stream

### Changed
- Improved documentation and README files
- Enhanced build process with CMake
- Better error handling and logging

### Fixed
- Various bug fixes and stability improvements