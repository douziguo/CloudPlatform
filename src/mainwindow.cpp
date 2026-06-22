#include "mainwindow.h"
#include "uploadpage.h"
#include "downloadpage.h"
#include "projectpage.h"
#include "localmanagerpage.h"
#include "logindialog.h"
#include "usermanagedialog.h"
#include "authmanager.h"
#include "sshmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <QGroupBox>
#include <QTimer>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_titleBar(nullptr)
    , m_isDragging(false)
    , m_uploadPage(nullptr)
    , m_downloadPage(nullptr)
    , m_logPanel(nullptr)
    , m_localManagerPage(nullptr)
    , m_sshManager(nullptr)
    , m_sshAutoConnect(true)  // 启用自动连接模式（隐藏按钮，自动连接）
{
    // 1. 设置无边框窗口（需先完成，后续弹框需要父窗口存在）
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    // 2. 初始化 SSH 管理器并配置连接参数（不立即连接）
    m_sshManager = SshManager::instance();
    initSshConnection();

    initUI();
    initConnections();

    // 3. 在 UI 就绪后，先连接 SSH → 同步 db → 再 initDatabase + LoginDialog
    //    用 singleShot 确保窗口已经显示再触发连接
    appendLog("[启动] CloudPlatform v1.0.0 已启动");
    appendLog("[启动] 正在连接服务器以同步用户数据库...");
    QTimer::singleShot(500, this, &MainWindow::onStartupSshAndDbSync);
}

MainWindow::~MainWindow() {}

