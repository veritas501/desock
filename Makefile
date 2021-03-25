all:
	mkdir -p build
	$(CC) tests/echo_server.c -o build/echo_server
	$(CC) tests/echo_client.c -o build/echo_client
	$(CC) -shared -fPIC desock.c -o build/desock.so -ldl -lpthread

clean:
	rm -rf build