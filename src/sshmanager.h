#ifndef SSHMANAGER_H
#define SSHMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

// 远程文件信息
struct RemoteFileInfo {
    QString name;       // 文件名
    QString owner;      // 文件所有者（从 ls -l 提取）
    bool    isDir = false;      // 是否是目录
    qint64  size = 0;           // 文件大小（目录为0）
    QString modTime;    // 修改时间
    QString perms;      // 权限字符串
};
Q_DECLARE_METATYPE(RemoteFileInfo)

class SshManager : public QObject
{
    Q_OBJECT

public:
    static SshManager* instance();

    // 去除 ANSI 颜色转义码（防止 ls --color 混入文件名）
    static QString stripAnsiCodes(const QString &text);

    // SSH连接配置
    void setConnectionParams(const QString &host,
                            const QString &user,
                            const QString &password,
                            int port = 22);

    // 连接管理
    bool connectToServer();
    void disconnectFromServer(bool emitSignal = true);
    bool isConnected() const;

    // 获取当前连接参数（供后台线程使用）
    QString host() const { return m_host; }
    QString user() const { return m_user; }
    QString password() const { return m_password; }
    int port() const { return m_port; }
    QString plinkPath() const { return m_plinkPath; }

    // 执行远程命令（同步，waitForFinish=true 时等待结果）
    QString executeCommand(const QString &command, bool waitForFinish = true);

    // 静态方法：用独立 QProcess 执行单条远程命令（线程安全，可在后台线程调用）
    // 返回命令输出，失败返回空字符串
    static QString executeOnce(const QString &host,
                               const QString &user,
                               const QString &password,
                               int port,
                               const QString &plinkPath,
                               const QString &command,
                               int timeoutMs = 30000);

    bool executeCommandAsync(const QString &command);

    // 文件传输（异步，结果通过信号通知）
    void uploadFile(const QString &localPath, const QString &remotePath);
    void downloadFile(const QString &remotePath, const QString &localPath);
    bool downloadDir(const QString &remoteDir, const QString &localDir);

    // 远程目录浏览
    void listRemoteDir(const QString &remotePath = ".");
    void changeRemoteDir(const QString &path);
    QString currentRemoteDir() const { return m_currentRemoteDir; }

    // 远程文件/目录删除
    bool deleteRemoteFile(const QString &remotePath);
    bool deleteRemoteDir(const QString &remotePath);

signals:
    void connectionEstablished();
    void connectionLost();
    void connectionError(const QString &errorMessage);
    void commandOutput(const QString &output);
    void commandFinished(int exitCode);
    void remoteDirListed(const QString &path, const QList<RemoteFileInfo> &fileList);
    void remoteDirError(const QString &errorMessage);

    // 异步上传/下载信号
    void uploadProgress(const QString &key, qint64 sent, qint64 total);
    void uploadFinished(const QString &key, bool success, const QString &message);
    void downloadProgress(const QString &key, qint64 received, qint64 total);
    void downloadFinished(const QString &key, bool success, const QString &message);

private:
    explicit SshManager(QObject *parent = nullptr);
    ~SshManager();

    // 自动连接：优先 plink → sshpass → 密钥
    bool autoConnect();

    // 检查SSH工具是否可用
    bool checkSshTools();

    // 配置SSH密钥认证
    bool setupSshKeyAuth();

    // 查找 plink.exe / pscp.exe（查程序目录 → 当前工作目录 → PATH）
    QString findPlink();
    QString findPscp();

    // 中文文件名用 plink + cat 下载（绕过 pscp 命令行编码问题）
    void downloadFileViaPlinkCat(const QString &remotePath, const QString &localPath);

    // 各种连接方式尝试
    bool tryInteractiveSsh();
    bool tryPlink(const QString &plinkPath);
    bool trySshpass();
    bool trySshKey();

    static SshManager *m_instance;

    QString m_host;
    QString m_user;
    QString m_password;
    int m_port;

    QProcess *m_sshProcess;

    bool m_isConnected;
    bool m_useKeyAuth;

    // 如果使用 plink 连接，保存 plink.exe 路径供后续命令使用
    QString m_plinkPath;

    // 当前远程目录
    QString m_currentRemoteDir;

    // 跟踪异步上传/下载进程
    QMap<QString, QProcess*> m_uploadProcesses;
    QMap<QString, QProcess*> m_downloadProcesses;
};

#endif // SSHMANAGER_H
