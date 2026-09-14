#include "chatwindow.h"
#include "./ui_chatwindow.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QDebug>
#include <arpa/inet.h>

ChatWindow::ChatWindow(const QString& username, QTcpSocket* socket, QWidget *parent)
    : QMainWindow(parent)
    , ui_(new Ui::ChatWindow)
    , socket_(socket)
    , username_(username)
{
    ui_->setupUi(this);

    setWindowTitle("EchoNet - " + username_);

    // 初始状态：未加入房间，禁用输入和发送
    ui_->inputLineEdit->setEnabled(false);
    ui_->sendBtn->setEnabled(false);
    ui_->leaveRoomBtn->setEnabled(false);

    // 网络信号
    connect(socket_, &QTcpSocket::readyRead, this, &ChatWindow::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &ChatWindow::onDisconnected);

    // 按钮
    connect(ui_->createRoomBtn, &QPushButton::clicked, this, &ChatWindow::onCreateRoom);
    connect(ui_->joinRoomBtn,   &QPushButton::clicked, this, &ChatWindow::onJoinRoom);
    connect(ui_->leaveRoomBtn,  &QPushButton::clicked, this, &ChatWindow::onLeaveRoom);
    connect(ui_->sendBtn,       &QPushButton::clicked, this, &ChatWindow::onSendMessage);
    connect(ui_->inputLineEdit, &QLineEdit::returnPressed, this, &ChatWindow::onSendMessage);

    // 心跳
    heartbeatTimer_ = new QTimer(this);
    connect(heartbeatTimer_, &QTimer::timeout, this, &ChatWindow::sendHeartbeat);
    heartbeatTimer_->start(15000);
}

ChatWindow::~ChatWindow()
{
    heartbeatTimer_->stop();
    delete ui_;
}

void ChatWindow::sendPacket(MyType type, uint32_t id, const QString& payload)
{
    QByteArray payloadBytes = payload.toUtf8();
    uint32_t bodyLen = 2 + 4 + payloadBytes.size();

    QByteArray packet;
    packet.resize(4 + bodyLen);

    uint32_t netBodyLen = htonl(bodyLen);
    memcpy(packet.data(), &netBodyLen, 4);

    uint16_t netType = htons(static_cast<uint16_t>(type));
    memcpy(packet.data() + 4, &netType, 2);

    uint32_t netId = htonl(id);
    memcpy(packet.data() + 6, &netId, 4);

    memcpy(packet.data() + 10, payloadBytes.constData(), payloadBytes.size());

    socket_->write(packet);
}

void ChatWindow::onCreateRoom()
{
    bool ok;
    QString roomName = QInputDialog::getText(this, "创建房间", "房间名:",
                                             QLineEdit::Normal, "", &ok);
    if (ok && !roomName.isEmpty()) {
        sendPacket(MyType::CREATE_ROOM_REQ, requestId_++, roomName);
    }
}

void ChatWindow::onJoinRoom()
{
    bool ok;
    QString roomIdStr = QInputDialog::getText(this, "加入房间", "房间ID:",
                                              QLineEdit::Normal, "", &ok);
    if (ok && !roomIdStr.isEmpty()) {
        bool isNumber;
        uint32_t roomId = roomIdStr.toUInt(&isNumber);
        if (isNumber) {
            currentRoomId_ = roomId;
            sendPacket(MyType::JOIN_ROOM_REQ, requestId_++, roomIdStr);
        } else {
            QMessageBox::warning(this, "提示", "房间ID必须是数字");
        }
    }
}

void ChatWindow::onLeaveRoom()
{
    if (currentRoomId_ != 0) {
        sendPacket(MyType::LEAVE_ROOM_REQ, requestId_++, QString::number(currentRoomId_));
    }
}

void ChatWindow::onSendMessage()
{
    QString content = ui_->inputLineEdit->text().trimmed();
    if (content.isEmpty() || currentRoomId_ == 0) {
        return;
    }

    sendPacket(MyType::SEND_MSG_REQ, requestId_++, content);
    ui_->inputLineEdit->clear();
}

void ChatWindow::sendHeartbeat()
{
    sendPacket(MyType::HEARTBEAT_REQ, requestId_++, "");
}

