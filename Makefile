.PHONY: help build test install restart pair flash monitor clean bump-patch bump-minor bump-major

help:
	@echo "Targets:"
	@echo "  build           Build bridge (swift build -c release)"
	@echo "  test            Run bridge tests + firmware native tests"
	@echo "  install         Install bridge as launchd agent"
	@echo "  restart         Restart the running bridge launchd agent (kickstart)"
	@echo "  pair            Run bridge in foreground (verbose) to see the watch connect"
	@echo "  flash           Flash firmware to a connected M5Stack StopWatch"
	@echo "  monitor         Open serial monitor on the watch"
	@echo "  clean           Remove build artifacts"
	@echo "  bump-patch      Bump firmware release version: patch (0.1.1 -> 0.1.2)"
	@echo "  bump-minor      Bump firmware release version: minor (0.1.1 -> 0.2.0)"
	@echo "  bump-major      Bump firmware release version: major (0.1.1 -> 1.0.0)"

build:
	cd bridge && swift build -c release

test:
	cd bridge && swift test
	cd firmware && pio test -e native
	python3 firmware/tools/test_bump_version.py

install: build
	./bridge/.build/release/stopwatch-bridge install

restart:
	launchctl kickstart -k "gui/$$(id -u)/dev.stopwatch.bridge"

pair: build
	./bridge/.build/release/stopwatch-bridge pair

flash:
	python3 firmware/tools/zap.py || true
	cd firmware && pio run -t upload

monitor:
	cd firmware && pio device monitor -b 115200

clean:
	cd bridge && swift package clean
	cd firmware && pio run -t clean

bump-patch:
	python3 firmware/tools/bump_version.py patch

bump-minor:
	python3 firmware/tools/bump_version.py minor

bump-major:
	python3 firmware/tools/bump_version.py major
