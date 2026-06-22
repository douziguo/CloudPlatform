#include "authmanager.h"
#include "sshmanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include <QThread>
#include <QEventLoop>
#include <QProcess>

AuthManager* AuthManager::m_instance = nullptr;

AuthManager* AuthManager::instance()
{
    if (!m_instance) {
        m_instance = new AuthManager();
    }
    return m_instance;
}

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
{
}

AuthManager::~AuthManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool AuthManager::initDatabase(const QString &dbPath)
{
    QString dbDir;
    if (dbPath.isEmpty()) {
        // 优先使用之前 syncDbFromServer() 下载到的本地路径
        if (!m_localDbPath.isEmpty()) {
            QFileInfo fi(m_localDbPath);
            dbDir = fi.absolutePath();
        } else {
            dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (dbDir.isEmpty()) {
                dbDir = QCoreApplication::applicationDirPath();
            }
        }
    } else {
        dbDir = dbPath;
    }

    QDir dir(dbDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 若已通过 syncDbFromServer() 下载，直接使用该文件；否则用默认文件名
    QString dbFile;
    if (!m_localDbPath.isEmpty() && QFile::exists(m_localDbPath)) {
        dbFile = m_localDbPath;
    } else {
        dbFile = dbDir + "/cloudplatform_users.db";
        m_localDbPath = dbFile;  // 记录路径，供后续同步使用
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", "auth_connection");
    m_db.setDatabaseName(dbFile);

    if (!m_db.open()) {
        qDebug() << "[AuthManager] 无法打开数据库:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  salt TEXT NOT NULL,"
        "  role INTEGER NOT NULL DEFAULT 1,"
        "  client_name TEXT DEFAULT '',"
        "  created_at TEXT NOT NULL,"
        "  last_login TEXT"
        ")"
    );

    // 上传记录表
    QSqlQuery uploadTableQuery(m_db);
    bool uploadTableOk = uploadTableQuery.exec(
        "CREATE TABLE IF NOT EXISTS file_uploads ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT NOT NULL,"
        "  remote_path TEXT NOT NULL,"
        "  file_name TEXT NOT NULL,"
        "  uploaded_at TEXT NOT NULL"
        ")"
    );
    if (!uploadTableOk) {
        qDebug() << "[AuthManager] 创建 file_uploads 表失败:" << uploadTableQuery.lastError().text();
    } else {
        qDebug() << "[AuthManager] file_uploads 表已就绪";
    }

    // 下载记录表
    QSqlQuery downloadTableQuery(m_db);
    bool downloadTableOk = downloadTableQuery.exec(
        "CREATE TABLE IF NOT EXISTS file_downloads ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT NOT NULL,"
        "  remote_path TEXT NOT NULL,"
        "  file_name TEXT NOT NULL,"
        "  local_path TEXT,"
        "  file_size INTEGER DEFAULT 0,"
        "  downloaded_at TEXT NOT NULL"
        ")"
    );
    if (!downloadTableOk) {
        qDebug() << "[AuthManager] 创建 file_downloads 表失败:" << downloadTableQuery.lastError().text();
    } else {
        qDebug() << "[AuthManager] file_downloads 表已就绪";
    }

    if (!ok) {
        qDebug() << "[AuthManager] 创建表失败:" << query.lastError().text();
        return false;
    }

    // 自动迁移：为旧 users 表补 client_name 列
    QSqlQuery(m_db).exec("ALTER TABLE users ADD COLUMN client_name TEXT DEFAULT ''");
    // 自动迁移：为旧 projects 表补 client_name 列
    QSqlQuery(m_db).exec("ALTER TABLE projects ADD COLUMN client_name TEXT DEFAULT ''");

    // 创建项目管理相关表
    createProjectTables();

    qDebug() << "[AuthManager] 数据库初始化成功:" << dbFile;
    ensureDefaultAdmin();
    return true;
}

// ========================================================================
// 项目管理相关表
// ========================================================================
void AuthManager::createProjectTables()
{
    // 项目表
    QSqlQuery projQuery(m_db);
    bool projOk = projQuery.exec(
        "CREATE TABLE IF NOT EXISTS projects ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  code TEXT,"
        "  location TEXT,"
        "  client TEXT,"
        "  client_name TEXT DEFAULT '',"
        "  description TEXT,"
        "  notes TEXT,"
        "  created_by TEXT NOT NULL,"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL"
        ")"
    );
    if (!projOk) {
        qDebug() << "[AuthManager] 创建 projects 表失败:" << projQuery.lastError().text();
    } else {
        qDebug() << "[AuthManager] projects 表已就绪";
    }

    // 测量任务表
    QSqlQuery taskQuery(m_db);
    bool taskOk = taskQuery.exec(
        "CREATE TABLE IF NOT EXISTS measurement_tasks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  project_id INTEGER NOT NULL,"
        "  task_name TEXT NOT NULL,"
        "  measure_date TEXT,"
        "  personnel TEXT,"
        "  pipe_start TEXT,"
        "  pipe_end TEXT,"
        "  pipe_material TEXT,"
        "  pipe_diameter TEXT,"
        "  pipe_length TEXT,"
        "  status TEXT DEFAULT 'pending',"
        "  progress INTEGER DEFAULT 0,"
        "  notes TEXT,"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL,"
        "  FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE"
        ")"
    );
    if (!taskOk) {
        qDebug() << "[AuthManager] 创建 measurement_tasks 表失败:" << taskQuery.lastError().text();
    } else {
        qDebug() << "[AuthManager] measurement_tasks 表已就绪";
    }
}

// ========================================================================
// 下载记录
// ========================================================================
bool AuthManager::recordDownload(const QString &username, const QString &remotePath,
                                 const QString &fileName, const QString &localPath,
                                 qint64 fileSize, const QString &downloadedAt)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO file_downloads (username, remote_path, file_name, local_path, file_size, downloaded_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(username);
    query.addBindValue(remotePath);
    query.addBindValue(fileName);
    query.addBindValue(localPath);
    query.addBindValue(fileSize);
    query.addBindValue(downloadedAt);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 记录下载失败:" << query.lastError().text();
        return false;
    }
    qDebug() << "[AuthManager] 记录下载:" << username << remotePath << fileName;
    return true;
}

QList<DownloadRecord> AuthManager::getDownloadRecords()
{
    QList<DownloadRecord> records;
    if (!m_db.isOpen()) return records;

    QSqlQuery query(m_db);
    query.prepare("SELECT id, username, remote_path, file_name, local_path, file_size, downloaded_at "
                  "FROM file_downloads ORDER BY downloaded_at DESC");
    if (!query.exec()) return records;

    while (query.next()) {
        DownloadRecord rec;
        rec.id          = query.value(0).toInt();
        rec.username    = query.value(1).toString();
        rec.remotePath  = query.value(2).toString();
        rec.fileName    = query.value(3).toString();
        rec.localPath   = query.value(4).toString();
        rec.fileSize    = query.value(5).toLongLong();
        rec.downloadedAt= query.value(6).toString();
        records.append(rec);
    }
    return records;
}

// ========================================================================
// 项目 CRUD
// ========================================================================
int AuthManager::addProject(const ProjectInfo &info)
{
    if (!m_db.isOpen()) return -1;

    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO projects (name, code, location, client, client_name, description, notes, created_by, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(info.name);
    query.addBindValue(info.code);
    query.addBindValue(info.location);
    query.addBindValue(info.client);
    query.addBindValue(info.clientName);
    query.addBindValue(info.description);
    query.addBindValue(info.notes);
    query.addBindValue(info.createdBy);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 添加项目失败:" << query.lastError().text();
        return -1;
    }

    int id = query.lastInsertId().toInt();
    qDebug() << "[AuthManager] 添加项目成功: id=" << id << info.name;
    syncDbToServer();  // 写操作后同步到服务器
    return id;
}

bool AuthManager::updateProject(int projectId, const ProjectInfo &info)
{
    if (!m_db.isOpen()) return false;

    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QSqlQuery query(m_db);
    query.prepare(
        "UPDATE projects SET name=?, code=?, location=?, client=?, client_name=?, description=?, notes=?, updated_at=? "
        "WHERE id=?"
    );
    query.addBindValue(info.name);
    query.addBindValue(info.code);
    query.addBindValue(info.location);
    query.addBindValue(info.client);
    query.addBindValue(info.clientName);
    query.addBindValue(info.description);
    query.addBindValue(info.notes);
    query.addBindValue(now);
    query.addBindValue(projectId);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 更新项目失败:" << query.lastError().text();
        return false;
    }
    syncDbToServer();  // 写操作后同步到服务器
    return true;
}

