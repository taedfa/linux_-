#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024
#define SERVER_IP "123.249.105.161"
#define PORT 8888

int client_socket;
char username[50];
char target_username[50] = "";  // 当前选择的聊天对象
pthread_mutex_t target_mutex = PTHREAD_MUTEX_INITIALIZER;
char last_online_list[BUFFER_SIZE] = "";  // 记录上一次在线列表，避免重复显示

// 解析在线用户列表（仅在列表变化时显示）
void parse_online_users(const char *list) {
    if (strstr(list, "在线用户列表：") == NULL) return;

    // 列表未变化则不显示
    if (strcmp(list, last_online_list) == 0) return;
    strcpy(last_online_list, list);

    printf("\r\033[K===== 在线用户 =====\n");
    char temp[BUFFER_SIZE];
    strcpy(temp, list);
    char *line = strtok(temp, "\n");
    line = strtok(NULL, "\n");  // 跳过第一行"在线用户列表："

    while (line != NULL) {
        printf("%s\n", line);
        line = strtok(NULL, "\n");
    }

    // 提示选择聊天对象
    pthread_mutex_lock(&target_mutex);
    if (strlen(target_username) == 0) {
        printf("====================\n");
        printf("请输入目标用户序号选择聊天对象：");
        fflush(stdout);
    } else {
        printf("====================\n");
        printf("当前聊天对象：%s（输入序号切换，输入@用户名:消息临时指定）\n", target_username);
        printf("%s: ", username);
        fflush(stdout);
    }
    pthread_mutex_unlock(&target_mutex);
}

// 接收消息线程
void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';

        // 区分在线列表和普通消息
        if (strstr(buffer, "在线用户列表：") != NULL) {
            parse_online_users(buffer);  // 仅在列表变化时显示
        } else {
            // 普通消息或发送确认
            pthread_mutex_lock(&target_mutex);
            printf("\r\033[K%s\n", buffer);
            if (strlen(target_username) > 0) {
                printf("%s: ", username);
            }
            fflush(stdout);
            pthread_mutex_unlock(&target_mutex);
        }
    }

    printf("\n与服务器断开连接\n");
    close(client_socket);
    exit(EXIT_SUCCESS);
    return NULL;
}

// 显示帮助信息
void print_help() {
    printf("\n===== 操作说明 =====\n");
    printf("1. 选择聊天对象：输入在线用户列表中的序号（如1）\n");
    printf("2. 发送消息：直接输入内容（发给当前选择的用户）\n");
    printf("3. 临时切换对象：@目标用户:消息（如@user2:你好）\n");
    printf("4. 退出聊天室：输入 /exit 并回车\n");
    printf("====================\n\n");
}

int main() {
    struct sockaddr_in server_addr;
    pthread_t recv_thread;
    char buffer[BUFFER_SIZE], password[50], choice[10];
    int bytes_read;

    // 创建socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket创建失败");
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("服务器IP无效");
        exit(EXIT_FAILURE);
    }

    // 连接服务器
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("连接服务器失败");
        exit(EXIT_FAILURE);
    }

    // 登录/注册流程
    printf("===== 聊天室登录/注册 =====\n");
    
    // 接收操作提示
    bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        perror("接收提示失败");
        close(client_socket);
        return 0;
    }
    buffer[bytes_read] = '\0';
    printf("%s\n", buffer);
    fflush(stdout);

    // 输入操作选择
    fgets(choice, 10, stdin);
    choice[strcspn(choice, "\n")] = '\0';
    send(client_socket, choice, strlen(choice), 0);

    // 注册流程
    if (strcmp(choice, "1") == 0) {
        // 接收用户名提示
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) goto exit_err;
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
        fflush(stdout);

        // 输入用户名
        fgets(username, 50, stdin);
        username[strcspn(username, "\n")] = '\0';
        send(client_socket, username, strlen(username), 0);

        // 接收密码提示
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) goto exit_err;
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
        fflush(stdout);

        // 输入密码
        fgets(password, 50, stdin);
        password[strcspn(password, "\n")] = '\0';
        send(client_socket, password, strlen(password), 0);

        // 接收注册结果
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) goto exit_err;
        buffer[bytes_read] = '\0';
        printf("%s\n", buffer);
        if (strstr(buffer, "成功") == NULL) {
            close(client_socket);
            return 0;
        }
    }
    // 登录流程
    else if (strcmp(choice, "2") == 0) {
        // 接收用户名提示
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) goto exit_err;
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
        fflush(stdout);

        // 输入用户名
        fgets(username, 50, stdin);
        username[strcspn(username, "\n")] = '\0';
        send(client_socket, username, strlen(username), 0);

        // 接收密码提示
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) goto exit_err;
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
        fflush(stdout);

        // 输入密码
        fgets(password, 50, stdin);
        password[strcspn(password, "\n")] = '\0';
        send(client_socket, password, strlen(password), 0);

        // 接收登录结果
        bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) goto exit_err;
        buffer[bytes_read] = '\0';
        printf("%s\n", buffer);
        if (strstr(buffer, "成功") == NULL) {
            close(client_socket);
            return 0;
        }
    } else {
        printf("输入错误，退出程序\n");
        close(client_socket);
        return 0;
    }

    // 启动接收消息线程
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        perror("创建接收线程失败");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // 显示帮助信息
    print_help();

    // 聊天消息发送循环
    while (1) {
        // 读取用户输入
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;
        buffer[strcspn(buffer, "\n")] = '\0';

        // 退出命令
        if (strcmp(buffer, "/exit") == 0) {
            send(client_socket, buffer, strlen(buffer), 0);
            break;
        }

        // 处理选择聊天对象（输入序号）
        int target_idx;
        if (sscanf(buffer, "%d", &target_idx) == 1) {
            // 提示用户根据在线列表输入序号对应的用户名
            printf("请输入序号 %d 对应的用户名：", target_idx);
            fflush(stdout);
            fgets(target_username, 50, stdin);
            target_username[strcspn(target_username, "\n")] = '\0';
            printf("已切换聊天对象为：%s\n", target_username);
            printf("%s: ", username);
            fflush(stdout);
            continue;
        }

        // 发送消息（自动拼接当前目标用户）
        pthread_mutex_lock(&target_mutex);
        if (strlen(target_username) > 0 && buffer[0] != '@') {
            char send_buf[BUFFER_SIZE];
            snprintf(send_buf, BUFFER_SIZE, "%.*s:%.*s",
                     49, target_username,
                     BUFFER_SIZE - 51, buffer);  // 限制长度避免溢出
            send(client_socket, send_buf, strlen(send_buf), 0);
        } else {
            // 临时指定目标
            char send_buf[BUFFER_SIZE];
            snprintf(send_buf, BUFFER_SIZE, "%.*s", BUFFER_SIZE - 1, buffer);
            send(client_socket, send_buf, strlen(send_buf), 0);
        }
        pthread_mutex_unlock(&target_mutex);
    }

    // 退出清理
    close(client_socket);
    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    return 0;

exit_err:
    perror("与服务器通信异常");
    close(client_socket);
    return 0;
}
