#run:
#	mkdir -p build
#	clang -Wall *.c -o ./build/db && ./build/db
dev: db run
db: main.c
	mkdir -p build
	gcc main.c -o ./build/db

run: db
	./build/db test.db

clean:
	rm -rf build/ *.db

test: db
	bundle exec rspec

ruby-deps:
	bundle install --path vendor/bundle
