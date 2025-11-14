#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024
#define SERVER_IP "123.249.105.161"  // 服务器IP123.249.105.161，本地测试用127.0.0.1
#define PORT 8888

int client_socket;
char username[50];
char target_username[50] = "";  // 当前选择的私聊对象
pthread_mutex_t target_mutex = PTHREAD_MUTEX_INITIALIZER;
char last_online_list[BUFFER_SIZE] = "";  // 记录上一次在线列表

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
            // 普通消息或系统提示
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

// 显示私聊帮助信息
void print_private_help() {
    printf("\n===== 私聊操作说明 =====\n");
    printf("1. 选择聊天对象：输入在线用户列表中的序号（如1）\n");
    printf("2. 发送消息：直接输入内容（发给当前选择的用户）\n");
    printf("3. 临时切换对象：@目标用户:消息（如@user2:你好）\n");
    printf("4. 退出聊天室：输入 /exit 并回车\n");
    printf("5. 返回功能选择：输入 /back 并回车\n");
    printf("====================\n\n");
}

// 群聊功能菜单
void group_chat_menu() {
    char buffer[BUFFER_SIZE];
    while (1) {
        printf("\n===== 群聊功能 =====\n");
        printf("1. 创建群聊\n");
        printf("2. 查看群列表\n");
        printf("3. 加入群聊\n");
        printf("4. 进入群聊（发送消息）\n");
        printf("5. 返回上一级\n");
        printf("请选择操作（1-5）：");
        fflush(stdout);

        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';

        // 1. 创建群聊
        if (strcmp(buffer, "1") == 0) {
            printf("请输入群聊名称：");
            fflush(stdout);
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = '\0';
            char create_cmd[BUFFER_SIZE];
            snprintf(create_cmd, BUFFER_SIZE, "/create_group %s", buffer);
            send(client_socket, create_cmd, strlen(create_cmd), 0);
            
            // 接收创建结果
            recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            printf("%s\n", buffer);
        }
        // 2. 查看群列表
        else if (strcmp(buffer, "2") == 0) {
            send(client_socket, "/get_group_list", strlen("/get_group_list"), 0);
            recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            printf("\n%s\n", buffer);
        }
        // 3. 加入群聊
        else if (strcmp(buffer, "3") == 0) {
            printf("请输入要加入的群ID：");
            fflush(stdout);
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = '\0';
            char join_cmd[BUFFER_SIZE];
            snprintf(join_cmd, BUFFER_SIZE, "/join_group %s", buffer);
            send(client_socket, join_cmd, strlen(join_cmd), 0);
            
            // 接收加入结果
            recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            printf("%s\n", buffer);
        }
        // 4. 进入群聊（发送消息）
        else if (strcmp(buffer, "4") == 0) {
            printf("请输入要聊天的群ID：");
            fflush(stdout);
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = '\0';
            int group_id = atoi(buffer);
            printf("已进入群ID：%d 的群聊（输入/back返回菜单，输入#群ID:消息可切换群聊）\n", group_id);
            printf("%s（群聊）: ", username);
            fflush(stdout);

            // 群聊消息发送循环
            while (1) {
                fgets(buffer, BUFFER_SIZE, stdin);
                buffer[strcspn(buffer, "\n")] = '\0';
                if (strcmp(buffer, "/back") == 0) break;
                if (strcmp(buffer, "/exit") == 0) {
                    send(client_socket, buffer, strlen(buffer), 0);
                    exit(EXIT_SUCCESS);
                }

                // 格式化群聊消息
                char group_msg[BUFFER_SIZE];
                snprintf(group_msg, BUFFER_SIZE, "#%d:%s", group_id, buffer);
                send(client_socket, group_msg, strlen(group_msg), 0);
                printf("%s（群聊）: ", username);
                fflush(stdout);
            }
        }
        // 5. 返回上一级
        else if (strcmp(buffer, "5") == 0) {
            break;
        } else {
            printf("输入错误，请重新选择\n");
        }
    }
}

// 私聊功能处理
void private_chat() {
    char buffer[BUFFER_SIZE];
    print_private_help();

    while (1) {
        // 读取用户输入
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;
        buffer[strcspn(buffer, "\n")] = '\0';

        // 退出命令
        if (strcmp(buffer, "/exit") == 0) {
            send(client_socket, buffer, strlen(buffer), 0);
            break;
        }

        // 返回功能选择
        if (strcmp(buffer, "/back") == 0) {
            pthread_mutex_lock(&target_mutex);
            strcpy(target_username, "");  // 清空当前目标
            pthread_mutex_unlock(&target_mutex);
            break;
        }

        // 处理选择聊天对象（输入序号）
        int target_idx;
        if (sscanf(buffer, "%d", &target_idx) == 1) {
            // 提示用户输入序号对应的用户名
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
            snprintf(send_buf, BUFFER_SIZE, "%s:%s", target_username, buffer);
            send(client_socket, send_buf, strlen(send_buf), 0);
        } else {
            // 临时指定目标
            send(client_socket, buffer, strlen(buffer), 0);
        }
        pthread_mutex_unlock(&target_mutex);
    }
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

    // 功能选择循环
    while (1) {
        printf("\n===== 功能选择 =====\n");
        printf("1. 个人聊天（私聊）\n");
        printf("2. 群聊功能\n");
        printf("3. 退出聊天室\n");
        printf("请选择功能（1-3）：");
        fflush(stdout);

        fgets(choice, 10, stdin);
        choice[strcspn(choice, "\n")] = '\0';

        if (strcmp(choice, "1") == 0) {
            private_chat();  // 进入私聊模式
        } else if (strcmp(choice, "2") == 0) {
            group_chat_menu();  // 进入群聊菜单
        } else if (strcmp(choice, "3") == 0) {
            send(client_socket, "/exit", strlen("/exit"), 0);
            break;
        } else {
            printf("输入错误，请重新选择\n");
        }
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