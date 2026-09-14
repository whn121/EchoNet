#include "loginwindow.h"
#include "./ui_loginwindow.h"
#include <QMessageBox>
#include <QDebug>
#include <QByteArray>
#include <QPushButton>
#include <arpa/inet.h>
#include "chatwindow.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui_(new Ui::MainWindow)
{
    ui_->setupUi(this);
    m_socket_ = new QTcpSocket(this); //窗口关闭自动析构
    qtimes_ = 0;
    ifloginempty_ = false;

    //绑定信号槽
    connect(ui_->btn_login, &QPushButton::clicked, this, &MainWindow::slot_loginBtnClicked);
    connect(m_socket_, &QTcpSocket::connected, this, &MainWindow::slot_connected);
    connect(m_socket_, &QTcpSocket::readyRead, this, &MainWindow::slot_readyRead);
    connect(m_socket_, &QTcpSocket::errorOccurred, this, &MainWindow::slot_error);
}

MainWindow::~MainWindow()
{
    delete ui_;
}

//点击登录按钮
void MainWindow::slot_loginBtnClicked()
{
    QString account = ui_->box_account->toPlainText().trimmed();//.trimmed去掉字符串**开头、结尾**所有空白字符：空格、
    //`\n`换行、`\r`回车、`\t`制表符。**中间空格不动**
    QString password = ui_->box_password->toPlainText().trimmed();

    //判空
    if (account.isEmpty() || password.isEmpty())
    {
        QMessageBox::critical(this, "错误", "账号密码不能为空");
        ifloginempty_ = true;
        return;
    }
    ifloginempty_ = false;

    //如果之前已经连接，先关掉旧连接
    if (m_socket_->state() == QTcpSocket::ConnectedState)
    {
        m_socket_->close();
        m_recvBuf_.clear();
    }

    if (!ifloginempty_)
    {
        //连接本机EchoNet服务端，端口8888
        m_socket_->connectToHost("127.0.0.1", 8080);
        qDebug() << "正在连接服务器 127.0.0.1:8080";
    }
}

//TCP连接成功之后执行
void MainWindow::slot_connected()
{
    //组装协议
    QByteArray packet;
    QString payload = ui_->box_account->toPlainText().trimmed() + "|" + ui_->box_password->toPlainText().trimmed();

    uint32_t body_len = payload.size() + 6;
    packet.reserve(4 + body_len);

    uint32_t net_body_len = htonl(body_len);
    packet.append(reinterpret_cast<const char*> (&net_body_len), 4);

    uint16_t type = uint16_t(MyType::LOGIN_REQ);
    uint16_t net_type = htons(type);
    packet.append(reinterpret_cast<const char*> (&net_type), 2);

    uint32_t id = qtimes_++;
    uint32_t net_id = htonl(id);
    packet.append(reinterpret_cast<const char*> (&net_id), 4);

    QByteArray byte_payload = payload.toUtf8();

    packet.append(byte_payload);

    m_socket_->write(packet);
    qDebug()<<"发送报文："<<payload;
}

//收到服务器返回数据
void MainWindow::slot_readyRead()
{
    m_recvBuf_.append(m_socket_->readAll());
    parsePacket();
}

// 循环解析缓冲区完整帧
void MainWindow::parsePacket()
{
    while(m_recvBuf_.size() >=4)
    {
        uint32_t net_body_len;
        memcpy(&net_body_len, m_recvBuf_.constData(), 4);
        uint32_t body_len = ntohl(net_body_len);
        uint32_t total_frame_len = 4 + body_len;

        // 还没收到完整一帧，退出等待后续数据
        if ((uint32_t)m_recvBuf_.size() < total_frame_len)
        {
            break;
        }

        // 取出完整一帧
        QByteArray oneFrame = m_recvBuf_.left(total_frame_len);//从左边截取
        m_recvBuf_.remove(0, total_frame_len);

        // 解析body部分：type(2) + seq(4) + payload
        const char* bodyptr = oneFrame.constData() + 4;
        uint16_t net_type;
        memcpy(&net_type, bodyptr, 2);
        uint16_t type = ntohs(net_type);

        uint32_t net_id;
        memcpy(&net_id, bodyptr + 2, 4);
        uint32_t id = ntohl(net_id);

        QByteArray payload = oneFrame.mid(4+2+4);
        qDebug() << "收到帧 type="<<type << " id="<<id << " payload="<<payload;

        // 处理登录响应 LOGIN_RESP 0x02
        if(type == uint16_t(MyType::LOGIN_RESP))
        {
            QString res = QString::fromUtf8(payload).trimmed();
            if(res == "OK")
            {
                QMessageBox::information(this,"登录结果","登录成功！");
                // 先断开登录窗口对 readyRead 的监听，避免和聊天窗口重复接收
                disconnect(m_socket_, &QTcpSocket::readyRead, this, nullptr);

                // 创建聊天窗口
                ChatWindow *chatWin = new ChatWindow(ui_->box_account->toPlainText().trimmed(), m_socket_);
                chatWin->show();

                // 隐藏登录窗口
                this->hide();
            }
            else if(res == "FAIL")
            {
                QMessageBox::critical(this,"登录结果","账号密码错误！");
            }
        }
        else if(type == static_cast<uint16_t>(MyType::ERROR_RESP))
        {
            QString errMsg = QString::fromUtf8(payload);
            QMessageBox::warning(this,"服务端错误",errMsg);
        }
    }
}

//网络错误处理
void MainWindow::slot_error(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err);
    QString errStr = m_socket_->errorString();
    qDebug()<<"网络错误："<<errStr;
    QMessageBox::critical(this,"网络异常",errStr);
}













