#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define PORT 8888
#define USER_FILE "user.txt"
#define BROADCAST_INTERVAL 5  // 在线列表广播间隔（秒）
#define MAX_GROUPS 5          // 最大群数量

// 客户端结构体
typedef struct {
    int socket;
    char username[50];
    struct sockaddr_in address;
    int is_active;
} Client;

// 群聊结构体
typedef struct {
    int group_id;               // 群ID（唯一标识）
    char group_name[50];        // 群名称
    char members[10][50];       // 群成员用户名（最多10人）
    int member_count;           // 群成员数量
    int is_active;              // 群是否有效
} Group;

// 全局变量
Client clients[MAX_CLIENTS];
Group groups[MAX_GROUPS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t groups_mutex = PTHREAD_MUTEX_INITIALIZER;
int client_count = 0;
int group_count = 0;

// 检查用户名是否存在
int user_exists(const char *username) {
    FILE *fp = fopen(USER_FILE, "r");
    if (!fp) return 0;

    char line[BUFFER_SIZE], stored_user[50], stored_pwd[50];
    while (fgets(line, BUFFER_SIZE, fp)) {
        if (sscanf(line, "%49s %49s", stored_user, stored_pwd) == 2) {
            if (strcmp(stored_user, username) == 0) {
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

// 验证用户名密码
int verify_user(const char *username, const char *password) {
    FILE *fp = fopen(USER_FILE, "r");
    if (!fp) return 0;

    char line[BUFFER_SIZE], stored_user[50], stored_pwd[50];
    while (fgets(line, BUFFER_SIZE, fp)) {
        if (sscanf(line, "%49s %49s", stored_user, stored_pwd) == 2) {
            if (strcmp(stored_user, username) == 0 && strcmp(stored_pwd, password) == 0) {
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

// 注册用户
int register_user(const char *username, const char *password) {
    if (user_exists(username)) return 0;

    FILE *fp = fopen(USER_FILE, "a");
    if (!fp) return 0;

    fprintf(fp, "%s %s\n", username, password);
    fclose(fp);
    return 1;
}

// 查找客户端索引
int find_client_index(const char *username) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active && strcmp(clients[i].username, username) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

// 生成在线用户列表（去重）
void get_online_users(char *list) {
    pthread_mutex_lock(&clients_mutex);
    strcpy(list, "在线用户列表：\n");
    char temp[100];
    char added_users[MAX_CLIENTS][50] = {0};
    int added_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active) {
            int is_duplicate = 0;
            for (int j = 0; j < added_count; j++) {
                if (strcmp(added_users[j], clients[i].username) == 0) {
                    is_duplicate = 1;
                    break;
                }
            }
            if (!is_duplicate) {
                snprintf(temp, sizeof(temp), "%d. %s\n", added_count + 1, clients[i].username);
                strcat(list, temp);
                strcpy(added_users[added_count], clients[i].username);
                added_count++;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 广播在线用户列表（定时线程）
void *broadcast_online_users(void *arg) {
    while (1) {
        sleep(BROADCAST_INTERVAL);
        char user_list[BUFFER_SIZE];
        get_online_users(user_list);
        
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_active) {
                send(clients[i].socket, user_list, strlen(user_list), 0);
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    return NULL;
}

// 转发私聊消息
void forward_message(const char *message, const char *target_username, const char *sender_username, int sender_socket) {
    int target_index = find_client_index(target_username);
    if (target_index == -1) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "系统消息: 用户 %s 不存在或不在线", target_username);
        send(sender_socket, error_msg, strlen(error_msg), 0);
        return;
    }

    // 发送给目标用户
    char formatted_msg[BUFFER_SIZE];
    snprintf(formatted_msg, BUFFER_SIZE, "%s: %s", sender_username, message);
    send(clients[target_index].socket, formatted_msg, strlen(formatted_msg), 0);

    // 发送确认给发送者
    char confirm_msg[BUFFER_SIZE];
    //snprintf(confirm_msg, BUFFER_SIZE, "已发送给 %s: %s", target_username, message);
    //send(sender_socket, confirm_msg, strlen(confirm_msg), 0);
}

// 创建群聊
int create_group(const char *group_name, const char *creator) {
    pthread_mutex_lock(&groups_mutex);
    int group_idx = -1;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!groups[i].is_active) {
            group_idx = i;
            break;
        }
    }
    if (group_idx == -1) {
        pthread_mutex_unlock(&groups_mutex);
        return -1;  // 群数量达上限
    }

    // 初始化群信息
    groups[group_idx].group_id = group_idx + 1;  // 群ID从1开始
    strncpy(groups[group_idx].group_name, group_name, 49);  // 限制长度
    groups[group_idx].group_name[49] = '\0';
    strncpy(groups[group_idx].members[0], creator, 49);
    groups[group_idx].members[0][49] = '\0';
    groups[group_idx].member_count = 1;
    groups[group_idx].is_active = 1;
    group_count++;

    pthread_mutex_unlock(&groups_mutex);
    return groups[group_idx].group_id;
}

// 加入群聊
int join_group(int group_id, const char *username) {
    if (!user_exists(username)) return 0;

    pthread_mutex_lock(&groups_mutex);
    int group_idx = -1;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active && groups[i].group_id == group_id) {
            group_idx = i;
            break;
        }
    }
    if (group_idx == -1) {
        pthread_mutex_unlock(&groups_mutex);
        return 0;  // 群不存在
    }

    // 检查是否已在群内
    for (int i = 0; i < groups[group_idx].member_count; i++) {
        if (strcmp(groups[group_idx].members[i], username) == 0) {
            pthread_mutex_unlock(&groups_mutex);
            return 0;  // 已在群内
        }
    }

    // 加入群聊（不超过最大成员数）
    if (groups[group_idx].member_count >= 10) {
        pthread_mutex_unlock(&groups_mutex);
        return 0;  // 群成员满
    }
    strncpy(groups[group_idx].members[groups[group_idx].member_count], username, 49);
    groups[group_idx].members[groups[group_idx].member_count][49] = '\0';
    groups[group_idx].member_count++;

    pthread_mutex_unlock(&groups_mutex);
    return 1;
}

// 获取群列表
void get_group_list(char *list) {
    pthread_mutex_lock(&groups_mutex);
    strcpy(list, "群聊列表：\n");
    char temp[100];
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active) {
            snprintf(temp, sizeof(temp), "%d. 群ID：%d | 群名：%s | 成员数：%d\n",
                     i + 1, groups[i].group_id, groups[i].group_name, groups[i].member_count);
            strcat(list, temp);
        }
    }
    if (group_count == 0) {
        strcat(list, "暂无可用群聊\n");
    }
    pthread_mutex_unlock(&groups_mutex);
}

// 群聊消息广播
void broadcast_group_message(const char *message, int group_id, const char *sender_username) {
    pthread_mutex_lock(&groups_mutex);
    int group_idx = -1;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].is_active && groups[i].group_id == group_id) {
            group_idx = i;
            break;
        }
    }
    if (group_idx == -1) {
        pthread_mutex_unlock(&groups_mutex);
        return;
    }

    // 格式化消息
    char formatted_msg[BUFFER_SIZE];
    snprintf(formatted_msg, BUFFER_SIZE, "[群聊-%s] %s: %s",
             groups[group_idx].group_name, sender_username, message);

    // 发给群内所有在线成员
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < groups[group_idx].member_count; i++) {
        const char *member = groups[group_idx].members[i];
        for (int j = 0; j < MAX_CLIENTS; j++) {
            if (clients[j].is_active && strcmp(clients[j].username, member) == 0) {
                send(clients[j].socket, formatted_msg, strlen(formatted_msg), 0);
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    pthread_mutex_unlock(&groups_mutex);
}

// 处理客户端
void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE], username[50], password[50];
    int bytes_read, is_logged = 0;

    // 发送操作提示
    const char *operate_prompt = "请选择操作：1-注册 2-登录";
    send(client_socket, operate_prompt, strlen(operate_prompt), 0);

    // 接收操作选择
    bytes_read = recv(client_socket, buffer, 1, 0);
    if (bytes_read <= 0) goto exit_handle;
    buffer[bytes_read] = '\0';

    // 注册流程
    if (strcmp(buffer, "1") == 0) {
        send(client_socket, "请输入用户名：", strlen("请输入用户名："), 0);
        bytes_read = recv(client_socket, username, 49, 0);
        if (bytes_read <= 0) goto exit_handle;
        username[bytes_read] = '\0';

        send(client_socket, "请输入密码：", strlen("请输入密码："), 0);
        bytes_read = recv(client_socket, password, 49, 0);
        if (bytes_read <= 0) goto exit_handle;
        password[bytes_read] = '\0';

        if (register_user(username, password)) {
            send(client_socket, "注册成功！正在登录...", strlen("注册成功！正在登录..."), 0);
            is_logged = 1;
        } else {
            send(client_socket, "注册失败：用户名已存在", strlen("注册失败：用户名已存在"), 0);
        }
    }
    // 登录流程
    else if (strcmp(buffer, "2") == 0) {
        send(client_socket, "请输入用户名：", strlen("请输入用户名："), 0);
        bytes_read = recv(client_socket, username, 49, 0);
        if (bytes_read <= 0) goto exit_handle;
        username[bytes_read] = '\0';

        send(client_socket, "请输入密码：", strlen("请输入密码："), 0);
        bytes_read = recv(client_socket, password, 49, 0);
        if (bytes_read <= 0) goto exit_handle;
        password[bytes_read] = '\0';

        if (verify_user(username, password)) {
            send(client_socket, "登录成功！欢迎加入聊天室", strlen("登录成功！欢迎加入聊天室"), 0);
            is_logged = 1;
        } else {
            send(client_socket, "登录失败：用户名或密码错误", strlen("登录失败：用户名或密码错误"), 0);
        }
    } else {
        send(client_socket, "输入错误！请重新连接选择", strlen("输入错误！请重新连接选择"), 0);
    }

    // 登录成功后添加到客户端列表
    if (is_logged) {
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].is_active) {
                clients[i].socket = client_socket;
                strncpy(clients[i].username, username, 49);
                clients[i].username[49] = '\0';
                clients[i].is_active = 1;
                client_count++;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        printf("用户 %s 登录成功\n", username);
    }

    // 聊天消息处理
    while (is_logged && (bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        
        if (strcmp(buffer, "/exit") == 0) break;

        // 处理群聊相关命令
        char group_name[50], group_id_str[10];
        if (sscanf(buffer, "/create_group %[^\n]", group_name) == 1) {
            int group_id = create_group(group_name, username);
            char resp[BUFFER_SIZE];
            if (group_id != -1) {
                snprintf(resp, BUFFER_SIZE, "创建群聊成功！群ID：%d，群名：%s", group_id, group_name);
            } else {
                strcpy(resp, "创建群聊失败：群数量已达上限");
            }
            send(client_socket, resp, strlen(resp), 0);
            continue;
        } else if (strcmp(buffer, "/get_group_list") == 0) {
            char group_list[BUFFER_SIZE];
            get_group_list(group_list);
            send(client_socket, group_list, strlen(group_list), 0);
            continue;
        } else if (sscanf(buffer, "/join_group %s", group_id_str) == 1) {
            int group_id = atoi(group_id_str);
            int ret = join_group(group_id, username);
            char resp[BUFFER_SIZE];
            if (ret == 1) {
                snprintf(resp, BUFFER_SIZE, "加入群聊（ID：%d）成功", group_id);
            } else {
                strcpy(resp, "加入群聊失败：群不存在/已在群内/成员满/用户名未注册");
            }
            send(client_socket, resp, strlen(resp), 0);
            continue;
        }

        // 群聊消息格式：#群ID:消息
        int group_id;
        char group_msg[BUFFER_SIZE];
        if (sscanf(buffer, "#%d:%[^\n]", &group_id, group_msg) == 2) {
            broadcast_group_message(group_msg, group_id, username);
            continue;
        }

        // 私聊消息格式
        char target[50], msg[BUFFER_SIZE];
        if (sscanf(buffer, "@%49[^:]:%[^\n]", target, msg) == 2 || 
            sscanf(buffer, "%49[^:]:%[^\n]", target, msg) == 2) {
            forward_message(msg, target, username, client_socket);
        } else {
            send(client_socket, "消息格式错误！\n1. 私聊：@目标用户:消息 或 目标用户:消息\n2. 群聊：#群ID:消息", 
                 strlen("消息格式错误！\n1. 私聊：@目标用户:消息 或 目标用户:消息\n2. 群聊：#群ID:消息"), 0);
        }
    }

exit_handle:
    // 清理客户端连接
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == client_socket) {
            printf("用户 %s 已退出\n", clients[i].username);
            clients[i].is_active = 0;
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(client_socket);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id, broadcast_thread;

    // 初始化客户端列表
    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].is_active = 0;

    // 初始化群列表
    memset(groups, 0, sizeof(groups));
    for (int i = 0; i < MAX_GROUPS; i++) {
        groups[i].is_active = 0;
        groups[i].member_count = 0;
    }

    // 创建socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 端口复用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定+监听
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // 启动在线用户广播线程
    pthread_create(&broadcast_thread, NULL, broadcast_online_users, NULL);
    pthread_detach(broadcast_thread);

    printf("聊天室服务器已启动，端口：%d\n", PORT);

    // 接受连接
    while (1) {
        int *client_socket = malloc(sizeof(int));
        if ((*client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            perror("accept");
            free(client_socket);
            continue;
        }

        // 限制最大连接数
        pthread_mutex_lock(&clients_mutex);
        if (client_count >= MAX_CLIENTS) {
            send(*client_socket, "服务器已满，请稍后再试", strlen("服务器已满，请稍后再试"), 0);
            close(*client_socket);
            free(client_socket);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        pthread_mutex_unlock(&clients_mutex);

        // 创建线程处理客户端
        if (pthread_create(&thread_id, NULL, handle_client, client_socket) != 0) {
            perror("pthread_create");
            close(*client_socket);
            free(client_socket);
        }
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}