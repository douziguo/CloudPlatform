#include "sshmanager.h"
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextCodec>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <memory>

// 去除 ANSI 颜色转义码（防止 ls --color 混入文件名）
// 状态机遍历，直接识别 ESC 字符 (0x1B) 并跳过整个转义序列
QString SshManager::stripAnsiCodes(const QString &text)
{
    QString result;
    result.reserve(text.length());
    bool inEscape = false;
    for (int i = 0; i < text.length(); ++i) {
        if (text[i].unicode() == 0x1B) {
            inEscape = true;
            continue;
        }
        if (inEscape) {
            // ANSI 转义序列格式: ESC [ ... 字母
            // 跳过中间的所有参数字符（数字、分号、冒号、问号、空格等）
            if (text[i].isLetter()) {
                inEscape = false;
            }
            continue;
        }
        result.append(text[i]);
    }
    return result;
}

SshManager *SshManager::m_instance = nullptr;

SshManager* SshManager::instance()
{
    if (!m_instance) {
        m_instance = new SshManager();
    }
    return m_instance;
}

SshManager::SshManager(QObject *parent)
    : QObject(parent)
    , m_sshProcess(nullptr)
    , m_isConnected(false)
    , m_useKeyAuth(false)
    , m_port(22)
{
}

SshManager::~SshManager()
{
    disconnectFromServer();
}

void SshManager::setConnectionParams(const QString &host,
                                     const QString &user,
                                     const QString &password,
                                     int port)
{
    m_host = host;
    m_user = user;
    m_password = password;
    m_port = port;
    qDebug() << "[SshManager] 连接参数已设置 - Host:" << host << "User:" << user << "Port:" << port;
}

bool SshManager::connectToServer()
{
    if (m_host.isEmpty() || m_user.isEmpty()) {
        emit connectionError("服务器地址或用户名未设置");
        return false;
    }

    qDebug() << "[SshManager] 正在连接服务器:" << m_user << "@" << m_host;

    if (autoConnect()) {
        return true;
    }

    qDebug() << "[SshManager] 所有自动连接方式均失败";
    return false;
}

void SshManager::disconnectFromServer(bool emitSignal)
{
    if (m_sshProcess && m_sshProcess->state() != QProcess::NotRunning) {
        m_sshProcess->terminate();
        m_sshProcess->waitForFinished(3000);
        if (m_sshProcess->state() != QProcess::NotRunning) {
            m_sshProcess->kill();
        }
        delete m_sshProcess;
        m_sshProcess = nullptr;
    }

    // 清理所有上传/下载进程
    for (auto it = m_uploadProcesses.begin(); it != m_uploadProcesses.end(); ++it) {
        it.value()->kill();
        it.value()->deleteLater();
    }
    m_uploadProcesses.clear();
    for (auto it = m_downloadProcesses.begin(); it != m_downloadProcesses.end(); ++it) {
        it.value()->kill();
        it.value()->deleteLater();
    }
    m_downloadProcesses.clear();

    m_isConnected = false;
    if (emitSignal) {
        emit connectionLost();
    }
    qDebug() << "[SshManager] 已断开连接";
}

bool SshManager::isConnected() const
{
    return m_isConnected;
}

// ========================================================================
// 自动连接：优先 plink(-pw) → 交互式SSH → sshpass → SSH密钥
// ========================================================================
bool SshManager::autoConnect()
{
    qDebug() << "[SshManager] 尝试自动连接...";

    // 方法1 (最高优先级): plink.exe -pw
    QString plinkPath = findPlink();
    if (!plinkPath.isEmpty()) {
        qDebug() << "[SshManager] 方法1: 使用 plink:" << plinkPath;
        if (tryPlink(plinkPath)) {
            m_useKeyAuth = false;
            m_isConnected = true;
            m_plinkPath = plinkPath;
            emit connectionEstablished();
            return true;
        }
    }

    // 方法2: 交互式 SSH
    qDebug() << "[SshManager] 方法2: 交互式SSH自动输入密码...";
    if (tryInteractiveSsh()) {
        m_useKeyAuth = false;
        m_isConnected   = true;
        m_plinkPath.clear();
        emit connectionEstablished();
        qDebug() << "[SshManager] 提示：当前使用交互式SSH连接成功，"
                 << "但若需要上传/下载文件，建议将 plink.exe 和 pscp.exe "
                 << "放到程序目录或系统 PATH 中。";
        return true;
    }

    // 方法3: sshpass
    QString sshpassPath = QStandardPaths::findExecutable("sshpass");
    if (!sshpassPath.isEmpty()) {
        qDebug() << "[SshManager] 方法3: sshpass密码认证...";
        if (trySshpass()) {
            m_useKeyAuth = false;
            m_isConnected = true;
            m_plinkPath.clear();
            emit connectionEstablished();
            return true;
        }
    }

    // 方法4: SSH 密钥
    qDebug() << "[SshManager] 方法4: SSH密钥认证...";
    if (trySshKey()) {
        m_useKeyAuth = true;
        m_isConnected = true;
        m_plinkPath.clear();
        emit connectionEstablished();
        return true;
    }

    qDebug() << "[SshManager] 所有自动连接方式均失败";
    return false;
}

