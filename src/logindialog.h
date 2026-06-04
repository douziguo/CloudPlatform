#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>

// 前置声明
class AuthManager;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

    // 获取登录后的用户信息
    QString username() const { return m_username; }
    QString role() const { return m_role; }
    bool    isLoggedIn() const { return m_isLoggedIn; }

signals:
    void loginSuccess(const QString &username, const QString &role);
    void loginCancelled();

private slots:
    void onLoginClicked();
    void onExitClicked();
    void onTabChanged(int index);
    void onAddUserClicked();
    void onDeleteUserClicked();
    void onRefreshUsersClicked();

private:
    void initLoginTab();
    void initManageTab();
    void loadUserTable();
    void saveSettings();
    void loadSettings();

    QTabWidget   *m_tabWidget    = nullptr;
    QWidget       *m_loginPage    = nullptr;
    QWidget       *m_managePage   = nullptr;
    // 登录页
    QLineEdit    *m_userEdit     = nullptr;
    QLineEdit    *m_passEdit     = nullptr;
    QLabel       *m_roleLabel    = nullptr;
    QCheckBox    *m_rememberCheck = nullptr;
    QLabel       *m_statusLabel  = nullptr;
    QPushButton  *m_loginBtn     = nullptr;
    QPushButton  *m_exitBtn      = nullptr;
    // 管理页
    QTableWidget *m_userTable    = nullptr;
    QLineEdit    *m_newUserEdit  = nullptr;
    QLineEdit    *m_newPassEdit  = nullptr;
    QComboBox    *m_roleCombo    = nullptr;
    // 状态
    QString       m_username;
    QString       m_role;
    bool          m_isLoggedIn   = false;
};

#endif // LOGINDIALOG_H
