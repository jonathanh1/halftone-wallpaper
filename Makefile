APP := HalftoneWallpaper.app
BUNDLE := $(APP)/Contents
MACOS := $(BUNDLE)/MacOS
RES := $(BUNDLE)/Resources
INSTALL_DIR := $(HOME)/Applications
AGENT_DIR := $(HOME)/Library/LaunchAgents
LABEL := local.halftone-wallpaper
UID_ := $(shell id -u)

.PHONY: all run install uninstall clean
all: $(APP)

$(APP): Sources/main.swift Resources/ripple.html Info.plist
	mkdir -p $(MACOS) $(RES)
	swiftc -O -o $(MACOS)/HalftoneWallpaper Sources/main.swift
	cp Info.plist $(BUNDLE)/Info.plist
	cp Resources/ripple.html $(RES)/ripple.html
	codesign --force --sign - $(APP)
	@echo "built $(APP) — 'make run' to launch, 'make install' for login-item install"

run: $(APP)
	open $(APP)

install: $(APP)
	mkdir -p $(INSTALL_DIR) $(AGENT_DIR)
	rm -rf $(INSTALL_DIR)/$(APP)
	cp -R $(APP) $(INSTALL_DIR)/
	xattr -dr com.apple.quarantine $(INSTALL_DIR)/$(APP) 2>/dev/null || true
	launchctl bootout gui/$(UID_) $(AGENT_DIR)/$(LABEL).plist 2>/dev/null || true
	sed 's|__INSTALL_DIR__|$(INSTALL_DIR)|g' LaunchAgent.plist > $(AGENT_DIR)/$(LABEL).plist
	launchctl bootstrap gui/$(UID_) $(AGENT_DIR)/$(LABEL).plist
	@echo "installed: $(INSTALL_DIR)/$(APP) + $(LABEL) LaunchAgent (runs at login)"

uninstall:
	launchctl bootout gui/$(UID_) $(AGENT_DIR)/$(LABEL).plist 2>/dev/null || true
	rm -f $(AGENT_DIR)/$(LABEL).plist
	rm -rf $(INSTALL_DIR)/$(APP)
	@echo "uninstalled"

clean:
	rm -rf $(APP)