bool AuthManager::deleteProject(int projectId)
{
    if (!m_db.isOpen()) return false;

    // 先删除关联的测量任务
    QSqlQuery delTasks(m_db);
    delTasks.prepare("DELETE FROM measurement_tasks WHERE project_id=?");
    delTasks.addBindValue(projectId);
    delTasks.exec();

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM projects WHERE id=?");
    query.addBindValue(projectId);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 删除项目失败:" << query.lastError().text();
        return false;
    }
    qDebug() << "[AuthManager] 删除项目成功: id=" << projectId;
    syncDbToServer();  // 写操作后同步到服务器
    return true;
}

ProjectInfo AuthManager::getProject(int projectId)
{
    ProjectInfo info;
    if (!m_db.isOpen()) return info;

    QSqlQuery query(m_db);
    query.prepare("SELECT id, name, code, location, client, client_name, description, notes, created_by, created_at, updated_at "
                  "FROM projects WHERE id=?");
    query.addBindValue(projectId);

    if (query.exec() && query.next()) {
        info.id          = query.value(0).toInt();
        info.name        = query.value(1).toString();
        info.code        = query.value(2).toString();
        info.location    = query.value(3).toString();
        info.client      = query.value(4).toString();
        info.clientName  = query.value(5).toString();
        info.description = query.value(6).toString();
        info.notes       = query.value(7).toString();
        info.createdBy   = query.value(8).toString();
        info.createdAt   = query.value(9).toString();
        info.updatedAt   = query.value(10).toString();
    }
    return info;
}

