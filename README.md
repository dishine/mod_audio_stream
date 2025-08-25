
# mod_audio_stream

[![许可证：MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GitHub 发布](https://img.shields.io/github/v/release/amigniter/mod_audio_stream)](https://github.com/amigniter/mod_audio_stream/releases)


> **本文档基于原项目 [amigniter/mod_audio_stream](https://github.com/amigniter/mod_audio_stream) 进行翻译和修改。**
> **此项目是在原项目基础上进行的二次开发。原项目由 [amigniter](https://github.com/amigniter) 开发并维护。**

一个 FreeSWITCH 模块，用于将 L16 音频从信道流式传输到 WebSocket 端点。如果 WebSocket 返回响应（如 JSON），它可以有效地与 IBM Watson 等 ASR 引擎配合使用，或用于任何其他你认为适用的场景。

## 功能特点

- 将音频从 FreeSWITCH 信道流式传输到 WebSocket 端点
- 支持多种音频混合选项（单声道、混合声道、立体声）
- 具有播放支持的双向流式传输
- 支持 base64 编码音频和原始二进制流
- 播放跟踪、暂停和恢复功能
- DTMF 数字检测和转发
- 带有自定义事件的 JSON 响应处理
- 支持 TLS/SSL 以实现安全的 WebSocket 连接
- 自动重连功能
- 可配置的缓冲区大小和心跳
- 支持放音，返回ws音频数据可直接播放

## 安装

### 依赖项

该模块在 Debian/Ubuntu 系统上需要以下依赖项：

```bash
sudo apt-get install libfreeswitch-dev libssl-dev zlib1g-dev libspeexdsp-dev
```

### 构建

1. 克隆仓库：
   ```bash
   git clone https://github.com/amigniter/mod_audio_stream.git
   cd mod_audio_stream
   ```

2. 初始化并更新子模块：
   ```bash
   git submodule init
   git submodule update
   ```

3. 构建模块：
   ```bash
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make
   sudo make install
   ```

4. 要构建带有 TLS 支持的版本：
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Release -DUSE_TLS=ON ..
   ```

### Debian 软件包

要构建 Debian 软件包：
```bash
cpack -G DEB
```

软件包将在 `_packages` 目录中创建。

## 使用方法

### API 命令

该模块提供以下 API 命令：

#### 开始流式传输
```
uuid_audio_stream <uuid> start <wss-url> <mix-type> <sampling-rate> <metadata>
```

参数：
- `uuid`：FreeSWITCH 信道唯一 ID
- `wss-url`：WebSocket URL（`ws://` 或 `wss://`）
- `mix-type`：音频混合选项
    - `mono`：包含呼叫者音频的单声道
    - `mixed`：包含呼叫者和被叫者音频的单声道
    - `stereo`：两个声道，一个包含呼叫者音频，另一个包含被叫者音频
- `sampling-rate`：音频采样率
    - `8k`：8000 Hz 采样率
    - `16k`：16000 Hz 采样率
- `metadata`（可选）：在音频流式传输开始前发送的 UTF-8 文本

#### 发送文本
```
uuid_audio_stream <uuid> send_text <metadata>
```

向 WebSocket 服务器发送文本。

#### 停止流式传输
```
uuid_audio_stream <uuid> stop <metadata>
```

停止音频流式传输并关闭 WebSocket 连接。

#### 暂停流式传输
```
uuid_audio_stream <uuid> pause
```

暂停音频流式传输。

#### 恢复流式传输
```
uuid_audio_stream <uuid> resume
```

恢复音频流式传输。

### 信道变量

以下信道变量可用于配置 WebSocket 连接：

| 变量 | 描述 | 默认值 |
|----------|-------------|---------|
| `STREAM_MESSAGE_DEFLATE` | 禁用每条消息的 deflate 压缩 | off |
| `STREAM_HEART_BEAT` | 心跳间隔（秒） | off |
| `STREAM_SUPPRESS_LOG` | 抑制日志 | off |
| `STREAM_BUFFER_SIZE` | 缓冲区持续时间（毫秒） | 20 |
| `STREAM_EXTRA_HEADERS` | 用于附加头的 JSON 对象 | none |
| `STREAM_NO_RECONNECT` | 禁用自动重连 | off |
| `STREAM_TLS_CA_FILE` | CA 证书文件 | SYSTEM |
| `STREAM_TLS_KEY_FILE` | WSS 连接的客户端密钥 | none |
| `STREAM_TLS_CERT_FILE` | WSS 连接的客户端证书 | none |
| `STREAM_TLS_DISABLE_HOSTNAME_VALIDATION` | 禁用主机名验证 | false |

### 事件

该模块生成以下事件类型：

- `mod_audio_stream::connect`：成功连接到 WebSocket 服务器
- `mod_audio_stream::disconnect`：与 WebSocket 服务器断开连接
- `mod_audio_stream::error`：发生连接错误
- `mod_audio_stream::json`：从 WebSocket 服务器收到 JSON 响应
- `mod_audio_stream::play`：收到音频的播放事件

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 支持

如需商业支持、许可选项或有任何问题，请联系 [shinedi2015@gmail.com](mailto:amsoftswitch@gmail.com)。
```
