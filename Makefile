# 定义编译器和编译选项
CC = gcc
CFLAGS = -Wall -Wextra -lpthread  # -Wall 显示警告，-lpthread 链接线程库
PORT = 8888  # 端口号（与代码中一致）

# 目标文件：同时编译服务器和客户端
all: server client

# 编译服务器端
server: server.c
	$(CC) server.c -o server $(CFLAGS)
	@echo "服务器编译完成：./server"

# 编译客户端
client: client.c
	$(CC) client.c -o client $(CFLAGS)
	@echo "客户端编译完成：./client"

# 清理编译产物（可执行文件）
clean:
	rm -f server client
	@echo "已清理可执行文件"

# 运行服务器（可选）
run_server:
	./server

# 运行客户端（可选）
run_client:
	./client