void MainWindow::initUI()
{
    setWindowTitle("CloudPlatform - 测量数据管理系统");
    resize(1100, 1000);
    setMinimumSize(900, 700);

    // === 外框容器（带 #373E65 边框）===
    QWidget *frameWidget = new QWidget(this);
    frameWidget->setObjectName("frameWidget");

    QVBoxLayout *frameLayout = new QVBoxLayout(frameWidget);
    frameLayout->setContentsMargins(2, 2, 2, 2); // 2px 边框空间
    frameLayout->setSpacing(0);

    // === 自定义标题栏 ===
    m_titleBar = new QWidget();
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setFixedHeight(32);

    QHBoxLayout *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->setSpacing(0);

    // 标题文字
    QLabel *titleLabel = new QLabel("  CloudPlatform - 测量数据管理系统");
    titleLabel->setObjectName("titleLabel");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    // 最小化按钮
    QPushButton *minBtn = new QPushButton("—");
    minBtn->setObjectName("windowMinBtn");
    minBtn->setFixedSize(45, 32);
    titleLayout->addWidget(minBtn);

    // 最大化/还原按钮
    QPushButton *maxBtn = new QPushButton("□");
    maxBtn->setObjectName("windowMaxBtn");
    maxBtn->setFixedSize(45, 32);
    titleLayout->addWidget(maxBtn);

    // 关闭按钮
    QPushButton *closeBtn = new QPushButton("×");
    closeBtn->setObjectName("windowCloseBtn");
    closeBtn->setFixedSize(45, 32);
    titleLayout->addWidget(closeBtn);

    frameLayout->addWidget(m_titleBar);

    // === 内容区域 ===
    QWidget *contentWidget = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 顶部工具栏 ===
    QWidget *toolbarWidget = new QWidget();
    toolbarWidget->setObjectName("toolbarWidget");
    toolbarWidget->setFixedHeight(52);

    QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbarWidget);
    toolbarLayout->setContentsMargins(12, 0, 12, 0);
    toolbarLayout->setSpacing(12);
    toolbarLayout->setAlignment(Qt::AlignVCenter);

    // Logo
    QLabel *logoLabel = new QLabel("CloudPlatform");
    logoLabel->setObjectName("logoLabel");
    logoLabel->setMinimumWidth(120);
    toolbarLayout->addWidget(logoLabel, 0, Qt::AlignVCenter);

    toolbarLayout->addStretch();

    // === SSH连接控制（自动模式：隐藏按钮，自动管理连接）===

    // SSH状态标签（保留用于内部状态跟踪，但默认隐藏）
    m_sshStatusLabel = new QLabel("○ 未连接");
    m_sshStatusLabel->setStyleSheet("color: #f44747; font-weight: bold; font-size: 12px;");
    m_sshStatusLabel->setFixedWidth(80);
    m_sshStatusLabel->setVisible(false);  // 自动模式下隐藏
    toolbarLayout->addWidget(m_sshStatusLabel, 0, Qt::AlignVCenter);

    // SSH连接按钮（保留功能但默认隐藏，可通过配置显示）
    m_sshConnectBtn = new QPushButton("连接SSH");
    m_sshConnectBtn->setFixedSize(80, 28);
    m_sshConnectBtn->setToolTip("连接SSH服务器");
    m_sshConnectBtn->setVisible(false);  // 自动模式下隐藏
    toolbarLayout->addWidget(m_sshConnectBtn, 0, Qt::AlignVCenter);

    // 启用自动连接模式（登录后自动连接，无需手动点击）
    m_sshAutoConnect = true;

    toolbarLayout->addSpacing(20);

    // === 登录/用户管理区域 ===
    m_userInfoLabel = new QLabel();
    m_userInfoLabel->setStyleSheet("color: #969696; font-size: 12px;");
    toolbarLayout->addWidget(m_userInfoLabel, 0, Qt::AlignVCenter);

    m_loginBtn = new QPushButton(QString::fromUtf8("登录"));
    m_loginBtn->setFixedSize(60, 28);
    m_loginBtn->setToolTip(QString::fromUtf8("用户登录"));
    connect(m_loginBtn, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    toolbarLayout->addWidget(m_loginBtn, 0, Qt::AlignVCenter);

    // 退出登录按钮（登录后显示）
    m_logoutBtn = new QPushButton(QString::fromUtf8("退出登录"));
    m_logoutBtn->setFixedSize(80, 28);
    m_logoutBtn->setToolTip(QString::fromUtf8("退出当前账户"));
    m_logoutBtn->setVisible(false);
    connect(m_logoutBtn, &QPushButton::clicked, this, &MainWindow::onLogoutClicked);
    toolbarLayout->addWidget(m_logoutBtn, 0, Qt::AlignVCenter);

    m_userManageBtn = new QPushButton(QString::fromUtf8("用户管理"));
    m_userManageBtn->setFixedSize(70, 28);
    m_userManageBtn->setToolTip(QString::fromUtf8("管理用户账户"));
    m_userManageBtn->setVisible(false);  // 默认隐藏，登录后根据角色显示
    connect(m_userManageBtn, &QPushButton::clicked, this, &MainWindow::onUserManageClicked);
    toolbarLayout->addWidget(m_userManageBtn, 0, Qt::AlignVCenter);

    mainLayout->addWidget(toolbarWidget);

    // === 标签页 ===
    m_tabWidget = new QTabWidget();
    m_tabWidget->setDocumentMode(false);

    m_uploadPage = new UploadPage();
    m_downloadPage = new DownloadPage();
    m_projectPage = new ProjectPage();
    m_localManagerPage = new LocalManagerPage();

    // 初始只添加上传页，其他 tab 在 applyRolePermissions 中根据角色动态添加
    m_tabWidget->addTab(m_uploadPage, "文件上传");

    // === 日志面板 ===
    QWidget *logWidget = new QWidget();
    logWidget->setObjectName("logWidget");
    QVBoxLayout *logLayout = new QVBoxLayout(logWidget);
    logLayout->setContentsMargins(4, 4, 4, 4);
    logLayout->setSpacing(4);

    // 日志标题栏
    QHBoxLayout *logHeaderLayout = new QHBoxLayout();
    logHeaderLayout->setContentsMargins(4, 2, 4, 2);
    QLabel *logTitleLabel = new QLabel("运行日志");
    logTitleLabel->setObjectName("logTitleLabel");
    logHeaderLayout->addWidget(logTitleLabel);
    logHeaderLayout->addStretch();

    QPushButton *clearLogBtn = new QPushButton("清除");
    clearLogBtn->setFixedSize(80, 32);
    logHeaderLayout->addWidget(clearLogBtn);

    logLayout->addLayout(logHeaderLayout);

    // 日志文本区
    m_logPanel = new QTextEdit();
    m_logPanel->setReadOnly(true);
    m_logPanel->setMaximumHeight(80);
    logLayout->addWidget(m_logPanel);

    // 连接清除按钮
    connect(clearLogBtn, &QPushButton::clicked, m_logPanel, &QTextEdit::clear);

    // === Splitter: TabWidget + 日志 ===
    m_splitter = new QSplitter(Qt::Vertical);
    m_splitter->setHandleWidth(4);
    m_splitter->addWidget(m_tabWidget);
    m_splitter->addWidget(logWidget);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);

    mainLayout->addWidget(m_splitter);

    frameLayout->addWidget(contentWidget, 1);

    setCentralWidget(frameWidget);

    // === 状态栏 ===
    statusBar()->showMessage("正在初始化...");

    // 连接标题栏按钮
    connect(minBtn, &QPushButton::clicked, this, &QMainWindow::showMinimized);
    connect(maxBtn, &QPushButton::clicked, this, [this]() {
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    connect(closeBtn, &QPushButton::clicked, this, &QMainWindow::close);

    // === SSH连接相关信号槽 ===
    if (m_sshConnectBtn) {
        connect(m_sshConnectBtn, &QPushButton::clicked, this, &MainWindow::onSshConnectClicked);
    }
}

void MainWindow::initConnections()
{
    // SSH 管理器信号
    SshManager *ssh = SshManager::instance();
    connect(ssh, &SshManager::connectionEstablished,
            this, &MainWindow::onSshConnectionEstablished);
    connect(ssh, &SshManager::connectionLost,
            this, &MainWindow::onSshConnectionLost);
    connect(ssh, &SshManager::connectionError,
            this, &MainWindow::onSshConnectionError);
    connect(ssh, &SshManager::remoteDirListed,
            this, [this](const QString &path, const QList<RemoteFileInfo> &fileList) {
        // 转发给下载页
        if (m_downloadPage)
            m_downloadPage->onRemoteDirListed(path, fileList);
        statusBar()->showMessage(QString("远程目录: %1（%2 项）").arg(path).arg(fileList.size()), 3000);
        appendLog(QString("[SSH] 目录列表: %1，共 %2 项").arg(path).arg(fileList.size()));
    });
    connect(ssh, &SshManager::remoteDirError,
            this, [this](const QString &msg) {
        statusBar()->showMessage("远程目录错误: " + msg);
        appendLog(QString("[SSH错误] %1").arg(msg));
    });

    // 上传信号 -> 日志（连接SSH管理器的信号）
    connect(ssh, &SshManager::uploadProgress,
            this, [this](const QString &key, qint64 sent, qint64 total) {
        Q_UNUSED(sent); Q_UNUSED(total);
        appendLog(QString("[上传] 进度: %1").arg(key));
    });
    connect(ssh, &SshManager::uploadFinished,
            this, [this](const QString &key, bool success, const QString &message) {
        if (success)
            appendLog(QString("[上传完成] %1").arg(key));
        else
            appendLog(QString("[上传失败] %1 - %2").arg(key, message));
    });

    // 项目/任务变更 → 刷新上传页下拉框
    connect(m_projectPage, &ProjectPage::projectsChanged,
            m_uploadPage, &UploadPage::refreshProjectCombo);

    // 备份恢复完成后 → 刷新项目管理页面
    connect(m_localManagerPage, &LocalManagerPage::backupRestoreCompleted,
            this, [this]() {
        if (m_projectPage && AuthManager::instance()->isLoggedIn()) {
            m_projectPage->refreshProjectList();
            appendLog(QString::fromUtf8("[恢复] 项目管理页面已刷新"));
        }
    });
}

void MainWindow::appendLog(const QString &msg)
{
    if (!m_logPanel) return;
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logPanel->append(QString("<span style=\"color:#6a9955;\">[%1]</span> %2").arg(timestamp, msg));
    // 自动滚到底部
    QTextCursor cursor = m_logPanel->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logPanel->setTextCursor(cursor);
}

// === 鼠标事件 - 标题栏拖拽 ===
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_titleBar && m_titleBar->geometry().contains(event->pos())) {
        m_isDragging = true;
        m_dragPos = event->globalPos() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && !isMaximized()) {
        move(event->globalPos() - m_dragPos);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_isDragging = false;
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_titleBar && m_titleBar->geometry().contains(event->pos())) {
        if (isMaximized()) showNormal();
        else showMaximized();
        event->accept();
        return;
    }
    QMainWindow::mouseDoubleClickEvent(event);
}

