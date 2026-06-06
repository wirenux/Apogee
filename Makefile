all: clean build

run: build
	@./build/Apogee

build:
	@mkdir -p build/
	@gcc src/main.c -o build/Apogee -lcurl

clean:
	@rm -rf build/