// ========================================================================
// 查找 plink.exe
// ========================================================================
QString SshManager::findPlink()
{
    // 1. 程序所在目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString localPlink = appDir + "/plink.exe";
    if (QFile::exists(localPlink)) {
        qDebug() << "[SshManager] 找到本地 plink:" << localPlink;
        return localPlink;
    }

    // 2. 当前工作目录
    QString cwd = QDir::currentPath();
    QString cwdPlink = cwd + "/plink.exe";
    if (QFile::exists(cwdPlink)) {
        qDebug() << "[SshManager] 找到当前目录 plink:" << cwdPlink;
        return cwdPlink;
    }

    // 3. PATH
    QString path = QStandardPaths::findExecutable("plink");
    if (!path.isEmpty()) {
        return path;
    }

    // 4. PuTTY 安装目录
    QStringList puttyDirs;
    puttyDirs << "C:/Program Files/PuTTY/plink.exe"
              << "C:/Program Files (x86)/PuTTY/plink.exe";
    for (const QString &p : puttyDirs) {
        if (QFile::exists(p)) {
            return p;
        }
    }

    return QString();
}

// ========================================================================
// 查找 pscp.exe（查程序目录 → 当前工作目录 → PATH → plink同目录）
// ========================================================================
QString SshManager::findPscp()
{
    // 1. 程序所在目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString appPscp = appDir + "/pscp.exe";
    if (QFile::exists(appPscp)) {
        qDebug() << "[SshManager] 找到本地 pscp:" << appPscp;
        return appPscp;
    }

    // 2. 当前工作目录
    QString cwd = QDir::currentPath();
    QString cwdPscp = cwd + "/pscp.exe";
    if (QFile::exists(cwdPscp)) {
        qDebug() << "[SshManager] 找到当前目录 pscp:" << cwdPscp;
        return cwdPscp;
    }

    // 3. PATH
    QString path = QStandardPaths::findExecutable("pscp");
    if (!path.isEmpty()) {
        return path;
    }

    // 4. 与 plink 同目录
    QString plinkDir = QFileInfo(findPlink()).dir().path();
    if (!plinkDir.isEmpty()) {
        QString puttyPscp = plinkDir + "/pscp.exe";
        if (QFile::exists(puttyPscp)) {
            return puttyPscp;
        }
    }

    return QString();
}

// ========================================================================
// 使用 plink.exe -pw 连接
// ========================================================================
bool SshManager::tryPlink(const QString &plinkPath)
{
    QProcess proc;
    proc.setProgram(plinkPath);
    QStringList args;
    // 不加 -batch，让 plink 可以提示主机密钥，我们在 readyRead 中自动回复 y
    args << "-pw" << m_password
         << "-P" << QString::number(m_port)
         << QString("%1@%2").arg(m_user, m_host)
         << "echo SSH_OK";
    proc.setArguments(args);

    // 自动确认主机密钥（plink 首次连接会输出到 stderr）
    connect(&proc, &QProcess::readyReadStandardError,
            &proc, [&proc]() {
        QByteArray err = proc.readAllStandardError();
        QString errStr = QString::fromLocal8Bit(err);
        if (errStr.contains("Store key in cache?") || errStr.contains("y/n")) {
            proc.write("y\n");
            qDebug() << "[SshManager] plink 自动确认主机密钥";
        }
    });

    proc.start();
    proc.waitForFinished(10000);

    QByteArray output = proc.readAllStandardOutput();
    QByteArray err = proc.readAllStandardError();
    int exitCode = proc.exitCode();

    qDebug() << "[SshManager] plink 输出:" << output << "错误:" << err << "退出码:" << exitCode;

    if (output.contains("SSH_OK") || exitCode == 0) {
        qDebug() << "[SshManager] plink 连接成功，主机密钥已缓存";
        return true;
    }

    qDebug() << "[SshManager] plink 失败:" << err;
    return false;
}

