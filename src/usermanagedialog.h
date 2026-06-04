#ifndef USERMANAGEDIALOG_H
#define USERMANAGEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>

class UserManageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserManageDialog(QWidget *parent = nullptr);
    ~UserManageDialog();

private slots:
    void onAddUserClicked();
    void onDeleteUserClicked();
    void onRefreshUsersClicked();
    void onResetPasswordClicked();

private:
    void initUI();
    void loadUserTable();

    QTableWidget *m_userTable    = nullptr;
    QLineEdit    *m_newUserEdit  = nullptr;
    QLineEdit    *m_newPassEdit  = nullptr;
    QComboBox    *m_roleCombo    = nullptr;
    QLineEdit    *m_clientNameEdit = nullptr;  // 客户账户名
    QPushButton  *m_addBtn       = nullptr;
    QPushButton  *m_delBtn       = nullptr;
    QPushButton  *m_resetBtn     = nullptr;
    QPushButton  *m_refreshBtn   = nullptr;
    bool          m_isSuperAdmin  = false;     // 当前登录用户是否为超级管理员
};

#endif // USERMANAGEDIALOG_H