QList<ProjectInfo> AuthManager::getAllProjects()
{
    QList<ProjectInfo> list;
    if (!m_db.isOpen()) return list;

    QSqlQuery query(m_db);
    if (isSuperAdmin()) {
        // 超级管理员（admin）查看所有项目
        query.prepare("SELECT id, name, code, location, client, client_name, description, notes, created_by, created_at, updated_at "
                      "FROM projects ORDER BY created_at DESC");
    } else {
        // 其他角色：只看自己关联客户名下的项目
        QString clientName = m_currentClientName;
        if (clientName.isEmpty()) {
            clientName = m_currentUser;  // 没有绑定客户名，用用户名作为客户名
        }
        query.prepare("SELECT id, name, code, location, client, client_name, description, notes, created_by, created_at, updated_at "
                      "FROM projects WHERE client_name=? OR client_name=? ORDER BY created_at DESC");
        query.addBindValue(clientName);
        query.addBindValue(m_currentUser);  // 也匹配以用户名为客户名的项目
    }
    if (!query.exec()) return list;

    while (query.next()) {
        ProjectInfo info;
        info.id          = query.value(0).toInt();
        info.name        = query.value(1).toString();
        info.code        = query.value(2).toString();
        info.location    = query.value(3).toString();
        info.client      = query.value(4).toString();
        info.clientName  = query.value(5).toString();
        info.description = query.value(6).toString();
        info.notes       = query.value(7).toString();
        info.createdBy   = query.value(8).toString();
        info.createdAt   = query.value(9).toString();
        info.updatedAt   = query.value(10).toString();
        list.append(info);
    }
    return list;
}

