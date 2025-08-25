# Security Policy

## Supported Versions

The following versions of mod_audio_stream are currently being supported with security updates:

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability within mod_audio_stream, please send an email to amsoftswitch@gmail.com. All security vulnerabilities will be promptly addressed.

Please do not publicly disclose the vulnerability until it has been addressed by the team.

## Security Considerations

When using mod_audio_stream, please consider the following security aspects:

1. **WebSocket Connections**: Ensure that WebSocket endpoints are properly secured, especially when using WSS (WebSocket Secure) connections.

2. **TLS Configuration**: When using TLS, properly configure certificates and validate server certificates to prevent man-in-the-middle attacks.

3. **Input Validation**: The module validates UTF-8 text input, but additional validation may be required at the application level.

4. **Network Exposure**: Limit network exposure of WebSocket endpoints to only trusted sources.

5. **Authentication**: Implement proper authentication mechanisms on WebSocket servers to prevent unauthorized access.

## Best Practices

1. Keep your FreeSWITCH installation up to date
2. Use strong, unique passwords for any authentication systems
3. Regularly review and audit WebSocket connection logs
4. Implement proper firewall rules to restrict access to WebSocket endpoints
5. Use WSS instead of WS for production environments