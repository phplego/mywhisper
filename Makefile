PKGS = gtk+-3.0 ayatana-appindicator3-0.1 x11 xtst libsoup-3.0
CXX = g++
CXXFLAGS = -std=c++17 $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(shell pkg-config --libs $(PKGS))
SOURCES = app.cpp app_settings.cpp settings_store.cpp transcribe.cpp audio_pipeline.cpp hotkey_x11.cpp tray_ui.cpp
TARGET = app.out

.DEFAULT_GOAL := help

.PHONY: help build start

help:
	@echo "Targets:"
	@echo "  build  Build $(TARGET)"
	@echo "  start  Start $(TARGET) in background (detached)"
	@echo "  help   Show this help"

build:
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $(TARGET)

start:
	nohup ./$(TARGET) >/dev/null 2>&1 &
