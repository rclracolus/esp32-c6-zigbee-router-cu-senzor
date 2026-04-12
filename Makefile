# Makefile pentru ESP32-C6 Zigbee Router + MQ-7

# Configurare port serial (modifică după nevoie)
PORT ?= /dev/ttyUSB0
BAUD ?= 115200

.PHONY: all build flash monitor clean fullclean menuconfig calibrate help

# Default target
all: build

# Configurare target ESP32-C6
setup:
	@echo "📦 Setting up ESP32-C6 target..."
	idf.py set-target esp32c6

# Build proiect
build:
	@echo "🔨 Building project..."
	idf.py build

# Flash firmware
flash:
	@echo "⚡ Flashing to $(PORT)..."
	idf.py -p $(PORT) flash

# Monitor serial
monitor:
	@echo "📊 Starting serial monitor on $(PORT)..."
	idf.py -p $(PORT) monitor

# Flash și monitor
flash-monitor: flash monitor

# Build complet (clean + build)
rebuild: clean build

# Flash complet (build + flash + monitor)
deploy: build flash monitor

# Menuconfig
menuconfig:
	@echo "⚙️  Opening menuconfig..."
	idf.py menuconfig

# Clean build files
clean:
	@echo "🧹 Cleaning build files..."
	idf.py clean

# Full clean (inclusiv sdkconfig)
fullclean:
	@echo "🧹 Full clean (including sdkconfig)..."
	idf.py fullclean

# Erase flash complet
erase:
	@echo "⚠️  Erasing flash on $(PORT)..."
	idf.py -p $(PORT) erase-flash

# Calibrare MQ-7
calibrate:
	@echo "🔬 Starting MQ-7 calibration..."
	@echo "Make sure device is flashed and connected to $(PORT)"
	@sleep 2
	python3 calibrate_mq7.py $(PORT)

# Info despre build
info:
	@echo "📋 Project info:"
	@echo "  Target: ESP32-C6"
	@echo "  Port: $(PORT)"
	@echo "  Baud: $(BAUD)"
	@idf.py show-efuse-summary 2>/dev/null || echo "  (Connect device for efuse info)"

# Size info
size:
	@echo "📏 Binary size info:"
	idf.py size

# Help
help:
	@echo "╔════════════════════════════════════════════════════════╗"
	@echo "║  ESP32-C6 Zigbee Router + MQ-7 - Makefile Help         ║"
	@echo "╚════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📦 Setup & Build:"
	@echo "  make setup          - Configure ESP32-C6 target"
	@echo "  make build          - Build project"
	@echo "  make rebuild        - Clean + build"
	@echo "  make menuconfig     - Open configuration menu"
	@echo ""
	@echo "⚡ Flash & Monitor:"
	@echo "  make flash          - Flash firmware to device"
	@echo "  make monitor        - Open serial monitor"
	@echo "  make flash-monitor  - Flash + monitor"
	@echo "  make deploy         - Build + flash + monitor"
	@echo ""
	@echo "🧹 Cleanup:"
	@echo "  make clean          - Clean build files"
	@echo "  make fullclean      - Full clean (including config)"
	@echo "  make erase          - Erase entire flash"
	@echo ""
	@echo "🔬 Calibration & Info:"
	@echo "  make calibrate      - Run MQ-7 calibration script"
	@echo "  make info           - Show project info"
	@echo "  make size           - Show binary size"
	@echo ""
	@echo "🔧 Configuration:"
	@echo "  PORT=/dev/ttyUSB1 make flash  - Use different port"
	@echo "  BAUD=921600 make monitor      - Use different baud rate"
	@echo ""
	@echo "📚 Quick Start:"
	@echo "  1. make setup       # First time only"
	@echo "  2. make deploy      # Build, flash, and monitor"
	@echo "  3. make calibrate   # After device is running"
	@echo ""

# Comenzi de diagnosticare
diagnose:
	@echo "🔍 Running diagnostics..."
	@echo ""
	@echo "1. Checking ESP-IDF installation:"
	@which idf.py > /dev/null && echo "  ✓ idf.py found" || echo "  ✗ idf.py not found - run '. ~/esp/esp-idf/export.sh'"
	@echo ""
	@echo "2. Checking Python dependencies:"
	@python3 -c "import serial" 2>/dev/null && echo "  ✓ pyserial installed" || echo "  ✗ pyserial missing - run 'pip install pyserial'"
	@echo ""
	@echo "3. Checking USB device:"
	@ls $(PORT) 2>/dev/null && echo "  ✓ Device found at $(PORT)" || echo "  ✗ No device at $(PORT)"
	@echo ""
	@echo "4. Checking permissions:"
	@groups | grep -q dialout && echo "  ✓ User in dialout group" || echo "  ⚠ User not in dialout group - run 'sudo usermod -aG dialout $$USER'"
	@echo ""

# Monitor logs filtered
monitor-mq7:
	@echo "📊 Monitoring MQ-7 sensor data only..."
	idf.py -p $(PORT) monitor | grep "MQ-7"

monitor-zigbee:
	@echo "📊 Monitoring Zigbee events only..."
	idf.py -p $(PORT) monitor | grep -E "(ZB_|Zigbee|network)"

# Git helpers (optional)
git-status:
	@git status --short 2>/dev/null || echo "Not a git repository"

git-commit:
	@git add -A
	@git commit -m "Update ESP32-C6 Zigbee Router project"
	@git log -1 --oneline
