#include "usermanagedialog.h"
#include "authmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QHeaderView>
#include <QInputDialog>

UserManageDialog::UserManageDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("用户管理"));
    setMinimumSize(520, 480);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    initUI();
    loadUserTable();
}

UserManageDialog::~UserManageDialog() {}

void UserManageDialog::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // === 用户列表 ===
    m_userTable = new QTableWidget();
    m_userTable->setColumnCount(6);
    m_userTable->setHorizontalHeaderLabels({
        QString::fromUtf8("ID"),
        QString::fromUtf8("用户名"),
        QString::fromUtf8("角色"),
        QString::fromUtf8("客户账户"),
        QString::fromUtf8("创建时间"),
        QString::fromUtf8("最后登录")
    });
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setAlternatingRowColors(true);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->setMinimumHeight(180);
    mainLayout->addWidget(m_userTable);

    // === 添加用户区域 ===
    QGroupBox *addGroup = new QGroupBox(QString::fromUtf8("添加用户"));
    QFormLayout *addForm = new QFormLayout(addGroup);
    addForm->setLabelAlignment(Qt::AlignRight);

    m_newUserEdit = new QLineEdit();
    m_newUserEdit->setPlaceholderText(QString::fromUtf8("新用户名"));
    addForm->addRow(QString::fromUtf8("用户名:"), m_newUserEdit);

    m_newPassEdit = new QLineEdit();
    m_newPassEdit->setPlaceholderText(QString::fromUtf8("密码"));
    m_newPassEdit->setEchoMode(QLineEdit::Password);
    addForm->addRow(QString::fromUtf8("密码:"), m_newPassEdit);

    m_roleCombo = new QComboBox();
    m_roleCombo->addItem(QString::fromUtf8("外业人员"), static_cast<int>(UserRole::FieldWorker));
    m_roleCombo->addItem(QString::fromUtf8("内业人员"), static_cast<int>(UserRole::OfficeWorker));
    m_roleCombo->addItem(QString::fromUtf8("管理员"), static_cast<int>(UserRole::Customer));
    m_roleCombo->addItem(QString::fromUtf8("超级管理员"), static_cast<int>(UserRole::Admin));
    addForm->addRow(QString::fromUtf8("角色:"), m_roleCombo);

    m_clientNameEdit = new QLineEdit();
    m_clientNameEdit->setPlaceholderText(QString::fromUtf8("关联的客户账户名（必填）"));
    addForm->addRow(QString::fromUtf8("客户账户:"), m_clientNameEdit);

    QHBoxLayout *addBtnLayout = new QHBoxLayout();
    addBtnLayout->addStretch();
    m_addBtn = new QPushButton(QString::fromUtf8("添加用户"));
    m_addBtn->setObjectName("addUserBtn");
    connect(m_addBtn, &QPushButton::clicked, this, &UserManageDialog::onAddUserClicked);
    addBtnLayout->addWidget(m_addBtn);
    addForm->addRow("", addBtnLayout);

    mainLayout->addWidget(addGroup);

    // === 操作按钮区 ===
    QHBoxLayout *opLayout = new QHBoxLayout();
    opLayout->setSpacing(10);

    m_delBtn = new QPushButton(QString::fromUtf8("删除选中用户"));
    m_delBtn->setObjectName("deleteUserBtn");
    connect(m_delBtn, &QPushButton::clicked, this, &UserManageDialog::onDeleteUserClicked);

    m_resetBtn = new QPushButton(QString::fromUtf8("重置密码"));
    m_resetBtn->setObjectName("resetPasswordBtn");
    connect(m_resetBtn, &QPushButton::clicked, this, &UserManageDialog::onResetPasswordClicked);

    m_refreshBtn = new QPushButton(QString::fromUtf8("刷新列表"));
    m_refreshBtn->setFixedHeight(28);
    connect(m_refreshBtn, &QPushButton::clicked, this, &UserManageDialog::onRefreshUsersClicked);

    opLayout->addWidget(m_delBtn);
    opLayout->addWidget(m_resetBtn);
    opLayout->addStretch();
    opLayout->addWidget(m_refreshBtn);

    mainLayout->addLayout(opLayout);

    // === 权限控制：非超级管理员锁定客户账户字段 ===
    m_isSuperAdmin = AuthManager::instance()->isSuperAdmin();
    if (!m_isSuperAdmin) {
        QString myClient = AuthManager::instance()->currentUserClient();
        if (myClient.isEmpty()) {
            myClient = AuthManager::instance()->currentUser();
        }
        m_clientNameEdit->setText(myClient);
        m_clientNameEdit->setEnabled(false);
        m_clientNameEdit->setPlaceholderText(QString::fromUtf8("自动绑定当前客户账户"));

        // 移除"超级管理员"角色选项（仅超级管理员可创建）
        for (int i = 0; i < m_roleCombo->count(); ++i) {
            if (m_roleCombo->itemData(i).toInt() == static_cast<int>(UserRole::Admin)) {
                m_roleCombo->removeItem(i);
                break;
            }
        }
    }
}