// === SSH连接相关函数 ===

void MainWindow::initSshConnection()
{
    if (!m_sshManager) {
        m_sshManager = SshManager::instance();
    }

    // 设置默认SSH连接参数
    // 你可以修改这些参数为你的服务器信息
    QString host = "1.15.120.246";
    QString user = "ubuntu";
    QString password = "Supernavi2015!";

    m_sshManager->setConnectionParams(host, user, password, 22);

    // 注意：connectionEstablished / connectionLost / connectionError
    // 已在 initConnections() 中连接，此处不再重复连接
    connect(m_sshManager, &SshManager::commandOutput,
            this, [this](const QString &output) {
                appendLog("[SSH输出] " + output.trimmed());
            });
    connect(m_sshManager, &SshManager::commandFinished,
            this, [this](int exitCode) {
                appendLog(QString("[SSH] 命令执行完成，退出码: %1").arg(exitCode));
            });

    appendLog("[SSH] SSH连接管理器已初始化（自动连接模式）");
    appendLog(QString("[SSH] 服务器: %1@%2").arg(user, host));

    // 自动连接模式下启动时自动尝试连接
    if (m_sshAutoConnect) {
        appendLog("[SSH] 已启用自动连接模式，将在启动/登录时自动连接服务器");
        // 不在这里立即连接，由 onStartupSshAndDbSync() 统一处理
    } else {
        appendLog("[SSH] 手动模式 - 点击「连接SSH」按钮手动连接服务器");
    }
}