// ========================================================================
// 交互式SSH：用 QProcess 启动 ssh，自动向 stdin 输入密码
// ========================================================================
bool SshManager::tryInteractiveSsh()
{
    qDebug() << "[SshManager] 尝试交互式SSH连接（自动输入密码）...";

    QString sshPath = QStandardPaths::findExecutable("ssh");
    if (sshPath.isEmpty()) {
        QStringList commonPaths;
        commonPaths << "C:/Windows/System32/OpenSSH/ssh.exe"
                    << "C:/Windows/SysWOW64/OpenSSH/ssh.exe";
        for (const QString &p : commonPaths) {
            if (QFile::exists(p)) {
                sshPath = p;
                break;
            }
        }
    }
    if (sshPath.isEmpty()) {
        qDebug() << "[SshManager] 未找到 ssh.exe";
        return false;
    }

    qDebug() << "[SshManager] 使用 ssh:" << sshPath;

    QProcess proc;
    proc.setProgram(sshPath);
    QStringList args;
    args << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "ConnectTimeout=5"
         << "-o" << "PasswordAuthentication=yes"
         << "-o" << "KbdInteractiveAuthentication=no"
         << "-o" << "PubkeyAuthentication=no"
         << QString("%1@%2").arg(m_user, m_host)
         << "echo SSH_OK";
    proc.setArguments(args);
    proc.start();

    if (!proc.waitForStarted(5000)) {
        qDebug() << "[SshManager] ssh 进程未能启动";
        return false;
    }

    qDebug() << "[SshManager] ssh 进程已启动，等待密码提示...";

    // 等待 stderr 中的 "password:" 提示
    QByteArray stderrBuffer;
    for (int i = 0; i < 40; i++) {
        proc.waitForReadyRead(200);

        QByteArray err = proc.readAllStandardError();
        if (!err.isEmpty()) {
            stderrBuffer.append(err);
            qDebug() << "[SshManager] stderr:" << QString::fromLocal8Bit(err);
            if (stderrBuffer.toLower().contains("password:")) {
                qDebug() << "[SshManager] 收到密码提示，正在输入密码...";
                QByteArray passwordBytes = m_password.toUtf8() + "\n";
                proc.write(passwordBytes);
                proc.waitForBytesWritten(2000);
                break;
            }
        }

        QByteArray out = proc.readAllStandardOutput();
        if (!out.isEmpty() && out.contains("SSH_OK")) {
            qDebug() << "[SshManager] SSH已连接（无需密码）";
            proc.waitForFinished(1000);
            return true;
        }

        if (proc.state() == QProcess::NotRunning) {
            break;
        }
    }

    // 等待进程结束
    proc.waitForFinished(10000);

    QByteArray output = proc.readAllStandardOutput();
    QByteArray err = proc.readAllStandardError();

    if (output.contains("SSH_OK") || proc.exitCode() == 0) {
        qDebug() << "[SshManager] 交互式SSH连接成功！";
        return true;
    }

    qDebug() << "[SshManager] 交互式SSH失败:" << QString::fromLocal8Bit(err);
    return false;
}

// ========================================================================
// 使用 sshpass 连接
// ========================================================================
bool SshManager::trySshpass()
{
    QProcess proc;
    proc.setProgram("sshpass");
    QStringList args;
    args << "-p" << m_password
         << "ssh"
         << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "ConnectTimeout=5"
         << "-p" << QString::number(m_port)
         << QString("%1@%2").arg(m_user, m_host)
         << "echo SSH_OK";
    proc.setArguments(args);
    proc.start();
    proc.waitForFinished(10000);

    QByteArray output = proc.readAllStandardOutput();
    if (output.contains("SSH_OK") || proc.exitCode() == 0) {
        qDebug() << "[SshManager] sshpass 连接成功";
        return true;
    }
    qDebug() << "[SshManager] sshpass 失败:" << proc.readAllStandardError();
    return false;
}

// ========================================================================
// 使用 SSH 密钥连接
// ========================================================================
bool SshManager::trySshKey()
{
    QString sshPath = QStandardPaths::findExecutable("ssh");
    if (sshPath.isEmpty()) {
        QStringList commonPaths;
        commonPaths << "C:/Windows/System32/OpenSSH/ssh.exe"
                    << "C:/Windows/SysWOW64/OpenSSH/ssh.exe";
        for (const QString &p : commonPaths) {
            if (QFile::exists(p)) { sshPath = p; break; }
        }
    }
    if (sshPath.isEmpty()) {
        qDebug() << "[SshManager] trySshKey: 未找到 ssh.exe，跳过";
        return false;
    }

    QProcess proc;
    proc.setProgram(sshPath);
    QStringList args;
    args << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "ConnectTimeout=5"
         << "-o" << "BatchMode=yes"
         << "-p" << QString::number(m_port)
         << QString("%1@%2").arg(m_user, m_host)
         << "echo SSH_OK";
    proc.setArguments(args);
    proc.start();

    if (!proc.waitForStarted(5000)) {
        qDebug() << "[SshManager] trySshKey: ssh 进程未能启动";
        return false;
    }

    proc.waitForFinished(10000);

    int exitCode = proc.exitCode();
    QProcess::ExitStatus exitStatus = proc.exitStatus();
    QByteArray output = proc.readAllStandardOutput();
    QByteArray err = proc.readAllStandardError();

    qDebug() << "[SshManager] trySshKey: exitCode=" << exitCode
             << "exitStatus=" << exitStatus
             << "output=" << output.trimmed()
             << "err=" << err.trimmed();

    if (exitStatus == QProcess::NormalExit && exitCode == 0 && output.contains("SSH_OK")) {
        qDebug() << "[SshManager] SSH密钥连接成功";
        return true;
    }
    qDebug() << "[SshManager] SSH密钥连接失败";
    return false;
}