void UserManageDialog::loadUserTable()
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
        case UserRole::Admin:       roleStr = QString::fromUtf8("超级管理员"); break;
        case UserRole::FieldWorker:  roleStr = QString::fromUtf8("外业人员"); break;
        case UserRole::OfficeWorker: roleStr = QString::fromUtf8("内业人员"); break;
        case UserRole::Customer:    roleStr = QString::fromUtf8("管理员"); break;
        }
        m_userTable->setItem(i, 2, new QTableWidgetItem(roleStr));
        m_userTable->setItem(i, 3, new QTableWidgetItem(u.clientName.isEmpty() ? "-" : u.clientName));
        m_userTable->setItem(i, 4, new QTableWidgetItem(u.createdAt));
        m_userTable->setItem(i, 5, new QTableWidgetItem(u.lastLogin));

        // 超级管理员行标红，管理员行标蓝
        if (u.role == UserRole::Admin) {
            for (int col = 0; col < 6; ++col) {
                m_userTable->item(i, col)->setForeground(Qt::red);
            }
        } else if (u.role == UserRole::Customer) {
            for (int col = 0; col < 6; ++col) {
                m_userTable->item(i, col)->setForeground(Qt::blue);
            }
        }
    }
}

void UserManageDialog::onAddUserClicked()
{
    if (!AuthManager::instance()->canManageUsers()) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"), QString::fromUtf8("仅管理员可添加用户"));
        return;
    }

    QString username = m_newUserEdit->text().trimmed();
    QString password = m_newPassEdit->text();
    UserRole role = static_cast<UserRole>(m_roleCombo->currentData().toInt());

    // 客户账户名：超级管理员手动填写，其他管理员自动绑定为自己的客户名
    QString clientName;
    if (m_isSuperAdmin) {
        clientName = m_clientNameEdit->text().trimmed();
    } else {
        clientName = AuthManager::instance()->currentUserClient();
        if (clientName.isEmpty()) {
            clientName = AuthManager::instance()->currentUser();
        }
    }

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请填写用户名和密码"));
        return;
    }

    // 客户账户名必填
    if (clientName.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("必须填写客户账户名，用于关联项目权限隔离"));
        m_clientNameEdit->setFocus();
        return;
    }

    bool ok = AuthManager::instance()->addUser(username, password, role, clientName);
    if (ok) {
        QMessageBox::information(this, QString::fromUtf8("成功"), QString::fromUtf8("用户 [%1] 添加成功").arg(username));
        m_newUserEdit->clear();
        m_newPassEdit->clear();
        // 超级管理员清空客户账户输入框；普通管理员重置为锁定值
        if (m_isSuperAdmin) {
            m_clientNameEdit->clear();
        } else {
            QString myClient = AuthManager::instance()->currentUserClient();
            if (myClient.isEmpty()) myClient = AuthManager::instance()->currentUser();
            m_clientNameEdit->setText(myClient);
        }
        loadUserTable();
    } else {
        QMessageBox::warning(this, QString::fromUtf8("失败"), QString::fromUtf8("添加用户失败，用户名可能已存在"));
    }
}

void UserManageDialog::onDeleteUserClicked()
{
    if (!AuthManager::instance()->canManageUsers()) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"), QString::fromUtf8("仅管理员可删除用户"));
        return;
    }

    int row = m_userTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先选中要删除的用户"));
        return;
    }

    int userId = m_userTable->item(row, 0)->text().toInt();
    QString username = m_userTable->item(row, 1)->text();
    QString roleStr = m_userTable->item(row, 2)->text();

    // 超级管理员不可删除
    if (userId == 1) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("超级管理员账户不可删除"));
        return;
    }

    // 非超级管理员不能删除其他管理员（含超级管理员和公司管理员）
    if (!AuthManager::instance()->isSuperAdmin() && roleStr == QString::fromUtf8("管理员")) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"),
                             QString::fromUtf8("仅超级管理员可删除管理员账户"));
        return;
    }

    auto reply = QMessageBox::question(this, QString::fromUtf8("确认删除"),
                                       QString::fromUtf8("确定要删除用户 [%1] 吗？").arg(username));
    if (reply != QMessageBox::Yes) return;

    if (AuthManager::instance()->deleteUser(userId)) {
        QMessageBox::information(this, QString::fromUtf8("成功"), QString::fromUtf8("用户已删除"));
        loadUserTable();
    } else {
        QMessageBox::warning(this, QString::fromUtf8("失败"), QString::fromUtf8("删除用户失败"));
    }
}

void UserManageDialog::onResetPasswordClicked()
{
    if (!AuthManager::instance()->canManageUsers()) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"), QString::fromUtf8("仅管理员可重置密码"));
        return;
    }

    int row = m_userTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先选中要重置密码的用户"));
        return;
    }

    int userId = m_userTable->item(row, 0)->text().toInt();
    QString username = m_userTable->item(row, 1)->text();
    bool ok;
    QString newPass = QInputDialog::getText(this, QString::fromUtf8("重置密码"),
                                            QString::fromUtf8("为用户 [%1] 设置新密码:").arg(username),
                                            QLineEdit::Password, QString(), &ok);
    if (!ok || newPass.isEmpty()) return;

    if (AuthManager::instance()->updatePassword(userId, newPass)) {
        QMessageBox::information(this, QString::fromUtf8("成功"), QString::fromUtf8("密码已重置"));
    } else {
        QMessageBox::warning(this, QString::fromUtf8("失败"), QString::fromUtf8("重置密码失败"));
    }
}

void UserManageDialog::onRefreshUsersClicked()
{
    loadUserTable();
}
