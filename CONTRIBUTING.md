# Contributing to mod_audio_stream

Thank you for your interest in contributing to mod_audio_stream! This document provides guidelines and instructions for contributing to this project.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/your-username/mod_audio_stream.git`
3. Create a new branch for your feature or bug fix: `git checkout -b feature/your-feature-name`
4. Make your changes
5. Commit your changes with a descriptive commit message
6. Push to your fork: `git push origin feature/your-feature-name`
7. Create a pull request

## Code Style

- Follow the existing code style in the project
- Use meaningful variable and function names
- Add comments for complex logic
- Keep functions focused and small
- Write clear commit messages

## Reporting Issues

When reporting issues, please include:

1. A clear and descriptive title
2. Steps to reproduce the issue
3. Expected behavior
4. Actual behavior
5. Environment information (OS, FreeSWITCH version, etc.)
6. Any relevant logs or error messages

## Pull Request Guidelines

- Keep pull requests focused on a single feature or bug fix
- Include tests if applicable
- Update documentation as needed
- Ensure all tests pass
- Write a clear description of the changes
- Reference any related issues

## Development Setup

1. Install dependencies:
   ```bash
   sudo apt-get install libfreeswitch-dev libssl-dev zlib1g-dev libspeexdsp-dev cmake make gcc g++
   ```

2. Build the module:
   ```bash
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make
   ```

## Testing

- Add unit tests for new functionality
- Ensure existing tests continue to pass
- Test on different FreeSWITCH versions if possible

## License

By contributing to mod_audio_stream, you agree that your contributions will be licensed under the MIT License.