#ifndef COSMANAGER_H
#define COSMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>
#include <QFile>

// COS对象信息结构体
struct CosObjectInfo {
    QString key;           // 对象键（路径）
    QString lastModified;  // 最后修改时间
    qint64 size;           // 文件大小（字节）
    QString etag;          // ETag
};

// Bucket信息
struct BucketInfo {
    QString name;
    QString location;
    QString creationDate;
};

class CosManager : public QObject
{
    Q_OBJECT

public:
    static CosManager* instance();

    void setCredentials(const QString &secretId, const QString &secretKey, const QString &region);
    void setBucket(const QString &bucket);

    // 获取当前配置
    QString currentBucket() const { return m_bucket; }
    QString currentRegion() const { return m_region; }

    // API接口
    void listBuckets();
    void listObjects(const QString &prefix = "", const QString &delimiter = "/");
    void uploadFile(const QString &key, const QString &localPath);
    void downloadFile(const QString &key, const QString &savePath);
    void headObject(const QString &key);
    void deleteObject(const QString &key);

    // 取消当前操作
    void abortAll();

signals:
    // 登录/连接
    void loginResult(bool success, const QString &message);
    void bucketListReady(const QList<BucketInfo> &buckets);

    // 对象列表
    void objectListReady(const QList<CosObjectInfo> &objects, const QStringList &commonPrefixes);

    // 上传
    void uploadProgress(const QString &key, qint64 sent, qint64 total);
    void uploadFinished(const QString &key, bool success, const QString &message);

    // 下载
    void downloadProgress(const QString &key, qint64 received, qint64 total);
    void downloadFinished(const QString &key, bool success, const QString &message);

    // HEAD
    void headObjectResult(const QString &key, qint64 size, const QString &lastModified, const QString &contentType);
    void headObjectError(const QString &key, const QString &message);

    // 错误
    void networkError(const QString &message);

private:
    explicit CosManager(QObject *parent = nullptr);

    // 腾讯云 COS v5 签名
    QString buildAuthorization(const QString &httpMethod,
                             const QString &uriPathname,
                             const QMap<QString, QString> &headers,
                             const QMap<QString, QString> &params);

    // 构造请求URL
    QString buildUrl(const QString &bucket,
                     const QString &path,
                     const QMap<QString, QString> &params = QMap<QString, QString>());

    // 解析XML响应
    QList<BucketInfo> parseBucketList(const QByteArray &xml);
    QList<CosObjectInfo> parseObjectList(const QByteArray &xml, QStringList &commonPrefixes);

private:
    static CosManager *m_instance;
    QNetworkAccessManager *m_networkManager;

    QString m_secretId;
    QString m_secretKey;
    QString m_region;
    QString m_bucket;

    // 追踪活跃的上传/下载
    QMap<QNetworkReply*, QString> m_replyKeyMap;    // reply -> key
    QMap<QNetworkReply*, QFile*> m_replyFileMap;     // reply -> 上传/下载文件
    QList<QNetworkReply*> m_activeReplies;
};

#endif // COSMANAGER_H
