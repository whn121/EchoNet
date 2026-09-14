#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>

namespace Ui {
class ChatWindow;
}

enum class MyType : uint16_t
{
    //登录
    LOGIN_REQ    = 0x01,   // 客户端 -> 服务器，携带 username|password
    LOGIN_RESP   = 0x02,   // 服务器 -> 客户端，返回登录结果
    //创建room
    CREATE_ROOM_REQ  = 0x03,  // 客户端 -> 服务器，携带 room_name
    CREATE_ROOM_RESP = 0x04,  // 服务器 -> 客户端，返回 room_id
    //加入room
    JOIN_ROOM_REQ    = 0x05,  // 客户端 -> 服务器，携带 room_id
    JOIN_ROOM_RESP   = 0x06,  // 服务器 -> 客户端，返回加入结果
    //离开room
    LEAVE_ROOM_REQ   = 0x07,  // 客户端 -> 服务器，携带 room_id
    LEAVE_ROOM_RESP  = 0x08,  // 服务器 -> 客户端，返回离开结果
    //发送消息
    SEND_MSG_REQ    = 0x09,  // 客户端 -> 服务器，携带 room_id|content
    SEND_MSG_RESP   = 0x0A,  // 服务器 -> 发送者，确认发送成功
    //广播发给all
    BROADCAST_MSG   = 0x0B,  // 服务器 -> 房间内所有成员（包括发送者），携带 room_id|username|content
    //心跳(心跳就是客户端和服务端之间，每隔一段时间互相发一条极小的探测数据包，证明对方还活着、连接没断)
    HEARTBEAT_REQ   = 0x0C,  // 客户端 -> 服务器，无 payload 或带时间戳
    HEARTBEAT_RESP  = 0x0D,  // 服务器 -> 客户端，无 payload 或带时间戳
    //错误处理
    ERROR_RESP      = 0x0E,  // 服务器 -> 客户端，携带错误码和错误消息

    MEMBER_COUNT_UPDATE = 0x0F

};

class ChatWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatWindow(const QString& username, QTcpSocket* socket, QWidget *parent = nullptr);
    ~ChatWindow();

private slots:
    void onCreateRoom();
    void onJoinRoom();
    void onLeaveRoom();
    void onSendMessage();
    void onReadyRead();
    void onDisconnected();
    void sendHeartbeat();

private:
    void parsePacket();
    void sendPacket(MyType type, uint32_t id, const QString& payload);

    Ui::ChatWindow *ui_;
    QTcpSocket *socket_;
    QString username_;
    uint32_t currentRoomId_ = 0;
    uint32_t requestId_ = 1;
    QByteArray recvBuf_;
    QTimer *heartbeatTimer_;
};

#endif // CHATWINDOW_H
