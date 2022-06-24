run: main.cpp http_conn.cpp
	g++ -o run main.cpp http_conn.cpp -lpthread -g
clean:
	rm -r run