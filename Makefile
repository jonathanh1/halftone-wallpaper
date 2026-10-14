APP := HalftoneWallpaper.app
BUNDLE := $(APP)/Contents
MACOS := $(BUNDLE)/MacOS
RES := $(BUNDLE)/Resources

.PHONY: all run clean
all: $(APP)

$(APP): Sources/main.swift Resources/ripple.html Info.plist
	mkdir -p $(MACOS) $(RES)
	swiftc -O -o $(MACOS)/HalftoneWallpaper Sources/main.swift
	cp Info.plist $(BUNDLE)/Info.plist
	cp Resources/ripple.html $(RES)/ripple.html
	codesign --force --sign - $(APP)
	@echo "built $(APP) — run 'make run' or 'open $(APP)'"

run: $(APP)
	open $(APP)

clean:
	rm -rf $(APP)
