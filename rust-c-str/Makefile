CARGO = cargo

CARGO_OPTS =

all:
	$(MAKE) build
	$(MAKE) doc

build:
	$(CARGO) $(CARGO_OPTS) build

clean:
	$(CARGO) $(CARGO_OPTS) clean

check:
	$(MAKE) build
	$(CARGO) bench --no-run
	$(MAKE) test

test:
	$(CARGO) $(CARGO_OPTS) test

bench:
	$(CARGO) $(CARGO_OPTS) bench

doc:
	$(CARGO) $(CARGO_OPTS) doc

.PHONY: all build clean check test bench doc
