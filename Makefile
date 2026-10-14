APP := HalftoneWallpaper.app
BUNDLE := $(APP)/Contents
MACOS := $(BUNDLE)/MacOS
RES := $(BUNDLE)/Resources
INSTALL_DIR := $(HOME)/Applications
AGENT_DIR := $(HOME)/Library/LaunchAgents
LABEL := local.halftone-wallpaper
UID_ := $(shell id -u)
SAVER := HalftoneSaver.saver
SINSTALL_DIR := $(HOME)/Library/Screen Savers
SAVER_RES := $(SAVER)/Contents/Resources
SAVER_MACOS := $(SAVER)/Contents/MacOS

.PHONY: all run saver install-saver install uninstall clean
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

saver: $(SAVER)

$(SAVER): Sources/HalftoneSaverView.swift Resources/ripple.html Saver-Info.plist
	mkdir -p $(SAVER_MACOS) $(SAVER_RES)
	swiftc -O -emit-library -emit-module -module-name HalftoneSaver -emit-module-path /tmp/HalftoneSaver.swiftmodule -o $(SAVER_MACOS)/HalftoneSaver Sources/HalftoneSaverView.swift
	cp Saver-Info.plist $(SAVER)/Contents/Info.plist
	cp Resources/ripple.html $(SAVER_RES)/ripple.html
	codesign --force --sign - $(SAVER)
	@echo "built $(SAVER) — 'make install-saver' to add to System Settings"

install-saver: $(SAVER)
	mkdir -p "$(SINSTALL_DIR)"
	rm -rf "$(SINSTALL_DIR)/$(SAVER)"
	cp -R $(SAVER) "$(SINSTALL_DIR)/"
	xattr -dr com.apple.quarantine "$(SINSTALL_DIR)/$(SAVER)" 2>/dev/null || true
	@echo "installed to ~/Library/Screen Savers — open System Settings → Screen Saver → Halftone Aurora"

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
	rm -rf "$(SINSTALL_DIR)/$(SAVER)"
	@echo "uninstalled"

clean:
	rm -rf $(APP) $(SAVER)