// ========================================================================
// 执行 SSH 命令（异步）
// ========================================================================
bool SshManager::executeCommandAsync(const QString &command)
{
    if (!m_isConnected) {
        emit connectionError("SSH未连接");
        return false;
    }

    if (m_sshProcess && m_sshProcess->state() != QProcess::NotRunning) {
        emit connectionError("上一条命令还在执行中");
        return false;
    }

    if (!m_sshProcess) {
        m_sshProcess = new QProcess(this);
        connect(m_sshProcess, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            emit commandFinished(exitCode);
        });
        connect(m_sshProcess, &QProcess::readyReadStandardOutput,
                this, [this]() {
            QByteArray data = m_sshProcess->readAllStandardOutput();
            emit commandOutput(QString::fromUtf8(data));
        });
        connect(m_sshProcess, &QProcess::readyReadStandardError,
                this, [this]() {
            QByteArray data = m_sshProcess->readAllStandardError();
            emit commandOutput(QString::fromUtf8(data));
        });
    }

    QStringList args;
    if (!m_plinkPath.isEmpty()) {
        m_sshProcess->setProgram(m_plinkPath);
        args << "-batch" << "-pw" << m_password
             << "-P" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << command;
    } else if (!m_useKeyAuth) {
        m_sshProcess->setProgram("sshpass");
        args << "-p" << m_password
             << "ssh"
             << "-o" << "StrictHostKeyChecking=no"
             << "-p" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << command;
    } else {
        m_sshProcess->setProgram("ssh");
        args << "-o" << "StrictHostKeyChecking=no"
             << "-p" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << command;
    }

    m_sshProcess->setArguments(args);
    m_sshProcess->start();
    return true;
}

// ========================================================================
// 执行 SSH 命令（同步，等待结果）
// ========================================================================
QString SshManager::executeCommand(const QString &command, bool waitForFinish)
{
    if (!m_isConnected) {
        return "";
    }

    QProcess proc;
    QStringList args;

    if (!m_plinkPath.isEmpty()) {
        proc.setProgram(m_plinkPath);
        args << "-batch" << "-pw" << m_password
             << "-P" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << command;
    } else if (!m_useKeyAuth) {
        proc.setProgram("sshpass");
        args << "-p" << m_password
             << "ssh"
             << "-o" << "StrictHostKeyChecking=no"
             << "-p" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << command;
    } else {
        proc.setProgram("ssh");
        args << "-o" << "StrictHostKeyChecking=no"
             << "-p" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << command;
    }

    // 设置 TERM=dumb 防止远程命令输出 ANSI 颜色码
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "dumb");
    proc.setProcessEnvironment(env);

    proc.setArguments(args);
    proc.start();
    proc.waitForFinished(waitForFinish ? 30000 : 5000);

#ifdef Q_OS_WIN
    // Windows 上 plink 把远程 UTF-8 输出转成了本地编码 (GBK)
    return QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
#else
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
#endif
}

// ========================================================================
// 远程文件/目录删除
// ========================================================================
bool SshManager::deleteRemoteFile(const QString &remotePath)
{
    if (!m_isConnected) {
        qWarning() << "[SshManager] deleteRemoteFile: SSH未连接";
        return false;
    }

    QString cmd = QString("rm -f \"%1\"").arg(remotePath);
    qDebug() << "[SshManager] 删除远程文件:" << remotePath;
    QString result = executeCommand(cmd, true);

    // 验证文件是否已删除
    QString checkCmd = QString("test -f \"%1\" && echo EXISTS || echo GONE").arg(remotePath);
    QString checkResult = executeCommand(checkCmd, true);
    bool deleted = checkResult.contains("GONE");

    if (!deleted) {
        qWarning() << "[SshManager] 删除文件失败:" << remotePath << "result:" << result;
    }
    return deleted;
}

bool SshManager::deleteRemoteDir(const QString &remotePath)
{
    if (!m_isConnected) {
        qWarning() << "[SshManager] deleteRemoteDir: SSH未连接";
        return false;
    }

    // 安全检查：防止误删根目录
    if (remotePath == "/" || remotePath == "/home" || remotePath == "/home/ubuntu" ||
        remotePath == "/home/ubuntu/" || remotePath.isEmpty()) {
        qWarning() << "[SshManager] deleteRemoteDir: 拒绝删除受保护路径:" << remotePath;
        return false;
    }

    QString cmd = QString("rm -rf \"%1\"").arg(remotePath);
    qDebug() << "[SshManager] 删除远程目录:" << remotePath;
    QString result = executeCommand(cmd, true);

    // 验证目录是否已删除
    QString checkCmd = QString("test -d \"%1\" && echo EXISTS || echo GONE").arg(remotePath);
    QString checkResult = executeCommand(checkCmd, true);
    bool deleted = checkResult.contains("GONE");

    if (!deleted) {
        qWarning() << "[SshManager] 删除目录失败:" << remotePath << "result:" << result;
    }
    return deleted;
}

