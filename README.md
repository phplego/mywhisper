![MyWhisper icon](icons/icon_idle.svg) 
![MyWhisper icon](icons/icon_recording_3.svg)
![MyWhisper icon](icons/icon_transcribing_2.svg)



# mywhisper-gtk

Minimalist Whisper for voice input in different Linux applications, including terminal apps.

Transcription is powered by the `gpt-transcribe` model.

Tested on Ubuntu 24.04 with an X11 server.

## Quick start

Download the latest AppImage from [Releases](https://github.com/phplego/mywhisper/releases/latest).

Make the AppImage executable and run it:

```bash
chmod +x mywhisper-gtk-*.AppImage
./mywhisper-gtk-*.AppImage
```

Set `OpenAI API Key` in app settings (`Settings` in tray menu).

## Build from source

Install the dependencies, build, and run:

```bash
sudo apt update
sudo apt install -y build-essential libayatana-appindicator3-dev libxtst-dev libsoup-3.0-dev alsa-utils opus-tools
make build
./app.out
```

## Screenshots

<img width="276" height="296" alt="MyWhisper tray menu" src="screenshots/readme-menu.png" />

<img width="642" height="523" alt="MyWhisper settings" src="screenshots/readme-settings.png" />


## Hotkeys

| Hotkey | Action |
|---|---|
| Double `Ctrl` / `Shift` / `Alt` | Start/stop voice input; key and interval are configurable in settings |
| `Esc` (during voice input) | Cancel |
