
// 构架：Client对象中有recvBuf接受缓存和sendQueue发送队列；
// 规定完整的消息帧格式：[len(4字节) 后续一共多少字节][type(1字节)][data(Buffer)]

#include <arpa/inet.h>
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <queue>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
using json   = nlohmann::json;
using DATA   = unsigned char;
using Buffer = std::vector<DATA>;
enum class TYPE : DATA {
    GROUP   = 1, // 群聊文本 无json demo:"这是群聊消息"
    PRIVATE = 2, // 私聊文本 json demo:{"to":"123","msg":"hello"}
    CONTROL = 3, // 控制消息（登录，获取在线列表，失败类型）demo:{"cmd":"login",data:xxx}
                 //         login  get_list     error
    FILE_CTRL = 4, // 文件传输控制消息（发文件请求，同意接受，拒绝）
                   // demo:{"cmd":"file_send_req","to":"123","fileName":"xxx.txt","fileSize":12345}
    FILE_DATA = 5, // 二进制文件数据
};


static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct OutChunk {
    Buffer data;    // 发送数据缓存
    size_t off = 0; // 已发送偏移
};

struct Client {
    int                  fd   = -1;
    unsigned int         id   = 0;           // 唯一ID（未登录则ID为0）
    std::string          peer = "";          // ip:port
    Buffer               recvBuf;            // 接受缓存
    std::queue<OutChunk> sendQueue;          // 发送队列
    bool                 isEpollOut = false; // 是否已监听 EPOLLOUT
    std::string          userName;           // 用户名
};
using Clients = std::unordered_map<int, Client>;
std::unordered_map<unsigned int, int> id2fd;   // 私聊路由
std::unordered_map<int, Client>       clients; // 在线客户端列表 fd -> Client
//  clients[id2fd[id]] -> 要发送到的客户端

/// @brief 从消息指针构造帧
/// @param data 消息指针
/// @param type 消息类型
/// @param messLen 实际消息长度(不包括前五格式字节)
/// @return 完整的一帧数据
Buffer packFrame(const DATA* data, DATA type, int messLen)
{
    if (messLen < 0) return {};
    Buffer   res(messLen + 5); // 总长度messLen + 5
    uint32_t transLen = htonl(1u + messLen);
    memcpy(res.data(), &transLen, 4);
    memcpy(res.data() + 4, &type, 1);
    if (messLen > 0) memcpy(res.data() + 5, data, messLen);
    return res;
}

/// @brief 将帧推送到客户端的发送队列
/// @param epollfd
/// @param c 客户端对象
/// @param frame 数据帧
void pushSendQueue(int epollfd, Client& c, const Buffer& frame)
{
    c.sendQueue.push({(frame), 0});
    epoll_event event{};
    event.data.fd = c.fd;
    event.events  = EPOLLIN | EPOLLOUT;
    if (!c.isEpollOut) {
        epoll_ctl(epollfd, EPOLL_CTL_MOD, c.fd, &event);
        c.isEpollOut = true;
    }
};

/// @brief 清理客户端
bool doClose(int epollfd, int fd, Clients& clients)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);

    auto it = clients.find(fd);
    if (it != clients.end()) {
        int cid = it->second.id;
        fprintf(stdout, "client closed: id=%d %s fd=%d\n", cid, it->second.peer.c_str(), fd);
        clients.erase(it);
        id2fd.erase(cid);
    }
    return true;
}

/// @brief 发送客户端c发送队列中的数据（非阻塞，不保证发完）
/// @param epfd
/// @param c 要发送数据的客户端
/// @param clients 在线客户端列表
void writeEvent(int epfd, Client& c, std::unordered_map<int, Client>& clients)
{
    while (!c.sendQueue.empty()) {
        void* psend       = c.sendQueue.front().data.data() + c.sendQueue.front().off;
        int   wantSendLen = c.sendQueue.front().data.size() - c.sendQueue.front().off;
        int   n           = ::send(c.fd, psend, wantSendLen, 0);
        if (n > 0) {
            c.sendQueue.front().off += n;
            if (c.sendQueue.front().off == c.sendQueue.front().data.size()) { c.sendQueue.pop(); }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 发不动了，等下一次 EPOLLOUT
            break;
        }
        if (n == 0 || n < 0) {
            std::cerr << "send error,client = " << c.peer << " " << c.fd << std::endl;
            doClose(epfd, c.fd, clients);
            return;
        }
    }
    if (clients.find(c.fd) != clients.end() && c.sendQueue.empty() && c.isEpollOut) {
        c.isEpollOut = false;
        epoll_event event{};
        event.data.fd = c.fd;
        event.events  = EPOLLIN;
        epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &event);
    }
}

