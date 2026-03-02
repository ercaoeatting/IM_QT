# Qt IM
## 编译
服务端：g++ -o server server.cpp即可，其中，解析json使用了nlohmann-json库，需要系统有nlohmann-json库
客户端：为QT 6.8 项目，项目文件为IM.pro
## 服务端
服务端是一个单线程的LT epoll，每个 Client 维护 recvBuf（粘包/拆包缓存）+sendQueue（发送队列），事件驱动。通过clients: fd -> Client  id2fd: userId -> fd两个路由表实现用户管理,自定义消息帧格式如下说明了，打包帧，取帧和帧类型的逻辑和客户端一样。


##  客户端架构
客户端整体上算是一个MVC架构
M： m_client->m_history。聊天数据只从这里拿。
m_taskId2FileTasks 文件传输状态
V： createSingleMessageWidget。负责根据text+timestr画气泡
C： 主要是主窗口类switchChat 和 on_buttonSend_clicked等。负责从 Model 拿数据，交给 View 去画，或者将 View 的用户操作（如点击“接收/取消”按钮）转化为底层网络请求



## 模块说明
关键模块：
1. **`ChatClient`** (网络层)**：负责维护 TCP 长连接、自定义协议封包/拆包、JSON 数据解析，以及全局聊天记录（`m_history`）的维护。
2. **`Dialog`** (登录层)：负责应用启动时的用户身份验证、服务器参数配置及本地偏好设置（持久化）。
3. **`MainWindow`** (主界面)：负责在线用户列表更新、群聊/私聊状态切换，以及聊天气泡 UI 渲染。
4. **`FileDialog`** (文件层)：负责文件发送/接收的状态管理、进度更新，以及与服务器的文件传输协议交互。
### 网络层
负责网络，并与服务器通信协议匹配。
- **自定义二进制帧协议** &#x20;
  为了解决 TCP 的“粘包”与“半包”问题，自定义帧协议：
  - **4 字节 (Length)**：Payload 长度 + 1（+1类型字节），网络字节序传输。
  - **1 字节 (Type)**：enum class `TYPE`(uchar) (1: GROUP, 2: PRIVATE, 3: CONTROL ... )。
  - **N 字节 (Payload)**：实际数据(二进制(文件)，纯文本(群聊文本)或 JSON(控制消息和私聊))。
  - **核心实现**：`packFrame`负责封包，`tryParse`负责在`onReadyRead` 的缓冲区安全拆包，没拆出来返回false 服务端和客户端类似操作

- **Model**`std::unordered_map<uint32_t, std::vector<std::pair<QString, QString>>> m_history;`集中管理所有聊天记录，键为对方 ID（群聊为`9999`），值为时间戳+格式化后的消息列表的pair。

### 登录层

- **生命周期控制**在`main.cpp`中，程序先以阻塞方式`dlg.exec()`弹出登录框。只有在登录对话框返回`QDialog::Accepted`时，才会实例化并显示`MainWindow`
- **异步登录**由于网络连接是异步的，`Dialog` 中的登录按钮并不会直接进入主页面，而是：
  1. 触发`m_client->connectTo()`
  2. 监听`connected`信号并发送`login` 命令 JSON。
  3. 监听`loginAck`信号，根据服务器返回的`ok`决定是否`accept()` 关闭对话框并进入主界面。
  4. 期间利用`QProgressDialog` 提供模态的“正在登录...”反馈。
- **本地偏好存储**使用`QSettings` 将上次登录成功的 IP、端口、账号信息序列化到本地（注册表或配置文件），提升用户体验。

### 主界面
- **在线用户列表**通过监听信号，实时更新左侧在线列表
- **聊天状态切换**点击在线用户列表项，触发`switchChat(peerId)`，更新当前聊天对象，并从`m_history`加载对应的聊天记录。
- **聊天气泡 UI**`createSingleMessageWidget`根据消息内容和时间戳创建一个包含信息栏（发送者+时间）和消息气泡的 QWidget，添加到聊天区的布局中。
- **消息发送接受** 信号触发时，调用网络层方法，更新历史记录并显示

### 文件传输
FileDialog 维护一个std::unordered_map<QString, FileDialog::FileTask> m_fileTasks;，k:任务 ID，值为文件传输状态结构（包括文件名、总大小、已传大小、当前状态等）。任务 ID是唯一的，为了实现多文件。
发送时挂载一个QTimer，每隔1ms切64KB数据扔到网络层，打散到QT事件循环中，防止UI卡。
取消，拒绝，完成都相同地管理：销毁QTimer，关闭QFile，更新状态和UI。



