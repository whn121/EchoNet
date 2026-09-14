#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    //登录按钮点击
    void slot_loginBtnClicked();
    //成功连上服务器触发
    void slot_connected();
    //收到服务器数据触发
    void slot_readyRead();
    //网络出错
    void slot_error(QAbstractSocket::SocketError err);

private:
    Ui::MainWindow *ui_;
    QTcpSocket *m_socket_;

    QByteArray m_recvBuf_;

    uint32_t qtimes_ = 0;
    bool ifloginempty_ = false;

    // 完整解析一帧协议
    void parsePacket();
};
#endif // LOGINWINDOW_H