void MainWindow::connectToSshServer()
{
    appendLog("[SSH] 正在连接服务器...");
    updateSshStatus();

    if (m_sshManager->connectToServer()) {
        if (m_sshManager->isConnected()) {
            appendLog("[SSH] 自动连接成功！");
            // 不在这里手动调用 onSshConnectionEstablished()，
            // 信号 connectionEstablished 会自动触发，避免重复调用
        }
    } else {
        appendLog("[SSH] 自动连接失败，请检查网络、密码或SSH配置");
        appendLog("[SSH] 可手动运行: ssh ubuntu@1.15.120.246");
        onSshConnectionLost();
    }
}

void MainWindow::disconnectFromSshServer()
{
    if (m_sshManager) {
        // 传 false：主动断开，不发射 connectionLost 信号，避免误弹窗/自动重连
        m_sshManager->disconnectFromServer(false);
        appendLog("[SSH] 已断开连接");
        // 手动更新UI状态（按钮和标签保持隐藏）
        updateSshStatus();
        if (m_sshConnectBtn) {
            m_sshConnectBtn->setText(QString::fromUtf8("连接SSH"));
        }
        if (m_sshStatusLabel) {
            m_sshStatusLabel->setText("○ 未连接");
            m_sshStatusLabel->setStyleSheet("color: #f44747;");
        }
        statusBar()->showMessage(QString::fromUtf8("SSH未连接"));
    }
}

bool MainWindow::isSshConnected() const
{
    return m_sshManager && m_sshManager->isConnected();
}