// ========================================================================
// 测量任务 CRUD
// ========================================================================
int AuthManager::addMeasureTask(const MeasureTask &task)
{
    if (!m_db.isOpen()) return -1;

    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO measurement_tasks "
        "(project_id, task_name, measure_date, personnel, pipe_start, pipe_end, "
        "pipe_material, pipe_diameter, pipe_length, status, progress, notes, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(task.projectId);
    query.addBindValue(task.taskName);
    query.addBindValue(task.measureDate);
    query.addBindValue(task.personnel);
    query.addBindValue(task.pipeStart);
    query.addBindValue(task.pipeEnd);
    query.addBindValue(task.pipeMaterial);
    query.addBindValue(task.pipeDiameter);
    query.addBindValue(task.pipeLength);
    query.addBindValue(task.status);
    query.addBindValue(task.progress);
    query.addBindValue(task.notes);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 添加测量任务失败:" << query.lastError().text();
        return -1;
    }

    int id = query.lastInsertId().toInt();
    qDebug() << "[AuthManager] 添加测量任务成功: id=" << id << task.taskName;
    syncDbToServer();  // 写操作后同步到服务器
    return id;
}

bool AuthManager::updateMeasureTask(int taskId, const MeasureTask &task)
{
    if (!m_db.isOpen()) return false;

    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QSqlQuery query(m_db);
    query.prepare(
        "UPDATE measurement_tasks SET "
        "task_name=?, measure_date=?, personnel=?, pipe_start=?, pipe_end=?, "
        "pipe_material=?, pipe_diameter=?, pipe_length=?, status=?, progress=?, notes=?, updated_at=? "
        "WHERE id=?"
    );
    query.addBindValue(task.taskName);
    query.addBindValue(task.measureDate);
    query.addBindValue(task.personnel);
    query.addBindValue(task.pipeStart);
    query.addBindValue(task.pipeEnd);
    query.addBindValue(task.pipeMaterial);
    query.addBindValue(task.pipeDiameter);
    query.addBindValue(task.pipeLength);
    query.addBindValue(task.status);
    query.addBindValue(task.progress);
    query.addBindValue(task.notes);
    query.addBindValue(now);
    query.addBindValue(taskId);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 更新测量任务失败:" << query.lastError().text();
        return false;
    }
    syncDbToServer();  // 写操作后同步到服务器
    return true;
}

bool AuthManager::deleteMeasureTask(int taskId)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM measurement_tasks WHERE id=?");
    query.addBindValue(taskId);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 删除测量任务失败:" << query.lastError().text();
        return false;
    }
    qDebug() << "[AuthManager] 删除测量任务成功: id=" << taskId;
    syncDbToServer();  // 写操作后同步到服务器
    return true;
}

MeasureTask AuthManager::getMeasureTask(int taskId)
{
    MeasureTask task;
    if (!m_db.isOpen()) return task;

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, project_id, task_name, measure_date, personnel, pipe_start, pipe_end, "
        "pipe_material, pipe_diameter, pipe_length, status, progress, notes, created_at, updated_at "
        "FROM measurement_tasks WHERE id=?"
    );
    query.addBindValue(taskId);

    if (query.exec() && query.next()) {
        task.id            = query.value(0).toInt();
        task.projectId     = query.value(1).toInt();
        task.taskName      = query.value(2).toString();
        task.measureDate   = query.value(3).toString();
        task.personnel     = query.value(4).toString();
        task.pipeStart     = query.value(5).toString();
        task.pipeEnd       = query.value(6).toString();
        task.pipeMaterial  = query.value(7).toString();
        task.pipeDiameter  = query.value(8).toString();
        task.pipeLength    = query.value(9).toString();
        task.status        = query.value(10).toString();
        task.progress      = query.value(11).toInt();
        task.notes         = query.value(12).toString();
        task.createdAt     = query.value(13).toString();
        task.updatedAt     = query.value(14).toString();
    }
    return task;
}

QList<MeasureTask> AuthManager::getMeasureTasks(int projectId)
{
    QList<MeasureTask> list;
    if (!m_db.isOpen()) return list;

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, project_id, task_name, measure_date, personnel, pipe_start, pipe_end, "
        "pipe_material, pipe_diameter, pipe_length, status, progress, notes, created_at, updated_at "
        "FROM measurement_tasks WHERE project_id=? ORDER BY created_at ASC"
    );
    query.addBindValue(projectId);

    if (!query.exec()) return list;

    while (query.next()) {
        MeasureTask task;
        task.id            = query.value(0).toInt();
        task.projectId     = query.value(1).toInt();
        task.taskName      = query.value(2).toString();
        task.measureDate   = query.value(3).toString();
        task.personnel     = query.value(4).toString();
        task.pipeStart     = query.value(5).toString();
        task.pipeEnd       = query.value(6).toString();
        task.pipeMaterial  = query.value(7).toString();
        task.pipeDiameter  = query.value(8).toString();
        task.pipeLength    = query.value(9).toString();
        task.status        = query.value(10).toString();
        task.progress      = query.value(11).toInt();
        task.notes         = query.value(12).toString();
        task.createdAt     = query.value(13).toString();
        task.updatedAt     = query.value(14).toString();
        list.append(task);
    }
    return list;
}

