run: main.cpp ./http/http_conn.cpp ./log/log.cpp
	g++ -o run main.cpp ./http/http_conn.cpp ./log/log.cpp -lpthread -g
clean:
	rm -r run