void MainWindow::onSshConnectionEstablished()
{
    appendLog("[SSH] 连接已建立（自动模式）");
    updateSshStatus();

    // 自动连接模式下保持按钮隐藏，但更新内部状态
    if (m_sshConnectBtn) {
        m_sshConnectBtn->setText("断开SSH");
        // 按钮保持隐藏状态
    }

    if (m_sshStatusLabel) {
        m_sshStatusLabel->setText("● 已连接");
        m_sshStatusLabel->setStyleSheet("color: #4ec9b0;");
        // 标签保持隐藏状态
    }

    statusBar()->showMessage("SSH已连接 - " + m_sshManager->executeCommand("hostname"));

    // SSH连接成功后自动刷新下载页、项目管理页和上传页项目下拉
    if (m_downloadPage && AuthManager::instance()->isLoggedIn()) {
        // 先刷新权限（更新 m_allowedRootPath + 自动创建公司目录）
        m_downloadPage->refreshPermissions();
        QTimer::singleShot(300, m_downloadPage, &DownloadPage::refreshFileList);
    }
    if (m_projectPage && AuthManager::instance()->isLoggedIn()) {
        QTimer::singleShot(300, m_projectPage, &ProjectPage::refreshProjectList);
    }
    if (m_uploadPage && AuthManager::instance()->isLoggedIn()) {
        QTimer::singleShot(300, m_uploadPage, &UploadPage::refreshProjectCombo);
    }
}

void MainWindow::onSshConnectionError(const QString &errorMessage)
{
    appendLog(QString("[SSH错误] %1").arg(errorMessage));
    onSshConnectionLost();
}

void MainWindow::onSshConnectClicked()
{
    if (isSshConnected()) {
        disconnectFromSshServer();
    } else {
        connectToSshServer();
    }
}

void MainWindow::applyRolePermissions()
{
    bool isLoggedIn = AuthManager::instance()->isLoggedIn();
    UserRole role = AuthManager::instance()->currentRole();

    // 更新工具栏按钮显示
    if (m_loginBtn) {
        m_loginBtn->setVisible(!isLoggedIn);
    }
    if (m_logoutBtn) {
        m_logoutBtn->setVisible(isLoggedIn);
    }
    if (m_userManageBtn) {
        m_userManageBtn->setVisible(isLoggedIn && AuthManager::instance()->canManageUsers());
    }
    if (m_userInfoLabel) {
        if (isLoggedIn) {
            m_userInfoLabel->setText(QString::fromUtf8("[%1] %2 ")
                .arg(AuthManager::instance()->currentUser())
                .arg(AuthManager::instance()->roleString()));
            m_userInfoLabel->setStyleSheet("color: #4ec9b0; font-size: 12px; font-weight: bold;");
        } else {
            m_userInfoLabel->setText(QString::fromUtf8("[未登录] "));
            m_userInfoLabel->setStyleSheet("color: #969696; font-size: 12px;");
        }
    }

    if (!isLoggedIn) {
        // 未登录：清空所有tab，只保留上传页（禁用）
        if (m_tabWidget) {
            while (m_tabWidget->count() > 1) {
                m_tabWidget->removeTab(m_tabWidget->count() - 1);
            }
            m_tabWidget->setTabEnabled(0, false);
        }
        if (m_uploadPage) {
            m_uploadPage->setEnabled(false);
        }
        appendLog(QString::fromUtf8("[权限] 未登录，功能受限，请点击工具栏【登录】按钮"));
        return;
    }

    appendLog(QString("[权限] 当前角色: %1").arg(AuthManager::instance()->roleString()));

    // ===== 重建 tab 列表（根据角色动态控制） =====
    // 先清空所有 tab
    if (m_tabWidget) {
        while (m_tabWidget->count() > 0) {
            m_tabWidget->removeTab(0);
        }
    }

    // 上传页：除超级管理员外的所有登录用户可见
    if (AuthManager::instance()->canUpload()) {
        m_uploadPage->setEnabled(true);
        m_tabWidget->addTab(m_uploadPage, QString::fromUtf8("文件上传"));
        m_uploadPage->refreshProjectCombo();
    }

    // 管理员和内业人员：下载页（含超级管理员，可浏览下载）
    if (AuthManager::instance()->canDownload() || AuthManager::instance()->isSuperAdmin()) {
        m_tabWidget->addTab(m_downloadPage, QString::fromUtf8("文件下载"));
    }

    // 项目管理页：管理员和内业人员可编辑；超级管理员仅浏览（只读）
    if (AuthManager::instance()->canManageProjects() || AuthManager::instance()->isSuperAdmin()) {
        m_tabWidget->addTab(m_projectPage, QString::fromUtf8("项目管理"));
        // 不在这里刷新，等SSH连接成功后在 onSshConnectionEstablished 中统一刷新
    }

    // 外业人员：有下载页（只能看到自己上传的文件，不能下载），无项目管理
    if (role == UserRole::FieldWorker) {
        m_tabWidget->addTab(m_downloadPage, QString::fromUtf8("文件下载"));
        appendLog(QString::fromUtf8("[权限] 外业人员模式：可查看自己上传的文件，仅允许上传数据"));
    }

    // 超级管理员：仅浏览和下载，无上传、无项目编辑
    if (AuthManager::instance()->isSuperAdmin()) {
        appendLog(QString::fromUtf8("[权限] 超级管理员模式：仅限浏览和下载，项目管理为只读"));
    }

    // 所有已登录用户：本地管理（上传/下载历史 + 备份恢复）
    if (m_localManagerPage) {
        m_tabWidget->addTab(m_localManagerPage, QString::fromUtf8("本地管理"));
        m_localManagerPage->refreshHistory();
    }

    // ===== 刷新各页面的权限状态（必须在登录后调用） =====
    if (m_downloadPage) {
        m_downloadPage->refreshPermissions();
    }
    if (m_projectPage) {
        m_projectPage->refreshPermissions();
        m_projectPage->refreshProjectList();
    }
    if (m_uploadPage) {
        m_uploadPage->refreshProjectCombo();
    }
}

