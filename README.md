# supermoan

Blazing fast native C++ natural dictation for macOS.

Press a hotkey to start recording. Press it again to stop. Supermoan
transcribes your speech and types it into the app you are using.

- Native C++26 / Objective-C++
- Tiny app bundle
- No Electron
- No third-party libraries

## Install

```sh
brew install --cask korigamik/homebrew-tap/supermoan
```

## Why

Fancy dictation apps like [Wispr Flow](https://wisprflow.ai/), [Superwhisper](https://superwhisper.com/) and similar tools have their features behind paid tiers or hosted services.

We moan:

- Free
- Self-managed
- BYOK style Groq API integration
- No account system
- No bundled subscription

## Stack

- AudioToolbox: microphone capture as 16 kHz mono WAV
- ApplicationServices: layout-independent typing at the cursor
- Foundation: upload to Groq Whisper

## Requirements

- macOS 14+
- Homebrew
- Groq API key

## Build From Source

```sh
xcode-select --install
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~
```

Install target:

```text
~/Applications/Supermoan.app
```

## Setup

Put your API key in the environment or in `~/.env`:

```sh
GROQ_API_KEY=<your_key>
```

Bind your hotkey to:

```sh
open -gn /Applications/Supermoan.app
```

Launching with `open` matters. It makes macOS attribute microphone access to
the app bundle, so the normal permission prompt appears.

On first use, grant:

- Microphone
- Accessibility, for typing at the cursor

Reinstalling changes the ad-hoc signature, so macOS may ask for these again.

## Usage

```sh
open -gn /Applications/Supermoan.app
/Applications/Supermoan.app/Contents/MacOS/Supermoan --log
/Applications/Supermoan.app/Contents/MacOS/Supermoan --no-type
```

- First launch records.
- Second launch transcribes and types.
- `--log` prints `/tmp/supermoan.log`.
- `--no-type` logs keystrokes instead of posting them.

## Config

Optional config file:

```text
~/.config/supermoan/config
```

Format:

```text
long-recording-threshold : 1000
transcription-prompt : ""
silence-threshold : -50
```

## License

MIT License. See [LICENSE](LICENSE).
