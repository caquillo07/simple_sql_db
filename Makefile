run:
	mkdir -p build
	clang -Wall *.c -o ./build/db && ./build/db