void MainWindow::setupRoleBasedUI()
{
    // 登录后由 MainWindow 构造函数调用
    applyRolePermissions();
}

void MainWindow::updateSshStatus()
{
    if (!m_sshStatusLabel) return;

    bool connected = m_sshManager && m_sshManager->isConnected();
    if (connected) {
        m_sshStatusLabel->setText("● 已连接");
        m_sshStatusLabel->setStyleSheet("color: #4ec9b0;");
    } else {
        m_sshStatusLabel->setText("○ 未连接");
        m_sshStatusLabel->setStyleSheet("color: #f44747;");
    }

    if (m_sshConnectBtn) {
        m_sshConnectBtn->setText(connected ? "断开SSH" : "连接SSH");
    }
}

void MainWindow::onLoginClicked()
{
    LoginDialog loginDlg(this);
    if (loginDlg.exec() == QDialog::Accepted) {
        appendLog(QString::fromUtf8("[登录] 欢迎，%1（%2）")
                    .arg(AuthManager::instance()->currentUser(),
                         AuthManager::instance()->roleString()));
        applyRolePermissions();
        // 登录后自动连接SSH（若未连接）
        if (!isSshConnected()) {
            onAutoConnectSsh();
        }
    }
}

void MainWindow::onLogoutClicked()
{
    auto reply = QMessageBox::question(this,
                                      QString::fromUtf8("退出登录"),
                                      QString::fromUtf8("确定要退出当前账户吗？\n退出后需要重新登录。"),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    // 重置所有业务页面状态
    m_uploadPage->reset();
    m_downloadPage->reset();
    m_projectPage->reset();
    m_localManagerPage->reset();

    // 断开SSH连接
    if (isSshConnected()) {
        disconnectFromSshServer();
    }

    // 登出认证
    AuthManager::instance()->logout();

    appendLog(QString::fromUtf8("[登出] 已退出登录"));
    applyRolePermissions();

    // 重新弹出登录对话框
    QTimer::singleShot(200, this, [this]() {
        LoginDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            appendLog(QString::fromUtf8("[登录] 欢迎，%1（%2）")
                        .arg(AuthManager::instance()->currentUser(),
                             AuthManager::instance()->roleString()));
            applyRolePermissions();
            onAutoConnectSsh();
        }
    });
}

void MainWindow::onAutoConnectSsh()
{
    // 登录后自动尝试连接SSH
    if (isSshConnected()) return;
    appendLog("[SSH] 登录成功，正在自动连接服务器...");
    connectToSshServer();
}