/// @brief 从客户端的接收缓冲recvBuf内尝试解析一条完整消息，成功则返回 true
/// 并填充message同时消费输入缓冲；失败则返回 false，输入缓冲保持不变
/// @param recvBuf 客户端输入缓冲，可能完整消息/多条消息/半条消息
/// @param type 消息类型
/// @param message 消息
/// @param messLen 消息长度
/// @return
bool tryParse(Buffer& recvBuf, DATA& type, Buffer& message, int& messLen)
{
    if (recvBuf.size() < 5) return false;
    uint32_t transLen = 0; // 取出len
    memcpy(&transLen, recvBuf.data(), 4);
    int len = ntohl(transLen);
    if (len < 1 || len > 100 * 1024 * 1024) return false;
    if (recvBuf.size() < len + 4) return false; // 分包
    type    = recvBuf[4];
    messLen = len - 1;
    message = (messLen ? Buffer(recvBuf.data() + 5, recvBuf.data() + 5 + messLen) : Buffer{});
    // 消费
    recvBuf.erase(recvBuf.begin(), recvBuf.begin() + (long)(len + 4));
    return true;
}


/// @brief 接受数据(非阻塞)并放入客户端的接收缓冲
/// @param epfd
/// @param c 要发送数据的客户端
/// @param clients 在线客户端列表
bool readEvent(int epfd, Client& c, Clients& clients)
{
    DATA tmp[4096];

    while (true) {
        int n = ::recv(c.fd, tmp, sizeof(tmp), 0);
        if (n > 0) { c.recvBuf.insert(c.recvBuf.end(), tmp, tmp + n); }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { break; }
        if (n == 0 || n < 0) {
            std::cerr << "recv error or client close,client = " << c.peer << " " << c.fd
                      << std::endl;
            doClose(epfd, c.fd, clients);
            return false;
        }
    }
    return true;
    // while (true) {
    //     DATA  type;
    //     DATA* message;
    //     int   messLen;
    //     if (!tryParse(c.recvBuf, type, message, messLen)) break;
    // }
}

