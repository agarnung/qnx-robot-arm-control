.PHONY: help test linux qnx demo sanitize analyze

help:
	@echo "make test   # tests POSIX (protocol layout + telemetry + python client)"
	@echo "make linux  # build POSIX control_core + bench_client"
	@echo "make qnx    # cross-compile with qcc (needs QNX_ENV sourced)"
	@echo "make demo   # one-shot Linux demo: mock UR + control_core + bench"
	@echo "make sanitize # tests with ASan + UBSan"
	@echo "make analyze  # GCC static analyzer on core sources"

test:
	$(MAKE) -C control_core test

linux:
	$(MAKE) -C control_core

qnx:
	./qnx-emulator/build-control-core.sh

demo:
	./qnx-emulator/test-harness-linux.sh

sanitize:
	$(MAKE) -C control_core sanitize

analyze:
	$(MAKE) -C control_core analyze
