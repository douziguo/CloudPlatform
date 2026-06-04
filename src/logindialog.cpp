#include "logindialog.h"
#include "authmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QApplication>
#include <QSettings>
#include <QEventLoop>
#include <QDebug>
#include <QHeaderView>
#include <QInputDialog>
#include <QGroupBox>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("CloudPlatform - 登录");
    setFixedSize(480, 420);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);

    // 初始化 AuthManager 数据库
    AuthManager::instance()->initDatabase();

    initLoginTab();
    initManageTab();

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(m_loginPage, "登录");
    m_tabWidget->addTab(m_managePage, "用户管理");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->addWidget(m_tabWidget);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &LoginDialog::onTabChanged);

    loadSettings();
}

LoginDialog::~LoginDialog()
{
}

void LoginDialog::initLoginTab()
{
    m_loginPage = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(m_loginPage);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 10);

    // 标题
    QLabel *titleLabel = new QLabel("CloudPlatform");
    titleLabel->setObjectName("titleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    QLabel *subTitle = new QLabel("测量数据管理系统");
    subTitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subTitle);

    // 表单
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setSpacing(10);
    formLayout->setLabelAlignment(Qt::AlignRight);

    m_userEdit = new QLineEdit();
    m_userEdit->setPlaceholderText("请输入用户名");
    m_userEdit->setMinimumHeight(36);
    formLayout->addRow("用户名:", m_userEdit);

    m_passEdit = new QLineEdit();
    m_passEdit->setPlaceholderText("请输入密码");
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setMinimumHeight(36);
    formLayout->addRow("密码:", m_passEdit);

    // 角色提示（自动识别，无需选择）
    m_roleLabel = new QLabel("角色由系统自动分配，登录后可见");
    m_roleLabel->setStyleSheet("color: #969696; font-size: 11px;");
    formLayout->addRow("", m_roleLabel);

    mainLayout->addLayout(formLayout);

    // 记住密码
    m_rememberCheck = new QCheckBox("记住密码");
    mainLayout->addWidget(m_rememberCheck);

    // 状态提示
    m_statusLabel = new QLabel("");
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    // 按钮行
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(15);

    m_exitBtn = new QPushButton("退出");
    m_exitBtn->setObjectName("exitBtn");
    m_exitBtn->setFixedHeight(38);
    m_exitBtn->setCursor(Qt::PointingHandCursor);
    connect(m_exitBtn, &QPushButton::clicked, this, &LoginDialog::onExitClicked);

    m_loginBtn = new QPushButton("登 录");
    m_loginBtn->setObjectName("loginBtn");
    m_loginBtn->setFixedHeight(38);
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    m_loginBtn->setDefault(true);
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);

    btnRow->addWidget(m_exitBtn);
    btnRow->addWidget(m_loginBtn);
    mainLayout->addLayout(btnRow);

    // 回车触发登录
    connect(m_userEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_passEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
}

void LoginDialog::initManageTab()
{
    m_managePage = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(m_managePage);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // 用户列表表格
    m_userTable = new QTableWidget();
    m_userTable->setColumnCount(5);
    m_userTable->setHorizontalHeaderLabels({"ID", "用户名", "角色", "创建时间", "最后登录"});
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setAlternatingRowColors(true);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_userTable);

    // 添加用户区域
    QGroupBox *addGroup = new QGroupBox("添加用户");
    QFormLayout *addForm = new QFormLayout(addGroup);

    m_newUserEdit = new QLineEdit();
    m_newUserEdit->setPlaceholderText("新用户名");
    addForm->addRow("用户名:", m_newUserEdit);

    m_newPassEdit = new QLineEdit();
    m_newPassEdit->setPlaceholderText("密码");
    m_newPassEdit->setEchoMode(QLineEdit::Password);
    addForm->addRow("密码:", m_newPassEdit);

    m_roleCombo = new QComboBox();
    m_roleCombo->addItem("外业人员（仅上传）", static_cast<int>(UserRole::FieldWorker));
    m_roleCombo->addItem("内业人员（查看/下载）", static_cast<int>(UserRole::OfficeWorker));
    m_roleCombo->addItem("客户（锁定账户）", static_cast<int>(UserRole::Customer));
    m_roleCombo->addItem("管理员（全部权限）", static_cast<int>(UserRole::Admin));
    addForm->addRow("角色:", m_roleCombo);

    QPushButton *addBtn = new QPushButton("添加用户");
    addBtn->setObjectName("addUserBtn");
    connect(addBtn, &QPushButton::clicked, this, &LoginDialog::onAddUserClicked);
    addForm->addRow("", addBtn);

    layout->addWidget(addGroup);

    // 删除按钮
    QPushButton *delBtn = new QPushButton("删除选中用户");
    delBtn->setObjectName("deleteUserBtn");
    connect(delBtn, &QPushButton::clicked, this, &LoginDialog::onDeleteUserClicked);
    layout->addWidget(delBtn);

    // 刷新按钮
    QPushButton *refreshBtn = new QPushButton("刷新列表");
    refreshBtn->setFixedHeight(28);
    connect(refreshBtn, &QPushButton::clicked, this, &LoginDialog::onRefreshUsersClicked);
    layout->addWidget(refreshBtn);

    loadUserTable();
}

