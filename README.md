# supermoan

Dictation at the cursor for macOS. Press a hotkey, talk, press it again —
the transcription is typed wherever your cursor is.

A single C++26 binary using only Apple system frameworks:

- **AudioToolbox** — microphone capture (16 kHz mono WAV)
- **ApplicationServices** — keystroke synthesis, layout-independent Unicode
- **Foundation** — upload to [Groq](https://console.groq.com)'s Whisper API

## Dependencies

- macOS 14+ with Xcode Command Line Tools (`xcode-select --install`)
- CMake ≥ 3.28 (`brew install cmake`)
- A Groq API key (free tier available)

No third-party libraries.

## Build & install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~        # -> ~/Applications/Supermoan.app
```

## Setup

1. Put your API key in the environment or in `~/.env`:
   ```
   GROQ_API_KEY=<your_key>
   ```
2. Bind a key to `open -gn ~/Applications/Supermoan.app`. With
   [AeroSpace](https://github.com/nikitabobko/AeroSpace):
   ```toml
   [mode.main.binding]
   cmd-shift-d = 'exec-and-forget open -gn ~/Applications/Supermoan.app'
   ```
   The `open` launch is required: it makes the app bundle the TCC
   "responsible process", so the microphone prompt is shown for Supermoan.
   Spawned directly by a hotkey daemon, macOS would kill the process
   without a prompt (hotkey daemons declare no microphone usage).
3. On first use, grant the two one-time permissions:
   - **Microphone** — macOS prompts automatically
   - **Accessibility** — System Settings → Privacy & Security → Accessibility
     (a dialog points there; needed to type at the cursor)

   Rebuilding changes the ad-hoc code signature, which invalidates both
   grants — re-allow after `cmake --install`.

## Usage

- First launch: starts recording (types `(recording...)` at the cursor)
- Second launch: stops, transcribes, and types the result
- `Supermoan.app/Contents/MacOS/Supermoan --log` — view the log
  (`/tmp/supermoan.log`, auto-truncated at 512 KB)
- `--no-type` — log keystrokes instead of posting them (debugging)

Audio is captured from the **system default input device**
(System Settings → Sound → Input).

## Configuration

`~/.config/supermoan/config`, `key : value` lines, `#` comments:

```
# seconds; recordings longer than this use whisper-large-v3 instead of -turbo
long-recording-threshold : 1000

# context words to bias Whisper's vocabulary
transcription-prompt : ""

# dB peak below which the recording counts as silent
silence-threshold : -50
```

## License

MIT