// ========================================================================
// 基础方法（保持不变）
// ========================================================================
void AuthManager::ensureDefaultAdmin()
{
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM users WHERE role = 0");
    if (query.exec() && query.next()) {
        if (query.value(0).toInt() > 0) {
            return;
        }
    }

    QString salt = generateSalt();
    QString hash = hashPassword("admin123", salt);
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QSqlQuery insertQuery(m_db);
    insertQuery.prepare(
        "INSERT INTO users (username, password_hash, salt, role, created_at) "
        "VALUES (?, ?, ?, 0, ?)"
    );
    insertQuery.addBindValue("admin");
    insertQuery.addBindValue(hash);
    insertQuery.addBindValue(salt);
    insertQuery.addBindValue(now);

    if (insertQuery.exec()) {
        qDebug() << "[AuthManager] 默认管理员账户已创建: admin / admin123";
    } else {
        qDebug() << "[AuthManager] 创建默认管理员失败:" << insertQuery.lastError().text();
    }
}

QString AuthManager::hashPassword(const QString &password, const QString &salt)
{
    QByteArray data = (salt + password).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return QString(hash.toHex());
}

QString AuthManager::generateSalt()
{
    QByteArray salt;
    for (int i = 0; i < 16; ++i) {
        salt.append(static_cast<char>(QRandomGenerator::global()->generate()));
    }
    return QString(salt.toHex());
}