// ========================================================================
// 下载远程目录（递归，使用 pscp -r）
// ========================================================================
bool SshManager::downloadDir(const QString &remoteDir, const QString &localDir)
{
    QString pscpPath = findPscp();
    if (pscpPath.isEmpty()) {
        qWarning() << "[SshManager] downloadDir: 未找到 pscp.exe";
        emit downloadFinished(remoteDir, false, QString::fromUtf8("未找到 pscp.exe"));
        return false;
    }

    // 创建本地目录
    QDir().mkpath(localDir);

    QProcess *proc = new QProcess(this);
    proc->setProgram(pscpPath);

    QStringList args;
    args << "-r";       // 递归
    args << "-batch";   // 禁用交互式提示
    args << "-pw" << m_password;
    args << QString("%1@%2:\"%3\"").arg(m_user).arg(m_host).arg(remoteDir);
    args << localDir;

    proc->setArguments(args);

    // 设置 UTF-8 环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LANG", "zh_CN.UTF-8");
    proc->setProcessEnvironment(env);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, remoteDir](int exitCode, QProcess::ExitStatus exitStatus) {
        bool success = (exitCode == 0 && exitStatus == QProcess::NormalExit);
        QString msg = success ? QString::fromUtf8("下载成功")
                              : QString::fromUtf8("下载失败 (exitCode: %1)").arg(exitCode);
        emit downloadFinished(remoteDir, success, msg);
        proc->deleteLater();
    });

    qDebug() << "[SshManager] downloadDir() pscp -r:" << remoteDir << "->" << localDir;

    proc->start();
    return true;
}

// ========================================================================
// 上传文件（异步，结果通过 uploadFinished 信号通知）
// 只用 pscp.exe，找不到就直接报错
// ========================================================================
void SshManager::uploadFile(const QString &localPath, const QString &remotePath)
{
    if (!m_isConnected) {
        emit uploadFinished(localPath, false, "SSH未连接");
        return;
    }

    // 如果已有同名本地文件正在上传，先取消之前的
    if (m_uploadProcesses.contains(localPath)) {
        QProcess *oldProc = m_uploadProcesses.take(localPath);
        oldProc->kill();
        oldProc->deleteLater();
    }

    // 先创建远程目录
    QString remoteDir = QFileInfo(remotePath).path();
    if (!remoteDir.isEmpty() && remoteDir != ".") {
        QString mkdirCmd = QString("mkdir -p \"%1\"").arg(remoteDir);
        qDebug() << "[SshManager] 创建远程目录:" << remoteDir;
        executeCommand(mkdirCmd, true);
    }

    qDebug() << "[SshManager] uploadFile() 开始 - 本地:" << localPath << "远程:" << remotePath;

    // 统一用 findPscp() 查找 pscp.exe
    QString pscpPath = findPscp();

    if (!pscpPath.isEmpty()) {
        qDebug() << "[SshManager] 使用 pscp 上传:" << pscpPath;

        qint64 fileSize = QFileInfo(localPath).size();
        emit uploadProgress(localPath, 0, fileSize);

        QProcess *proc = new QProcess(this);
        proc->setProgram(pscpPath);

        // 设置 UTF-8 环境变量，让 pscp 正确解析中文参数
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LANG", "zh_CN.UTF-8");
        env.insert("LC_ALL", "zh_CN.UTF-8");
        proc->setProcessEnvironment(env);

        QStringList args;
        args << "-pw" << m_password
             << "-P" << QString::number(m_port)
             << localPath
             << QString("%1@%2:%3").arg(m_user, m_host, remotePath);
        proc->setArguments(args);

        // 自动确认主机密钥（PuTTY 首次连接会提示）
        connect(proc, &QProcess::readyReadStandardError,
                this, [proc]() {
            QByteArray err = proc->readAllStandardError();
            QString errStr = QString::fromLocal8Bit(err);
            if (errStr.contains("Store key in cache?") || errStr.contains("y/n")) {
                proc->write("y\n");
                qDebug() << "[SshManager] 自动确认主机密钥";
            }
        });

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, localPath, proc, fileSize](int exitCode, QProcess::ExitStatus) {
            if (!m_uploadProcesses.contains(localPath)) return;
            bool success = (exitCode == 0);
            QString errMsg = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
            if (!success && errMsg.isEmpty()) errMsg = QString("进程退出码: %1").arg(exitCode);
            emit uploadProgress(localPath, fileSize, fileSize);
            emit uploadFinished(localPath, success, errMsg);
            m_uploadProcesses.remove(localPath);
            proc->deleteLater();
        });

        connect(proc, &QProcess::errorOccurred,
                this, [this, localPath, proc](QProcess::ProcessError) {
            if (!m_uploadProcesses.contains(localPath)) return;
            QString errMsg = "上传进程错误: " + proc->errorString();
            emit uploadFinished(localPath, false, errMsg);
            m_uploadProcesses.remove(localPath);
            proc->deleteLater();
        });

        m_uploadProcesses[localPath] = proc;
        proc->start();
        if (!proc->waitForStarted(5000)) {
            qDebug() << "[SshManager] pscp 上传进程未能启动:" << proc->errorString();
        }
        return;
    }

    // 没有 pscp.exe，明确报错
    emit uploadFinished(localPath, false,
        "未找到 pscp.exe，无法上传文件。\n"
        "请将 pscp.exe 和 plink.exe 放在程序同一目录下。\n"
        "下载地址：https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html");
}

