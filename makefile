CXX ?= g++ #如果没有被赋值过就赋予等号后面的值

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp ./config/config.cpp webserver.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient
clean:
	rm	-r server