bool AuthManager::login(const QString &username, const QString &password)
{
    if (!m_db.isOpen()) {
        emit loginFailed("数据库未初始化");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT id, password_hash, salt, role, client_name FROM users WHERE username = ?");
    query.addBindValue(username);

    if (!query.exec()) {
        emit loginFailed("数据库查询失败");
        return false;
    }

    if (!query.next()) {
        emit loginFailed("用户不存在");
        return false;
    }

    QString storedHash = query.value(1).toString();
    QString salt       = query.value(2).toString();
    int     roleCode   = query.value(3).toInt();
    m_currentClientName = query.value(4).toString();

    QString inputHash = hashPassword(password, salt);
    if (inputHash != storedHash) {
        emit loginFailed("密码错误");
        return false;
    }

    m_currentUser = username;
    m_isLoggedIn  = true;
    switch (roleCode) {
    case 0:  m_currentRole = UserRole::Admin;       break;
    case 1:  m_currentRole = UserRole::FieldWorker;  break;
    case 2:  m_currentRole = UserRole::OfficeWorker; break;
    case 3:  m_currentRole = UserRole::Customer;     break;
    default: m_currentRole = UserRole::FieldWorker;   break;
    }

    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QSqlQuery updateQuery(m_db);
    updateQuery.prepare("UPDATE users SET last_login = ? WHERE username = ?");
    updateQuery.addBindValue(now);
    updateQuery.addBindValue(username);
    updateQuery.exec();

    QString roleStr = roleString();
    qDebug() << "[AuthManager] 登录成功:" << username << "角色:" << roleStr;
    emit loginSuccess(username, roleStr);
    return true;
}

void AuthManager::logout()
{
    m_isLoggedIn  = false;
    m_currentUser.clear();
    m_currentRole = UserRole::FieldWorker;
    m_currentClientName.clear();
    qDebug() << "[AuthManager] 已登出";
    emit loggedOut();
}

QString AuthManager::roleString() const
{
    switch (m_currentRole) {
    case UserRole::Admin:       return QString::fromUtf8("超级管理员");
    case UserRole::FieldWorker:  return QString::fromUtf8("外业人员");
    case UserRole::OfficeWorker: return QString::fromUtf8("内业人员");
    case UserRole::Customer:    return QString::fromUtf8("管理员");
    default:                    return QString::fromUtf8("未知");
    }
}

bool AuthManager::canUpload() const
{
    // 超级管理员无上传权限，仅限浏览和下载
    return m_isLoggedIn && !isSuperAdmin();
}

bool AuthManager::canDownload() const
{
    return isCompanyAdmin() || isOfficeWorker();
}

bool AuthManager::canDelete() const
{
    // 超级管理员无删除权限，仅限浏览和下载
    return isCompanyAdmin() && !isSuperAdmin();
}

bool AuthManager::canManageUsers() const
{
    return isCompanyAdmin();  // 超级管理员和公司管理员均可管理用户
}

bool AuthManager::canManageProjects() const
{
    return isCompanyAdmin() || isOfficeWorker();
}

bool AuthManager::canViewAllFiles() const
{
    return isCompanyAdmin() || isOfficeWorker();
}

bool AuthManager::canOnlyViewSelf() const
{
    return isFieldWorker();
}

QString AuthManager::currentUserClient() const
{
    return m_currentClientName;
}

bool AuthManager::isSuperAdmin() const
{
    // 只有 admin 账户是超级管理员
    return isLoggedIn() && (m_currentRole == UserRole::Admin) && (m_currentUser == "admin");
}

bool AuthManager::addUser(const QString &username, const QString &password, UserRole role, const QString &clientName)
{
    if (!isCompanyAdmin()) return false;
    if (!m_db.isOpen()) return false;

    QString salt = generateSalt();
    QString hash = hashPassword(password, salt);
    QString now  = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    int roleCode = static_cast<int>(role);

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO users (username, password_hash, salt, role, client_name, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(username);
    query.addBindValue(hash);
    query.addBindValue(salt);
    query.addBindValue(roleCode);
    query.addBindValue(clientName);
    query.addBindValue(now);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 添加用户失败:" << query.lastError().text();
        return false;
    }
    qDebug() << "[AuthManager] 添加用户成功:" << username << "客户:" << clientName;
    syncDbToServer();  // 写操作后同步到服务器
    return true;
}

bool AuthManager::deleteUser(int userId)
{
    if (!isCompanyAdmin()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM users WHERE id = ? AND role != 0");
    query.addBindValue(userId);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 删除用户失败:" << query.lastError().text();
        return false;
    }
    syncDbToServer();  // 写操作后同步到服务器
    return true;
}

bool AuthManager::updateUserRole(int userId, UserRole newRole)
{
    if (!isCompanyAdmin()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET role = ? WHERE id = ? AND role != 0");
    query.addBindValue(static_cast<int>(newRole));
    query.addBindValue(userId);

    bool ok = query.exec();
    if (ok) syncDbToServer();
    return ok;
}

bool AuthManager::updateUserClientName(int userId, const QString &clientName)
{
    if (!isCompanyAdmin()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET client_name = ? WHERE id = ?");
    query.addBindValue(clientName);
    query.addBindValue(userId);

    bool ok = query.exec();
    if (ok) syncDbToServer();
    return ok;
}

bool AuthManager::updatePassword(int userId, const QString &newPassword)
{
    if (!isAdmin()) {
        QSqlQuery checkQuery(m_db);
        checkQuery.prepare("SELECT username FROM users WHERE id = ?");
        checkQuery.addBindValue(userId);
        if (!checkQuery.exec() || !checkQuery.next()) {
            return false;
        }
        if (checkQuery.value(0).toString() != m_currentUser) {
            return false;
        }
    }

    QString salt = generateSalt();
    QString hash = hashPassword(newPassword, salt);

    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET password_hash = ?, salt = ? WHERE id = ?");
    query.addBindValue(hash);
    query.addBindValue(salt);
    query.addBindValue(userId);

    bool ok = query.exec();
    if (ok) syncDbToServer();
    return ok;
}

QList<UserInfo> AuthManager::getAllUsers()
{
    QList<UserInfo> list;
    if (!m_db.isOpen()) return list;

    QSqlQuery query(m_db);
    if (isSuperAdmin()) {
        // 超级管理员（admin）查看所有用户
        query.prepare("SELECT id, username, role, client_name, created_at, last_login FROM users ORDER BY id");
    } else {
        // 其他角色（公司管理员）：只查看自己客户名下的用户，同时也能看到超级管理员
        QString clientName = m_currentClientName;
        if (clientName.isEmpty()) {
            clientName = m_currentUser;
        }
        query.prepare("SELECT id, username, role, client_name, created_at, last_login FROM users "
                      "WHERE client_name=? ORDER BY id");
        query.addBindValue(clientName);
    }
    if (!query.exec()) return list;

    while (query.next()) {
        UserInfo info;
        info.id          = query.value(0).toInt();
        info.username    = query.value(1).toString();
        int roleCode     = query.value(2).toInt();
        info.clientName  = query.value(3).toString();
        switch (roleCode) {
        case 0:  info.role = UserRole::Admin;       break;
        case 1:  info.role = UserRole::FieldWorker;  break;
        case 2:  info.role = UserRole::OfficeWorker; break;
        case 3:  info.role = UserRole::Customer;     break;
        default: info.role = UserRole::FieldWorker;   break;
        }
        info.createdAt = query.value(4).toString();
        info.lastLogin = query.value(5).toString();
        list.append(info);
    }
    return list;
}

QString AuthManager::getUserSalt(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT salt FROM users WHERE username = ?");
    query.addBindValue(username);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return "";
}

bool AuthManager::recordUpload(const QString &username, const QString &remotePath,
                               const QString &fileName, const QString &uploadedAt)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO file_uploads (username, remote_path, file_name, uploaded_at) "
        "VALUES (?, ?, ?, ?)"
    );
    query.addBindValue(username);
    query.addBindValue(remotePath);
    query.addBindValue(fileName);
    query.addBindValue(uploadedAt);

    if (!query.exec()) {
        qDebug() << "[AuthManager] 记录上传失败:" << query.lastError().text();
        return false;
    }
    qDebug() << "[AuthManager] 记录上传:" << username << remotePath << fileName;
    return true;
}

QList<AuthManager::UploadRecord> AuthManager::getUploadRecords(const QString &remotePath)
{
    QList<UploadRecord> records;
    if (!m_db.isOpen()) return records;

    QSqlQuery query(m_db);
    if (remotePath.isEmpty()) {
        query.prepare("SELECT id, username, remote_path, file_name, uploaded_at "
                      "FROM file_uploads ORDER BY uploaded_at DESC");
    } else {
        query.prepare("SELECT id, username, remote_path, file_name, uploaded_at "
                      "FROM file_uploads WHERE remote_path LIKE ? ORDER BY uploaded_at DESC");
        query.addBindValue(remotePath + "%");
    }

    if (!query.exec()) return records;

    while (query.next()) {
        UploadRecord rec;
        rec.id         = query.value(0).toInt();
        rec.username   = query.value(1).toString();
        rec.remotePath = query.value(2).toString();
        rec.fileName   = query.value(3).toString();
        rec.uploadedAt = query.value(4).toString();
        records.append(rec);
    }
    return records;
}

// ========================================================================
// 远程数据库同步
// ========================================================================

QString AuthManager::syncDbFromServer()
{
    SshManager *ssh = SshManager::instance();
    if (!ssh->isConnected()) {
        qDebug() << "[AuthManager] syncDbFromServer: SSH未连接，跳过下载";
        return "";
    }

    // 本地临时路径：AppData/CloudPlatform/cloudplatform_users.db
    QString localDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (localDir.isEmpty()) {
        localDir = QCoreApplication::applicationDirPath();
    }
    QDir().mkpath(localDir);
    QString localPath = localDir + "/cloudplatform_users.db";

    QString remotePath = remoteDbPath(); // /home/ubuntu/cloudplatform_users.db

    qDebug() << "[AuthManager] 正在从服务器下载 db:" << remotePath << "->" << localPath;

    // 先检查服务器是否存在该文件
    QString checkResult = SshManager::executeOnce(
        ssh->host(), ssh->user(), ssh->password(), ssh->port(), ssh->plinkPath(),
        QString("test -f %1 && echo EXISTS || echo MISSING").arg(remotePath), 10000
    );
    bool remoteExists = checkResult.trimmed().contains("EXISTS");

    if (!remoteExists) {
        qDebug() << "[AuthManager] 服务器上无 db 文件，将使用本地新建（首次运行）";
        m_localDbPath = localPath;
        return localPath;  // 返回路径，initDatabase 会新建空库
    }

    // 使用 pscp 下载
    QString pscpPath = ssh->findPscp();
    if (pscpPath.isEmpty()) {
        qDebug() << "[AuthManager] syncDbFromServer: 未找到 pscp，无法下载 db";
        m_localDbPath = localPath;
        return localPath;
    }

    // 若已存在旧的本地文件先备份
    if (QFile::exists(localPath)) {
        QFile::remove(localPath + ".bak");
        QFile::copy(localPath, localPath + ".bak");
    }

    QProcess pscpProc;
    QStringList args;
    args << "-pw" << ssh->password()
         << "-batch"
         << QString("%1@%2:%3").arg(ssh->user(), ssh->host(), remotePath)
         << localPath;
    pscpProc.start(pscpPath, args);

    if (!pscpProc.waitForFinished(30000)) {
        pscpProc.kill();
        qDebug() << "[AuthManager] syncDbFromServer: pscp 超时，使用本地文件";
        // 若下载失败但有备份，恢复备份
        if (QFile::exists(localPath + ".bak") && !QFile::exists(localPath)) {
            QFile::copy(localPath + ".bak", localPath);
        }
        m_localDbPath = localPath;
        return localPath;
    }

    if (pscpProc.exitCode() != 0) {
        qDebug() << "[AuthManager] syncDbFromServer: pscp 下载失败:"
                 << pscpProc.readAllStandardError();
        if (QFile::exists(localPath + ".bak") && !QFile::exists(localPath)) {
            QFile::copy(localPath + ".bak", localPath);
        }
        m_localDbPath = localPath;
        return localPath;
    }

    qDebug() << "[AuthManager] db 下载成功:" << localPath;
    m_localDbPath = localPath;
    return localPath;
}

void AuthManager::syncDbToServer()
{
    if (m_localDbPath.isEmpty() || !QFile::exists(m_localDbPath)) {
        qDebug() << "[AuthManager] syncDbToServer: 本地 db 文件不存在，跳过";
        return;
    }

    SshManager *ssh = SshManager::instance();
    if (!ssh->isConnected()) {
        qDebug() << "[AuthManager] syncDbToServer: SSH未连接，跳过上传";
        return;
    }

    QString pscpPath = ssh->findPscp();
    if (pscpPath.isEmpty()) {
        qDebug() << "[AuthManager] syncDbToServer: 未找到 pscp，无法上传 db";
        emit dbSyncToServerFinished(false);
        return;
    }

    // 在后台线程中上传，不阻塞 UI
    QString localPath  = m_localDbPath;
    QString remotePath = remoteDbPath();
    QString host       = ssh->host();
    QString user       = ssh->user();
    QString password   = ssh->password();

    QThread *thread = new QThread();
    QObject *worker = new QObject();
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, [=]() {
        QProcess pscpProc;
        QStringList args;
        args << "-pw" << password
             << "-batch"
             << localPath
             << QString("%1@%2:%3").arg(user, host, remotePath);
        pscpProc.start(pscpPath, args);

        bool ok = false;
        if (pscpProc.waitForFinished(30000)) {
            ok = (pscpProc.exitCode() == 0);
            if (ok) {
                qDebug() << "[AuthManager] db 已同步到服务器:" << remotePath;
            } else {
                qDebug() << "[AuthManager] syncDbToServer: pscp 失败:"
                         << pscpProc.readAllStandardError();
            }
        } else {
            pscpProc.kill();
            qDebug() << "[AuthManager] syncDbToServer: pscp 超时";
        }

        emit AuthManager::instance()->dbSyncToServerFinished(ok);
        thread->quit();
    });

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}