// ========================================================================
// 检查 SSH 工具是否可用
// ========================================================================
bool SshManager::checkSshTools()
{
    if (!findPlink().isEmpty()) return true;
    if (!findPscp().isEmpty()) return true;
    if (!QStandardPaths::findExecutable("sshpass").isEmpty()) return true;
    if (!QStandardPaths::findExecutable("ssh").isEmpty()) return true;
    return false;
}

// ========================================================================
// 配置 SSH 密钥认证
// ========================================================================
bool SshManager::setupSshKeyAuth()
{
    qDebug() << "[SshManager] 配置SSH密钥认证...";

    QString homePath = QDir::homePath();
    QString privateKeyPath = homePath + "/.ssh/id_rsa";
    QString publicKeyPath  = privateKeyPath + ".pub";

    if (!QFile::exists(privateKeyPath)) {
        qDebug() << "[SshManager] SSH密钥不存在，正在生成...";
        QDir sshDir(homePath + "/.ssh");
        if (!sshDir.exists()) {
            sshDir.mkpath(".");
        }

        QProcess genProc;
        genProc.setProgram("ssh-keygen");
        QStringList args;
        args << "-t" << "rsa" << "-b" << "4096"
             << "-f" << privateKeyPath
             << "-N" << "";
        genProc.setArguments(args);
        genProc.start();
        genProc.waitForFinished(10000);

        if (!QFile::exists(privateKeyPath)) {
            qDebug() << "[SshManager] 生成密钥失败";
            return false;
        }
    }

    // 复制公钥到服务器
    qDebug() << "[SshManager] 复制公钥到服务器...";
    QProcess copyProc;
    copyProc.setProgram("sshpass");
    QStringList args;
    args << "-p" << m_password
         << "ssh-copy-id"
         << "-o" << "StrictHostKeyChecking=no"
         << "-i" << publicKeyPath
         << QString("%1@%2").arg(m_user, m_host);
    copyProc.setArguments(args);
    copyProc.start();
    copyProc.waitForFinished(15000);

    return copyProc.exitCode() == 0;
}

// ========================================================================
// 下载文件（异步，结果通过 downloadFinished 信号通知）
// 只用 pscp.exe，找不到就直接报错
// ========================================================================
void SshManager::downloadFile(const QString &remotePath, const QString &localPath)
{
    // 去除可能的 ANSI 颜色转义码（防止文件名被污染）
    QString cleanRemotePath = stripAnsiCodes(remotePath);

    if (!m_isConnected) {
        emit downloadFinished(cleanRemotePath, false, "SSH未连接");
        return;
    }

    // 如果已有同名远程文件正在下载，先取消之前的
    if (m_downloadProcesses.contains(cleanRemotePath)) {
        QProcess *oldProc = m_downloadProcesses.take(cleanRemotePath);
        oldProc->kill();
        oldProc->deleteLater();
    }

    qDebug() << "[SshManager] downloadFile() 开始 - 远程:" << cleanRemotePath << "本地:" << localPath;

    // 确保本地目录存在
    QFileInfo localInfo(localPath);
    QDir localDir = localInfo.dir();
    if (!localDir.exists()) {
        localDir.mkpath(".");
    }

    // 统一用 plink + cat 下载（pscp 在 Windows 上编码/参数/_stderr_竞争问题太多）
    if (!m_plinkPath.isEmpty()) {
        qDebug() << "[SshManager] 使用 plink+cat 下载:" << cleanRemotePath;
        downloadFileViaPlinkCat(cleanRemotePath, localPath);
        return;
    }

    // 没有 plink.exe，明确报错
    emit downloadFinished(cleanRemotePath, false,
        "未找到 plink.exe，无法下载文件。\n"
        "请将 plink.exe 放在程序同一目录下。\n"
        "下载地址：https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html");
}

