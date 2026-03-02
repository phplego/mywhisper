![MyWhisper icon](icons/icon_idle.svg) 
![MyWhisper icon](icons/icon_recording_3.svg)
![MyWhisper icon](icons/icon_transcribing_2.svg)



# mywhisper-gtk

Minimalist Whisper for voice input in different Linux applications, including terminal apps.

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
g++ -std=c++17 $(pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1 x11 xtst libsoup-3.0) app.cpp app_settings.cpp settings_store.cpp transcribe.cpp audio_pipeline.cpp hotkey_x11.cpp tray_ui.cpp $(pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1 x11 xtst libsoup-3.0) -o app.out
```