void MainWindow::setSshAutoMode(bool autoMode)
{
    // 设置 SSH 自动连接模式
    m_sshAutoConnect = autoMode;

    // 根据模式显示/隐藏按钮和状态标签
    if (m_sshConnectBtn) {
        m_sshConnectBtn->setVisible(!autoMode);
    }
    if (m_sshStatusLabel) {
        m_sshStatusLabel->setVisible(!autoMode);
    }

    appendLog(QString("[SSH] %1模式")
              .arg(autoMode ? QString::fromUtf8("自动（隐藏按钮）") : QString::fromUtf8("手动（显示按钮）")));

    // 如果切换到自动模式且当前未连接，立即尝试连接
    if (autoMode && !isSshConnected() && AuthManager::instance()->isLoggedIn()) {
        QTimer::singleShot(500, this, &MainWindow::connectToSshServer);
    }
}

void MainWindow::onStartupSshAndDbSync()
{
    // 步骤1：连接 SSH（自动模式）
    appendLog("[启动] 正在自动连接 SSH 服务器...");
    bool sshOk = m_sshManager->connectToServer();

    if (sshOk && m_sshManager->isConnected()) {
        appendLog("[启动] SSH 连接成功，正在同步用户数据库...");
        // 步骤2：从服务器下载 db
        QString localDb = AuthManager::instance()->syncDbFromServer();
        if (!localDb.isEmpty()) {
            appendLog(QString("[启动] 数据库同步完成：%1").arg(localDb));
        } else {
            appendLog("[启动] 数据库同步失败，使用本地缓存（若存在）");
        }
    } else {
        appendLog("[启动] SSH 连接失败，使用本地缓存数据库（换机器首次启动将只有默认 admin 账户）");
        appendLog("[启动] 登录后将尝试重新连接 SSH...");
    }

    // 步骤3：初始化数据库（使用同步回来的或本地的 db）
    AuthManager::instance()->initDatabase();

    // 步骤4：应用权限（此时未登录状态）
    applyRolePermissions();

    // 步骤5：弹出登录对话框
    QTimer::singleShot(100, this, [this]() {
        LoginDialog loginDlg(this);
        if (loginDlg.exec() == QDialog::Accepted) {
            appendLog(QString::fromUtf8("[登录] 欢迎，%1（%2）")
                        .arg(AuthManager::instance()->currentUser(),
                             AuthManager::instance()->roleString()));
            applyRolePermissions();
            // 若 SSH 已连接则不重复连接；未连接则再次尝试（自动模式）
            if (!isSshConnected()) {
                appendLog("[SSH] 登录成功，正在自动连接服务器...");
                connectToSshServer();
            }
        } else {
            // 用户关闭了登录框，退出程序
            QApplication::quit();
        }
    });
}


void MainWindow::onSshConnectionLost()
{
    appendLog("[SSH] 连接已断开");
    updateSshStatus();

    // 自动连接模式下保持按钮隐藏，但更新内部状态
    if (m_sshConnectBtn) {
        m_sshConnectBtn->setText(QString::fromUtf8("连接SSH"));
        // 按钮保持隐藏状态
    }

    if (m_sshStatusLabel) {
        m_sshStatusLabel->setText("○ 未连接");
        m_sshStatusLabel->setStyleSheet("color: #f44747;");
        // 标签保持隐藏状态
    }

    statusBar()->showMessage(QString::fromUtf8("SSH未连接"));

    // 如果已登录，自动尝试重连（自动模式下）
    if (AuthManager::instance()->isLoggedIn() && m_sshAutoConnect) {
        appendLog("[SSH] 自动模式：3秒后尝试重新连接...");
        QTimer::singleShot(3000, this, &MainWindow::connectToSshServer);
    } else if (AuthManager::instance()->isLoggedIn()) {
        // 手动模式：弹窗提示用户手动连接
        QMessageBox::warning(this,
                            QString::fromUtf8("SSH未连接"),
                            QString::fromUtf8("SSH服务器连接失败！\n\n请检查：\n  1. 网络是否正常\n  2. SSH服务是否启动\n  3. 用户名/密码是否正确\n\n你可以稍后在工具栏点击「连接SSH」手动重连。"));
    }
}

void MainWindow::onUserManageClicked()
{
    if (!AuthManager::instance()->canManageUsers()) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"),
                             QString::fromUtf8("仅管理员可管理用户"));
        return;
    }
    UserManageDialog dlg(this);
    dlg.exec();
}