// ========================================================================
// 通过 plink + cat 下载文件（解决中文文件名编码问题）
// plink 通过 SSH 协议直接传输命令字符串，不经过 Windows 命令行编码
// ========================================================================
void SshManager::downloadFileViaPlinkCat(const QString &remotePath, const QString &localPath)
{
    emit downloadProgress(remotePath, 0, 0);

    QProcess *proc = new QProcess(this);
    proc->setProgram(m_plinkPath);

    QStringList args;
    args << "-pw" << m_password
         << "-P" << QString::number(m_port)
         << QString("%1@%2").arg(m_user, m_host)
         << "cat '" + remotePath + "'";
    proc->setArguments(args);

    // 调试：打印路径中每个字符的 Unicode 值
    QString hexDump;
    for (const QChar &c : remotePath) {
        hexDump += QString("%1 ").arg(c.unicode(), 4, 16, QChar('0'));
    }
    qDebug() << "[SshManager] 下载路径 HEX  dump:" << hexDump;
    qDebug() << "[SshManager] 下载路径长度:" << remotePath.length();

    // 打开本地文件写入
    QFile *outFile = new QFile(localPath);
    if (!outFile->open(QIODevice::WriteOnly)) {
        emit downloadFinished(remotePath, false,
            "无法创建本地文件: " + localPath);
        outFile->deleteLater();
        proc->deleteLater();
        return;
    }

    // stderr 缓冲（避免 readyReadStandardError 和 finished 竞争读走数据）
    auto stderrBuffer = std::make_shared<QString>();

    // 读取 stdout 写入文件
    connect(proc, &QProcess::readyReadStandardOutput,
            this, [proc, outFile]() {
        outFile->write(proc->readAllStandardOutput());
    });

    // 自动确认主机密钥 + 缓冲 stderr
    connect(proc, &QProcess::readyReadStandardError,
            this, [proc, stderrBuffer]() {
        QByteArray err = proc->readAllStandardError();
        QString errStr = QString::fromLocal8Bit(err);
        if (errStr.contains("Store key in cache?") || errStr.contains("y/n")) {
            proc->write("y\n");
        }
        *stderrBuffer += errStr;
    });

    // 下载完成
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, remotePath, proc, outFile, stderrBuffer](int exitCode, QProcess::ExitStatus) {
        if (!m_downloadProcesses.contains(remotePath)) return;

        // 关键：先读走剩余 stdout，防止小文件数据丢失
        QByteArray remaining = proc->readAllStandardOutput();
        if (!remaining.isEmpty()) {
            outFile->write(remaining);
            outFile->flush();
        }

        outFile->close();
        bool success = (exitCode == 0);
        qint64 fileSize = outFile->size();
        outFile->deleteLater();

        QString errMsg = stderrBuffer->trimmed();
        if (!success && errMsg.isEmpty()) errMsg = QString("进程退出码: %1").arg(exitCode);

        emit downloadProgress(remotePath, 1, 1);
        emit downloadFinished(remotePath, success, errMsg);
        m_downloadProcesses.remove(remotePath);
        proc->deleteLater();

        qDebug() << "[SshManager] plink+cat 下载完成:" << remotePath
                 << "大小:" << fileSize << "成功:" << success;
    });

    connect(proc, &QProcess::errorOccurred,
            this, [this, remotePath, proc, outFile](QProcess::ProcessError) {
        if (!m_downloadProcesses.contains(remotePath)) return;
        // 先读走剩余数据，尽量保留已下载的内容
        QByteArray remaining = proc->readAllStandardOutput();
        if (!remaining.isEmpty()) {
            outFile->write(remaining);
            outFile->flush();
        }
        outFile->close();
        outFile->deleteLater();
        QString errMsg = "下载进程错误: " + proc->errorString();
        emit downloadFinished(remotePath, false, errMsg);
        m_downloadProcesses.remove(remotePath);
        proc->deleteLater();
    });

    m_downloadProcesses[remotePath] = proc;
    proc->start();
    if (!proc->waitForStarted(5000)) {
        qDebug() << "[SshManager] plink+cat 下载进程未能启动:" << proc->errorString();
    }
}

// ========================================================================
// 切换远程目录
// ========================================================================
void SshManager::changeRemoteDir(const QString &path)
{
    m_currentRemoteDir = path;
    listRemoteDir(path);
}