void LoginDialog::loadUserTable()
{
    if (!m_userTable) return;

    auto users = AuthManager::instance()->getAllUsers();
    m_userTable->setRowCount(users.size());

    for (int i = 0; i < users.size(); ++i) {
        const auto &u = users[i];
        m_userTable->setItem(i, 0, new QTableWidgetItem(QString::number(u.id)));
        m_userTable->setItem(i, 1, new QTableWidgetItem(u.username));

        QString roleStr;
        switch (u.role) {
        case UserRole::Admin:       roleStr = "管理员"; break;
        case UserRole::FieldWorker:  roleStr = "外业人员"; break;
        case UserRole::OfficeWorker: roleStr = "内业人员"; break;
        case UserRole::Customer:    roleStr = "客户"; break;
        }
        m_userTable->setItem(i, 2, new QTableWidgetItem(roleStr));
        m_userTable->setItem(i, 3, new QTableWidgetItem(u.createdAt));
        m_userTable->setItem(i, 4, new QTableWidgetItem(u.lastLogin));

        // 管理员行标红
        if (u.role == UserRole::Admin) {
            for (int col = 0; col < 5; ++col) {
                m_userTable->item(i, col)->setForeground(Qt::red);
            }
        }
    }
}

void LoginDialog::onLoginClicked()
{
    QString user = m_userEdit->text().trimmed();
    QString pass = m_passEdit->text();

    if (user.isEmpty()) {
        m_statusLabel->setStyleSheet("color: #e74c3c;");
        m_statusLabel->setText("请输入用户名");
        m_userEdit->setFocus();
        return;
    }
    if (pass.isEmpty()) {
        m_statusLabel->setStyleSheet("color: #e74c3c;");
        m_statusLabel->setText("请输入密码");
        m_passEdit->setFocus();
        return;
    }

    m_loginBtn->setEnabled(false);
    m_loginBtn->setText("登录中...");
    m_statusLabel->setText("");
    m_statusLabel->setStyleSheet("color: #3498db;");
    QApplication::processEvents();

    bool ok = AuthManager::instance()->login(user, pass);

    if (ok) {
        m_username  = user;
        m_role     = AuthManager::instance()->roleString();
        m_isLoggedIn = true;

        saveSettings();
        qDebug() << "[Login] 登录成功:" << user << "角色:" << m_role;
        accept();
    } else {
        m_statusLabel->setStyleSheet("color: #e74c3c;");
        m_statusLabel->setText("用户名或密码错误");
        m_passEdit->clear();
        m_passEdit->setFocus();
    }

    m_loginBtn->setEnabled(true);
    m_loginBtn->setText("登 录");
}

void LoginDialog::onExitClicked()
{
    reject();
}

void LoginDialog::onTabChanged(int index)
{
    // 切换到用户管理页时，检查权限
    if (index == 1) {
        if (!AuthManager::instance()->isLoggedIn()) {
            QMessageBox::information(this, "提示", "请先登录");
            m_tabWidget->setCurrentIndex(0);
            return;
        }
        if (!AuthManager::instance()->isAdmin()) {
            QMessageBox::warning(this, "权限不足", "仅管理员可管理用户");
            m_tabWidget->setCurrentIndex(0);
            return;
        }
        loadUserTable();
    }
}

void LoginDialog::onAddUserClicked()
{
    if (!AuthManager::instance()->isAdmin()) {
        QMessageBox::warning(this, "权限不足", "仅管理员可添加用户");
        return;
    }

    QString username = m_newUserEdit->text().trimmed();
    QString password = m_newPassEdit->text();
    UserRole role = static_cast<UserRole>(m_roleCombo->currentData().toInt());

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写用户名和密码");
        return;
    }

    bool ok = AuthManager::instance()->addUser(username, password, role);
    if (ok) {
        QMessageBox::information(this, "成功", QString("用户 [%1] 添加成功").arg(username));
        m_newUserEdit->clear();
        m_newPassEdit->clear();
        loadUserTable();
    } else {
        QMessageBox::warning(this, "失败", "添加用户失败，用户名可能已存在");
    }
}

void LoginDialog::onDeleteUserClicked()
{
    if (!AuthManager::instance()->isAdmin()) {
        QMessageBox::warning(this, "权限不足", "仅管理员可删除用户");
        return;
    }

    int row = m_userTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的用户");
        return;
    }

    int   userId   = m_userTable->item(row, 0)->text().toInt();
    QString username = m_userTable->item(row, 1)->text();

    if (userId == 1) {
        QMessageBox::warning(this, "提示", "默认管理员账户不可删除");
        return;
    }

    auto reply = QMessageBox::question(this, "确认删除",
                                       QString("确定要删除用户 [%1] 吗？").arg(username));
    if (reply != QMessageBox::Yes) return;

    if (AuthManager::instance()->deleteUser(userId)) {
        QMessageBox::information(this, "成功", "用户已删除");
        loadUserTable();
    } else {
        QMessageBox::warning(this, "失败", "删除用户失败");
    }
}

void LoginDialog::onRefreshUsersClicked()
{
    loadUserTable();
}

void LoginDialog::saveSettings()
{
    QSettings settings("CloudPlatform", "CloudPlatform");
    if (m_rememberCheck->isChecked()) {
        settings.setValue("remember", true);
        settings.setValue("username", m_userEdit->text().trimmed());
        // 注意：密码不应明文存储，这里仅存储用户名
    } else {
        settings.setValue("remember", false);
        settings.remove("username");
    }
}

void LoginDialog::loadSettings()
{
    QSettings settings("CloudPlatform", "CloudPlatform");
    if (settings.value("remember", false).toBool()) {
        m_rememberCheck->setChecked(true);
        m_userEdit->setText(settings.value("username").toString());
        m_passEdit->setFocus();
    }
}
