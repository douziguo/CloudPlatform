#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QDateTime>
#include <QVariantMap>
#include <QVariantList>

// 用户角色枚举
enum class UserRole {
    Admin,          // 超级管理员：admin 账户，查看所有项目+用户管理
    FieldWorker,    // 外业人员：仅上传、查看自己上传的文件
    OfficeWorker,   // 内业人员：查看下载所有文件、管理测量任务
    Customer        // 管理员：管理自己客户公司的项目全部权限
};

// 用户信息结构体
struct UserInfo {
    int     id = -1;
    QString username;
    QString password;   // 加密存储
    UserRole role = UserRole::FieldWorker;
    QString clientName; // 关联的客户账户名（非空=绑定客户）
    QString createdAt;
    QString lastLogin;
};

// 下载记录结构体
struct DownloadRecord {
    int     id;
    QString username;
    QString remotePath;
    QString fileName;
    QString localPath;
    qint64  fileSize;
    QString downloadedAt;
};

// 项目信息结构体
struct ProjectInfo {
    int     id = -1;
    QString name;
    QString code;
    QString location;
    QString client;        // 委托单位
    QString clientName;    // 关联的客户账户名（权限过滤用）
    QString description;
    QString notes;
    QString createdBy;
    QString createdAt;
    QString updatedAt;
};

// 测量任务结构体
struct MeasureTask {
    int     id = -1;
    int     projectId = -1;
    QString taskName;
    QString measureDate;
    QString personnel;
    QString pipeStart;
    QString pipeEnd;
    QString pipeMaterial;
    QString pipeDiameter;
    QString pipeLength;
    QString status;      // pending/in_progress/completed
    int     progress = 0; // 0-100
    QString notes;
    QString createdAt;
    QString updatedAt;
};

class AuthManager : public QObject
{
    Q_OBJECT

public:
    static AuthManager* instance();

    // 初始化数据库（dbPath 为空时使用默认本地临时路径）
    bool initDatabase(const QString &dbPath = "");

    // 远程数据库同步
    // 从服务器下载 db 到本地临时路径，返回本地路径（失败返回空字符串）
    QString syncDbFromServer();
    // 将当前本地 db 上传回服务器（异步，完成后记录日志）
    void syncDbToServer();

    // 获取本地 db 临时路径
    QString localDbPath() const { return m_localDbPath; }

    // 服务器 db 路径（固定）
    static QString remoteDbPath() { return "/home/ubuntu/cloudplatform_users.db"; }

    // 用户认证
    bool login(const QString &username, const QString &password);
    void logout();

    // 当前用户信息
    bool isLoggedIn() const { return m_isLoggedIn; }
    QString currentUser() const { return m_currentUser; }
    UserRole currentRole() const { return m_currentRole; }
    QString roleString() const;
    bool isAdmin() const { return m_currentRole == UserRole::Admin; }
    bool isFieldWorker() const { return m_currentRole == UserRole::FieldWorker; }
    bool isOfficeWorker() const { return m_currentRole == UserRole::OfficeWorker; }
    bool isCustomer() const { return m_currentRole == UserRole::Customer; }
    bool isCompanyAdmin() const { return isAdmin() || isCustomer(); }  // 可管理自己公司项目

    // 权限判断
    bool canUpload() const;       // 上传：外业+内业+管理员
    bool canDownload() const;     // 下载：内业+管理员
    bool canDelete() const;       // 删除：仅管理员
    bool canManageUsers() const;  // 用户管理：仅管理员
    bool canManageProjects() const; // 项目管理：内业+管理员
    bool canViewAllFiles() const; // 查看所有文件：内业+管理员
    bool canOnlyViewSelf() const; // 仅查看自己上传：外业

    // 客户权限：获取当前用户关联的客户名
    QString currentUserClient() const;
    bool isSuperAdmin() const;    // 超级管理员：admin 账户，可查看所有项目

    // 用户管理（仅管理员）
    bool addUser(const QString &username, const QString &password, UserRole role, const QString &clientName = "");
    bool deleteUser(int userId);
    bool updateUserRole(int userId, UserRole newRole);
    bool updateUserClientName(int userId, const QString &clientName);
    bool updatePassword(int userId, const QString &newPassword);
    QList<UserInfo> getAllUsers();

    // === 文件传输记录 ===

    // 上传记录
    struct UploadRecord {
        int id;
        QString username;
        QString remotePath;
        QString fileName;
        QString uploadedAt;
    };
    QList<UploadRecord> getUploadRecords(const QString &remotePath = "");

    // 记录上传文件到数据库
    bool recordUpload(const QString &username, const QString &remotePath,
                      const QString &fileName, const QString &uploadedAt);

    // 下载记录
    QList<DownloadRecord> getDownloadRecords();

    // 记录下载文件到数据库
    bool recordDownload(const QString &username, const QString &remotePath,
                         const QString &fileName, const QString &localPath,
                         qint64 fileSize, const QString &downloadedAt);

    // === 项目管理 ===

    // 项目 CRUD
    int addProject(const ProjectInfo &info);
    bool updateProject(int projectId, const ProjectInfo &info);
    bool deleteProject(int projectId);
    ProjectInfo getProject(int projectId);
    QList<ProjectInfo> getAllProjects();

    // 测量任务 CRUD
    int addMeasureTask(const MeasureTask &task);
    bool updateMeasureTask(int taskId, const MeasureTask &task);
    bool deleteMeasureTask(int taskId);
    MeasureTask getMeasureTask(int taskId);
    QList<MeasureTask> getMeasureTasks(int projectId);

    // 获取用户密码盐值（用于上传路径生成）
    QString getUserSalt(const QString &username);

signals:
    void loginSuccess(const QString &username, const QString &role);
    void loginFailed(const QString &reason);
    void loggedOut();
    void dbSyncToServerFinished(bool success);  // 上传同步完成信号

private:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager();

    // 密码加密（SHA-256 + salt）
    QString hashPassword(const QString &password, const QString &salt = "");
    QString generateSalt();

    // 确保 admin 默认账户存在
    void ensureDefaultAdmin();

    // 创建项目管理相关表
    void createProjectTables();

    bool           m_isLoggedIn = false;
    QString        m_currentUser;
    UserRole       m_currentRole = UserRole::FieldWorker;
    QString        m_currentClientName;  // 当前用户关联的客户名

    QString        m_localDbPath;        // 本地临时 db 文件路径

    QSqlDatabase   m_db;
    static AuthManager *m_instance;
};

#endif // AUTHMANAGER_H