// ========================================================================
// 列出远程目录内容（通过 ls 命令解析）
// ========================================================================
void SshManager::listRemoteDir(const QString &remotePath)
{
    if (!m_isConnected) {
        emit remoteDirError("SSH未连接");
        return;
    }

    // --color=never 防止 ANSI 颜色码混入文件名导致解析错误
    QString command = QString("ls -l --color=never --time-style=long-iso \"%1\" 2>/dev/null || ls -l --color=never \"%1\" 2>/dev/null").arg(remotePath);
    QString output = executeCommand(command, true);
    output = stripAnsiCodes(output);  // 整体去除 ANSI 码后再解析

    if (output.isEmpty()) {
        QString testCmd = QString("test -d \"%1\" && echo 'EXISTS' || echo 'NOT_FOUND'").arg(remotePath);
        QString testResult = executeCommand(testCmd, true);
        if (testResult.contains("NOT_FOUND")) {
            emit remoteDirError("目录不存在: " + remotePath);
            return;
        }
        emit remoteDirListed(remotePath, QList<RemoteFileInfo>());
        m_currentRemoteDir = remotePath;
        return;
    }

    QList<RemoteFileInfo> fileList;
    QStringList lines = output.split("\n", QString::SkipEmptyParts);

    // 使用正则解析 ls -l 输出，正确处理文件名含空格的情况
    // long-iso 格式: perms links owner group size YYYY-MM-DD HH:MM filename
    // 利用日期时间格式固定这一特征，用正则匹配到时间字段后，剩余部分即为文件名
    QRegularExpression longIsoRe(
        "^(\\S+)\\s+"          // (1) perms
        "(\\d+)\\s+"           // (2) links
        "(\\S+)\\s+"           // (3) owner
        "(\\S+)\\s+"           // (4) group
        "(\\S+)\\s+"           // (5) size (可能含逗号，如 4,608,000)
        "(\\d{4}-\\d{2}-\\d{2})\\s+"  // (6) date (YYYY-MM-DD)
        "(\\d{2}:\\d{2})\\s+"  // (7) time (HH:MM)
        "(.+)$"                // (8) filename (可含空格)
    );

    // 默认格式: perms links owner group size Mon DD HH:MM/YYYY filename
    // 先按空白分割，再根据列数判断格式
    QRegularExpression defaultTimeRe("^\\d{2}:\\d{2}$");

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("total ") || trimmed.isEmpty())
            continue;

        RemoteFileInfo info;

        // 优先尝试 long-iso 格式（日期格式固定，最可靠）
        QRegularExpressionMatch isoMatch = longIsoRe.match(trimmed);
        if (isoMatch.hasMatch()) {
            info.perms   = isoMatch.captured(1);
            info.isDir   = info.perms.startsWith('d');
            info.owner   = isoMatch.captured(3);  // 添加 owner
            info.size    = isoMatch.captured(5).replace(",", "").toLongLong();
            info.modTime = isoMatch.captured(6) + " " + isoMatch.captured(7);
            info.name    = isoMatch.captured(8);
        } else {
            // 回退到默认 ls -l 格式：按空白分割
            QStringList parts = trimmed.split(QRegExp("\\s+"), QString::SkipEmptyParts);
            if (parts.size() < 8) continue;

            info.perms = parts[0];
            info.isDir = info.perms.startsWith('d');
            info.owner = parts[2];  // 添加 owner
            info.size  = parts[4].replace(",", "").toLongLong();

            // parts[5]=Mon, parts[6]=DD, parts[7]=HH:MM 或 YYYY
            // 若 parts 更多，则 parts[8..] 为文件名
            if (parts.size() >= 9 && !defaultTimeRe.match(parts[7]).hasMatch()) {
                // parts[7] 不是时间（如 "2025"），说明是旧文件格式：Mon DD YYYY filename
                info.modTime = parts[5] + " " + parts[6] + " " + parts[7];
                info.name    = parts.mid(8).join(" ");
            } else {
                // 标准格式：Mon DD HH:MM filename
                info.modTime = parts[5] + " " + parts[6] + " " + parts[7];
                info.name    = parts.mid(8).join(" ");
            }
        }

        if (info.name.isEmpty()) continue;

        if (info.isDir && info.name.endsWith('/')) {
            info.name.chop(1);
        }

        // 去除可能的 ANSI 颜色转义码（双重保险）
        info.name = stripAnsiCodes(info.name);

        fileList.append(info);
    }

    m_currentRemoteDir = remotePath;
    emit remoteDirListed(remotePath, fileList);
    qDebug() << "[SshManager] 列出目录:" << remotePath << "文件数:" << fileList.size();
}

// ========================================================================
// 静态方法：用独立 QProcess 执行单条远程命令（线程安全）
// 可在后台线程调用，不依赖 SshManager 的交互式会话
// ========================================================================
QString SshManager::executeOnce(const QString &host,
                               const QString &user,
                               const QString &password,
                               int port,
                               const QString &plinkPath,
                               const QString &command,
                               int timeoutMs)
{
    QProcess proc;
    QStringList args;

    if (!plinkPath.isEmpty()) {
        proc.setProgram(plinkPath);
        args << "-batch" << "-pw" << password
             << "-P" << QString::number(port)
             << QString("%1@%2").arg(user, host)
             << command;
    } else {
        // 尝试 sshpass
        QString sshpass = QStandardPaths::findExecutable("sshpass");
        if (!sshpass.isEmpty()) {
            proc.setProgram(sshpass);
            args << "-p" << password
                 << "ssh"
                 << "-o" << "StrictHostKeyChecking=no"
                 << "-o" << "ConnectTimeout=10"
                 << "-p" << QString::number(port)
                 << QString("%1@%2").arg(user, host)
                 << command;
        } else {
            // 尝试 ssh（需要密钥）
            QString ssh = QStandardPaths::findExecutable("ssh");
            if (ssh.isEmpty()) {
                // 尝试 Windows OpenSSH 路径
                QStringList paths;
                paths << "C:/Windows/System32/OpenSSH/ssh.exe"
                      << "C:/Windows/SysWOW64/OpenSSH/ssh.exe";
                for (const QString &p : paths) {
                    if (QFile::exists(p)) { ssh = p; break; }
                }
            }
            if (ssh.isEmpty()) return "";
            proc.setProgram(ssh);
            args << "-o" << "StrictHostKeyChecking=no"
                 << "-o" << "ConnectTimeout=10"
                 << "-o" << "BatchMode=yes"
                 << "-p" << QString::number(port)
                 << QString("%1@%2").arg(user, host)
                 << command;
        }
    }

    proc.setArguments(args);
    proc.start();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        return "";
    }

#ifdef Q_OS_WIN
    // Windows 上 plink 把远程 UTF-8 输出转成了本地编码 (GBK)
    return QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
#else
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
#endif
}

