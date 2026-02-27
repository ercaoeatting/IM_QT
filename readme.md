# Qt IM

## 一、 架构总览

Model (数据层)： m_client->m_history。所有聊天记录的唯一存储地。

View (视图层)： createSingleMessageWidget。只负责“怎么画”，不关心数据怎么来。

Controller (控制层)： switchChat 和 on_buttonSend_clicked。负责从 Model 拿数据，交给 View 去画

整个项目的核心由三个关键模块组成：

1. **`ChatClient`** (核心网络与数据层)**：负责维护 TCP 长连接、自定义协议的封包/拆包、JSON 数据解析，以及全局聊天记录（`m_history`）的维护。它作为SSOT存在。
2. **`Dialog`** (登录与配置模块)：负责应用启动时的用户身份验证、服务器参数配置及本地偏好设置（持久化）。
3. **`MainWindow`** (主界面交互模块)：负责在线用户列表更新、群聊/私聊状态切换，以及聊天气泡 UI 渲染。

## 核心数据流向



```text 
[用户输入] -> MainWindow / Dialog
                   | (调用方法)
                   v
              ChatClient
                   | (封包/拆包)
                   v
             QTcpSocket (网络通信)
                   | (接收数据)
                   v
              ChatClient (解析更新 m_history，并发出 Signal)
                   | (Signals: privateMessageReceived, userListReceived 等)
                   v
              MainWindow (响应 Signal，读取 m_history 刷新 UI)
```


***

## 二、 说明

## 1. 协议与网络通信 (`ChatClient`)

负责网络，并与服务器通信协议匹配。

- **自定义二进制帧协议** &#x20;
  为了解决 TCP 的“粘包”与“半包”问题，实现了一套轻量级的帧协议：
  - **4 字节 (Length)**：Payload 长度 + 1（包含类型字节），网络字节序（大端）传输。
  - **1 字节 (Type)**：枚举`TYPE` (1: GROUP, 2: PRIVATE, 3: CONTROL ... )。
  - **N 字节 (Payload)**：实际数据（二进制，纯文本或 JSON 格式）。
  - **核心实现**：`packFrame`负责封包（`qToBigEndian`），`tryParse`负责在`onReadyRead` 的缓冲区中安全拆包，确保数据接收完整才向下分发。
- **业务数据解析** &#x20;


  - `GROUP`：直接作为 UTF-8 字符串分发。
  - `CONTROL`/`PRIVATE`：通过`handleControlJson`和`handlePrivateJson`解析 JSON。例如控制命令`login_ack`、`list`，以及私聊消息`{"cmd":"chat","from":"...","msg":"..."}`。
- **数据模型 (Model)**`std::unordered_map<uint32_t, std::vector<QString>> m_history;`集中管理所有聊天记录，键为对方 ID（群聊为`9999`），值为格式化后的消息列表。

## 2. 应用程序入口与登录 (`main.cpp`&`Dialog.cpp`)

- **生命周期控制**在`main.cpp`中，程序先以阻塞方式`dlg.exec()`弹出登录框。只有在登录对话框返回`QDialog::Accepted`时，才会实例化并显示`MainWindow`，避免了未登录就进入主界面的逻辑漏洞。
- **异步登录流**由于网络连接是异步的，`Dialog` 中的登录按钮并不会直接进入主页面，而是：
  1. 触发`m_client->connectTo()`
  2. 监听`connected`信号并发送`login` 命令 JSON。
  3. 监听`loginAck`信号，根据服务器返回的`ok`决定是否`accept()` 关闭对话框并进入主界面。
  4. 期间利用`QProgressDialog` 提供模态的“正在登录...”反馈。
- **本地偏好存储**使用`QSettings` 将上次登录成功的 IP、端口、账号信息序列化到本地（注册表或配置文件），提升用户体验。

## 3. 主界面与 UI 渲染 (`MainWindow.cpp`)


- **动态列表更新**使用`QTimer`定时器每秒触发一次`m_client->getList()`获取在线名单。通过监听`userListReceived`信号，动态刷新`QListWidget`，通过`Qt::UserRole` 中，避免直接解析 UI 文本。
- **复杂气泡 UI 渲染** (`createSingleMessageWidget`)使用`QListWidget::setItemWidget` 注入自定义的组合控件。外层`QVBoxLayout`（垂直布局）放置时间/用户名栏与消息气泡。内层`QHBoxLayout`（水平布局）结合`addStretch(1)` 实现“自己发的消息靠右，别人发的消息靠左”。

- **自适应宽度机制** 窗口尺寸改变会导致消息气泡排版混乱。程序通过重写`resizeEvent` 实现了完美响应：

  ```c++ 
  voidMainWindow::resizeEvent(QResizeEvent* event) {
    // 当窗口被拉伸时，遍历所有气泡，强制将气泡最大宽度与当前视口 (viewport) 宽度绑定
    // 确保长文本能够在缩放时自动换行并重新计算高度 (sizeHint)
  }
  ```

- **自聊与状态同步优化 (SSOT )** &#x20;

  修复了“自己发给自己”时消息重复出现的问题。
  - **逻辑**：在`on_buttonSend_clicked`中，发送方只管将数据推入`m_history`并发送网络请求。只有当发送对象**不是自己**时，才主动调用渲染。如果发送对象是自己，则完全信任服务器推回的`privateMessageReceived` 信号来触发 UI 渲染，保证了数据的唯一性（Single Source of Truth）。

***
