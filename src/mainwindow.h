#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>
#include <QTextEdit>
#include <QSplitter>
#include <QMouseEvent>
#include "sshmanager.h"

// 前置声明
class UserManageDialog;

class UploadPage;
class DownloadPage;
class ProjectPage;
class LocalManagerPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void appendLog(const QString &msg);

    // SSH连接相关
    void connectToSshServer();
    void disconnectFromSshServer();
    bool isSshConnected() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    // SSH相关槽函数
    void onSshConnectionEstablished();
    void onSshConnectionLost();
    void onSshConnectionError(const QString &errorMessage);
    void onSshConnectClicked();
    void updateSshStatus();
    void onLoginClicked();
    void onLogoutClicked();      // 退出登录
    void onUserManageClicked();

private:
    void initUI();
    void initConnections();
    void updateStatusBar(const QString &message);
    void initSshConnection();
    void applyRolePermissions();   // 根据角色应用界面权限
    void setupRoleBasedUI();      // 角色切换时刷新界面
    void onAutoConnectSsh();      // 登录后自动连接SSH

    // 标题栏
    QWidget *m_titleBar;
    bool m_isDragging;
    QPoint m_dragPos;

    // SSH连接相关控件
    QLabel     *m_sshStatusLabel;
    QPushButton *m_sshConnectBtn;

    // 登录/用户管理按钮
    QPushButton *m_loginBtn = nullptr;
    QPushButton *m_logoutBtn = nullptr;     // 退出登录按钮
    QPushButton *m_userManageBtn = nullptr;
    QLabel      *m_userInfoLabel = nullptr;

    // 标签页
    QTabWidget *m_tabWidget;
    UploadPage  *m_uploadPage;
    DownloadPage *m_downloadPage;
    ProjectPage  *m_projectPage = nullptr;
    LocalManagerPage *m_localManagerPage = nullptr;

    // 日志面板
    QSplitter *m_splitter;
    QTextEdit *m_logPanel;

    // SSH管理器
    SshManager *m_sshManager;
    bool  m_sshAutoConnect;
};

#endif // MAINWINDOW_H