void ChatWindow::onReadyRead()
{
    recvBuf_.append(socket_->readAll());
    parsePacket();
}

void ChatWindow::onDisconnected()
{
    ui_->statusbar->showMessage("连接已断开");
    ui_->chatTextEdit->append("[系统] 与服务器的连接已断开");
    ui_->inputLineEdit->setEnabled(false);
    ui_->sendBtn->setEnabled(false);
    heartbeatTimer_->stop();
}

void ChatWindow::parsePacket()
{
    while (recvBuf_.size() >= 4) {
        uint32_t netBodyLen;
        memcpy(&netBodyLen, recvBuf_.constData(), 4);
        uint32_t bodyLen = ntohl(netBodyLen);
        uint32_t totalLen = 4 + bodyLen;

        if ((uint32_t)recvBuf_.size() < totalLen) {
            break;
        }

        QByteArray frame = recvBuf_.left(totalLen);
        recvBuf_.remove(0, totalLen);

        const char* body = frame.constData() + 4;

        uint16_t netType;
        memcpy(&netType, body, 2);
        uint16_t typeVal = ntohs(netType);

        uint32_t netId;
        memcpy(&netId, body + 2, 4);
        uint32_t id = ntohl(netId);

        QByteArray payloadData = frame.mid(10);
        QString payload = QString::fromUtf8(payloadData);

        MyType type = static_cast<MyType>(typeVal);

        switch (type) {
        case MyType::CREATE_ROOM_RESP: {
            // payload 格式："OK|room_id|人数"
            QStringList parts = payload.split('|');
            if (parts.size() >= 3 && parts[0] == "OK") {
                currentRoomId_ = parts[1].toUInt();
                int memberCount = parts[2].toInt();
                ui_->roomLabel->setText(QString("当前房间: %1  人数: %2")
                                            .arg(currentRoomId_)
                                            .arg(memberCount));
                ui_->inputLineEdit->setEnabled(true);
                ui_->sendBtn->setEnabled(true);
                ui_->leaveRoomBtn->setEnabled(true);
            }
            break;
        }
        case MyType::JOIN_ROOM_RESP: {
            if (payload.startsWith("OK")) {
                QStringList parts = payload.split('|');
                int memberCount = 0;
                if (parts.size() >= 2) {
                    memberCount = parts[1].toInt();
                }
                ui_->roomLabel->setText(QString("当前房间: %1  人数: %2")
                                            .arg(currentRoomId_)
                                            .arg(memberCount));
                ui_->inputLineEdit->setEnabled(true);
                ui_->sendBtn->setEnabled(true);
                ui_->leaveRoomBtn->setEnabled(true);
            }
            break;
        }
        case MyType::LEAVE_ROOM_RESP: {
            currentRoomId_ = 0;
            ui_->roomLabel->setText("当前未加入任何房间");
            ui_->inputLineEdit->setEnabled(false);
            ui_->sendBtn->setEnabled(false);
            ui_->leaveRoomBtn->setEnabled(false);
            break;
        }
        case MyType::BROADCAST_MSG: {
            QStringList parts = payload.split('|');
            if (parts.size() >= 3) {
                QString fromUser = parts[1];
                QString content = parts[2];
                QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
                ui_->chatTextEdit->append("[" + time + "] " + fromUser + ": " + content);
            }
            break;
        }
        case MyType::SEND_MSG_RESP: {
            break;
        }
        case MyType::HEARTBEAT_RESP: {
            ui_->statusbar->showMessage("已连接");
            break;
        }
        case MyType::ERROR_RESP: {
            QMessageBox::warning(this, "错误", payload);
            break;
        }
        case MyType::MEMBER_COUNT_UPDATE: {
            // payload 格式: "room_id|count"
            QStringList parts = payload.split('|');
            if (parts.size() >= 2) {
                uint32_t updatedRoomId = parts[0].toUInt();
                int count = parts[1].toInt();
                // 只更新当前所在房间
                if (updatedRoomId == currentRoomId_) {
                    ui_->roomLabel->setText(QString("当前房间: %1  人数: %2")
                                                .arg(currentRoomId_).arg(count));
                }
            }
            break;
        }
        default:
            break;
        }
    }
}