/// @brief 将 frame 广播给除 sender_fd 外的所有在线客户端
/// @param epfd epoll fd
/// @param clients 在线客户端列表
/// @param sender_fd 发送端
/// @param frame 要广播的完整帧
static void broadcast(int epfd, std::unordered_map<int, Client>& clients, int sender_fd,
                      Buffer& frame)
{
    for (auto& clent : clients) {
        if (clent.first == sender_fd) continue;
        pushSendQueue(epfd, clent.second, frame);
    }
}
int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\nExample: %s 9000\n", argv[0], argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    int listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(listenfd, 128) != 0) {
        perror("listen");
        return 1;
    }
    if (set_nonblock(listenfd) != 0) {
        perror("set_nonblock listenfd");
        return 1;
    }

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }

    epoll_event ev{};
    ev.data.fd = listenfd;
    ev.events  = EPOLLIN; // LT
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) != 0) {
        perror("epoll_ctl ADD listenfd");
        return 1;
    }


    fprintf(stdout, "server listen on 0.0.0.0:%d (epoll LT, nonblock)\n", port);

    const int   MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            int      fd = events[i].data.fd;
            uint32_t e  = events[i].events;

            if (fd == listenfd) {
                while (true) {
                    sockaddr_in peer{};
                    socklen_t   len = sizeof(peer);
                    int         cfd = accept(listenfd, (sockaddr*)&peer, &len);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }
                    set_nonblock(cfd);

                    char ip[64];
                    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                    uint16_t p = ntohs(peer.sin_port);

                    Client c;
                    c.fd       = cfd;
                    c.id       = 0; // 未登录状态ID为0
                    c.userName = "";
                    c.peer     = std::string(ip) + ":" + std::to_string(p);

                    epoll_event cev{};
                    cev.data.fd = cfd;
                    cev.events  = EPOLLIN;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev) != 0) {
                        perror("epoll_ctl ADD conn");
                        close(cfd);
                        continue;
                    }

                    clients.emplace(cfd, std::move(c));

                    // std::string hi = "welcome, your id=" + std::to_string(clients[cfd].id);
                    // push_outq(epfd, clients[cfd],
                    //           pack_frame(GROUP, (const unsigned char*)hi.data(),
                    //                      (uint32_t)hi.size()));
                }
                continue;
            }
            if (e & EPOLLIN) {
                auto it = clients.find(fd); // 有来自客户端it的事件
                if (it == clients.end()) continue;
                if (!readEvent(epfd, it->second, clients)) { continue; }
                while (true) {
                    DATA   type;
                    Buffer dataBuffer;
                    int    dataLen = 0;
                    if (!tryParse(it->second.recvBuf, type, dataBuffer, dataLen)) break;
                    if (type == (DATA)TYPE::GROUP) {
                        std::string msg((const char*)dataBuffer.data(),
                                        (const char*)dataBuffer.data() + dataLen);
                        fprintf(stdout, "[%llu][%s] %s\n", (unsigned long long)it->second.id,
                                it->second.peer.c_str(), msg.c_str());
                        std::string out = "[" + std::to_string(it->second.id) + "] " + msg;
                        auto frame = packFrame((DATA*)out.data(), (DATA)TYPE::GROUP, out.size());
                        broadcast(epfd, clients, fd, frame);
                    }
                    else if (type == (DATA)TYPE::PRIVATE) {
                        std::string datas((const char*)dataBuffer.data(), dataLen);
                        json        j;
                        try {
                            j = json::parse(datas);
                        }
                        catch (...) {
                            perror("error to parse json");
                            continue;
                        }
                        // c -> s {"cmd":"chat","to":"123","msg":"hello"}
                        // s -> c {"cmd":"chat","from":"245","msg":"hello"}
                        int         to_id = std::stoi(j["to"].get<std::string>());
                        auto        to_fd = id2fd.find(to_id);
                        std::string outs;
                        if (to_fd == id2fd.end() || clients.find(to_fd->second) == clients.end()) {
                            perror("error to find to id");
                            continue;
                        }
                        else {
                            std::string msg = j["msg"].get<std::string>();
                            json        out;
                            out["cmd"]  = "chat";
                            out["from"] = std::to_string(it->second.id);
                            out["msg"]  = std::move(msg);
                            outs        = out.dump();
                        }
                        Client& to_it = clients[to_fd->second];
                        auto    frame =
                            packFrame((DATA*)outs.data(), (DATA)TYPE::PRIVATE, outs.size());
                        pushSendQueue(epfd, to_it, frame);
                    }
                    else if (type == (DATA)TYPE::CONTROL) {
                        std::string datas((const char*)dataBuffer.data(), dataLen);
                        json        j;
                        try {
                            j = json::parse(datas);
                        }
                        catch (...) {
                            continue;
                        }
                        std::string cmd = j.value("cmd", "");
                        if (cmd == "") continue;
                        if (cmd == "login") {
                            unsigned int want_id = stoul(j["id"].get<std::string>());
                            std::string  name    = j.value("name", "");
                            json         ack;
                            ack["cmd"] = "login_ack";
                            if (it->second.id != 0 || id2fd.find(want_id) != id2fd.end()) {
                                ack["ok"]     = false;
                                ack["reason"] = "already_login or id_in_use";
                            }
                            else if (want_id == 0 || name.empty()) {
                                // 客户端本身也要检查一次有效性
                                ack["ok"]     = false;
                                ack["reason"] = "bad_param";
                            }
                            else {
                                it->second.id       = want_id;
                                it->second.userName = name;
                                id2fd[want_id]      = it->second.fd;
                                ack["ok"]           = true;
                                ack["id"]           = want_id;
                                ack["name"]         = name;
                                id2fd[want_id]      = it->second.fd;
                                fprintf(stdout, "client login: id=%d name=%s fd=%d (online=%zu)\n",
                                        it->second.id, it->second.userName.c_str(), it->second.fd,
                                        clients.size());
                            }

                            std::string outs = ack.dump();
                            auto frame = packFrame((const DATA*)outs.data(), (DATA)TYPE::CONTROL,
                                                   (int)outs.size());
                            pushSendQueue(epfd, it->second, frame);
                            continue;
                        }
                        if (cmd == "get_list") {
                            json out;
                            out["cmd"]   = "list";
                            out["users"] = json::array();
                            for (auto& kv : clients) {
                                const Client& c = kv.second;
                                if (c.id == 0) continue; // 没登录的不返回
                                out["users"].push_back({{"id", c.id}, {"name", c.userName}});
                            }
                            std::string outs = out.dump();
                            auto frame = packFrame((const DATA*)outs.data(), (DATA)TYPE::CONTROL,
                                                   (int)outs.size());
                            pushSendQueue(epfd, it->second, frame);
                            continue;
                        }
                    }
                    else if (type == (DATA)TYPE::FILE_CTRL) {
                        // 文件传输控制消息（发文件请求，同意接受，拒绝）
                        // demo:{"cmd":"file_send_req","to":"123","fileName":"xxx.txt","fileSize":12345}
                        std::string datas((const char*)dataBuffer.data(), dataLen);
                        json        j   = json::parse(datas);
                        std::string cmd = j.value("cmd", "");
                        if (cmd == "file_send_req") {
                            int         to_id = std::stoi(j["to"].get<std::string>());
                            auto        to_fd = id2fd.find(to_id);
                            std::string outs;
                            if (to_fd == id2fd.end() ||
                                clients.find(to_fd->second) == clients.end()) {
                                perror("error to find to id");
                                // 发送 失败消息
                                json out;
                                out["cmd"]    = "error";
                                out["reason"] = "未找到该用户或对方不在线";
                                outs          = out.dump();
                                auto frame    = packFrame((const DATA*)outs.data(),
                                                          (DATA)TYPE::CONTROL, (int)outs.size());
                                pushSendQueue(epfd, it->second, frame);
                                continue;
                            }
                            else {
                                std::string fileName = j["fileName"].get<std::string>();
                                int         fileSize = j["fileSize"].get<int>();
                                json        out;
                                out["cmd"]      = "file_send_req";
                                out["from"]     = std::to_string(it->second.id);
                                out["fileName"] = fileName;
                                out["fileSize"] = fileSize;
                                out["taskId"]   = j["taskId"];
                                outs            = out.dump();
                                auto frame      = packFrame((const DATA*)outs.data(),
                                                            (DATA)TYPE::FILE_CTRL, (int)outs.size());
                                pushSendQueue(epfd, clients[to_fd->second], frame);
                            }
                        }
                        else if (cmd == "file_send_resp") {
                            // {"cmd":"file_send_resp","to":"123","from":"234","fileName":"xxx.txt","fileSize":12345,"accept":true}
                            int  to_id = std::stoi(j["to"].get<std::string>()); // 文件发送者
                            auto to_fd = id2fd.find(to_id);
                            std::string outs;
                            if (to_fd == id2fd.end() ||
                                clients.find(to_fd->second) == clients.end()) {
                                perror("error to find to id");
                                continue;
                            }
                            else {
                                std::string fileName = j["fileName"].get<std::string>();
                                int         fileSize = j["fileSize"].get<int>();
                                bool        accept   = j["accept"].get<bool>();
                                json        out;
                                out["cmd"]      = "file_send_resp";
                                out["from"]     = std::to_string(it->second.id); // 文件接收者
                                out["fileName"] = fileName;
                                out["fileSize"] = fileSize;
                                out["accept"]   = accept;
                                out["taskId"]   = j["taskId"];
                                outs            = out.dump();
                                auto frame      = packFrame((const DATA*)outs.data(),
                                                            (DATA)TYPE::FILE_CTRL, (int)outs.size());
                                pushSendQueue(epfd, clients[to_fd->second], frame);
                            }
                        }
                        else if (cmd == "file_send_deny") {
                            if (!j.contains("to")) continue;
                            int         to_id = std::stoi(j["to"].get<std::string>());
                            auto        to_fd = id2fd.find(to_id);
                            std::string outs;
                            if (to_fd != id2fd.end() &&
                                clients.find(to_fd->second) != clients.end()) {
                                json out;
                                out["cmd"]        = "file_send_deny";
                                out["from"]       = std::to_string(it->second.id);
                                out["reason"]     = "deny";
                                out["taskId"]     = j["taskId"];
                                std::string outs  = out.dump();
                                auto        frame = packFrame((const DATA*)outs.data(),
                                                              (DATA)TYPE::FILE_CTRL, (int)outs.size());
                                pushSendQueue(epfd, clients[to_fd->second], frame);
                            }
                        }
                    }
                    else if (type == (DATA)TYPE::FILE_DATA) {
                        // [二进制4字节 ID 客户端发过来是to_id
                        // 接受二进制改成from_id][任务ID+64KB文件数据,分段传输]
                        // 服务器不关心任务ID，UI那边处理就行，4字节，为了多文件
                        if (dataLen < 4) continue;
                        uint32_t net_id;
                        memcpy(&net_id, dataBuffer.data(), 4);
                        uint32_t to_id = ntohl(net_id);
                        auto     to_fd = id2fd.find(to_id);
                        if (to_fd != id2fd.end() && clients.find(to_fd->second) != clients.end()) {
                            int netFrom = htonl(it->second.id);
                            memcpy(dataBuffer.data(), &netFrom, 4);
                            auto frame = packFrame(dataBuffer.data(), (DATA)TYPE::FILE_DATA,
                                                   dataBuffer.size());
                            pushSendQueue(epfd, clients[to_fd->second], frame);
                        }
                    }
                }
            }
            if (e & EPOLLOUT) {
                auto it = clients.find(fd);
                if (it != clients.end()) { writeEvent(epfd, it->second, clients); }
            }
        }
    }
    close(epfd);
    close(listenfd);
    return 0;
}
