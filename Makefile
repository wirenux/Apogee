all: clean build

run: build
	@./build/Apogee $(ARGS)

build:
	@mkdir -p build/
	@gcc src/main.c src/cJSON.c -o build/Apogee -lcurl -lm

clean:
	@rm -rf build/
