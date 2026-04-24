![MyWhisper icon](icons/icon_idle.svg) 
![MyWhisper icon](icons/icon_recording_3.svg)
![MyWhisper icon](icons/icon_transcribing_2.svg)



# mywhisper-gtk

Minimalist Whisper for voice input in different Linux applications, including terminal apps.

Transcription is powered by the `gpt-4o-transcribe` model.

Tested on Ubuntu 24.04 with an X11 server.

## Run

Install packages required to run the app:

```bash
sudo apt update
sudo apt install -y alsa-utils ffmpeg
```

Run:

```bash
./app.out
```

Set `OpenAI API Key` in app settings (`Settings` in tray menu).

## Build

Install packages required to build:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk-3-dev libayatana-appindicator3-dev libx11-dev libxtst-dev libsoup-3.0-dev
```

Build:

```bash
make build
```

## Screenshots

<img width="276" height="296" alt="image" src="https://github.com/user-attachments/assets/7beaca98-a292-4ecd-bd99-79bce675df49" />

<img width="642" height="523" alt="image" src="https://github.com/user-attachments/assets/04ec3996-9e0f-45fa-829b-6594dc80fb24" />


## Hotkeys

| Hotkey | Action |
|---|---|
| Double `Ctrl` / `Shift` / `Alt` | Start/stop voice input; key and interval are configurable in settings |
| `Esc` (during voice input) | Cancel |
