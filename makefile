run: main.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp
	g++ -o run main.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp -lpthread -g -w -lmysqlclient
clean:
	rm -r run