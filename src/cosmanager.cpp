#include "cosmanager.h"
#include <QNetworkRequest>
#include <QHttpMultiPart>
#include <QSslConfiguration>
#include <QUrlQuery>
#include <QDateTime>
#include <QXmlStreamReader>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDebug>
#include <QMessageAuthenticationCode>

CosManager *CosManager::m_instance = nullptr;

CosManager* CosManager::instance()
{
    if (!m_instance) {
        m_instance = new CosManager();
    }
    return m_instance;
}

CosManager::CosManager(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // SSL配置
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    sslConfig.setProtocol(QSsl::TlsV1_2);
}

void CosManager::setCredentials(const QString &secretId, const QString &secretKey, const QString &region)
{
    m_secretId  = secretId;
    m_secretKey = secretKey;
    m_region    = region;
    qDebug() << "[CosManager] 凭证已设置, Region:" << region;
}

void CosManager::setBucket(const QString &bucket)
{
    m_bucket = bucket;
    qDebug() << "[CosManager] 切换Bucket:" << bucket;
}

// ========================================================================
// 腾讯云 COS 签名 v5 实现
// 文档: https://cloud.tencent.com/document/product/436/7778
// ========================================================================

// HMAC-SHA1 封装
static QByteArray hmacSha1(const QByteArray &key, const QByteArray &data)
{
    QMessageAuthenticationCode hmac(QCryptographicHash::Sha1);
    hmac.setKey(key);
    hmac.addData(data);
    return hmac.result();
}

// SHA1 封装
static QByteArray sha1(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex();
}

QString CosManager::buildAuthorization(const QString &httpMethod,
                                     const QString &uriPathname,
                                     const QMap<QString, QString> &headers,
                                     const QMap<QString, QString> &params)
{
    // 1. 签名生效时间
    qint64 now  = QDateTime::currentSecsSinceEpoch();
    qint64 end  = now + 3600;          // 1 小时有效期
    QString keyTime  = QString("%1;%2").arg(now).arg(end);
    QString signTime = keyTime;             // 与 KeyTime 相同

    // 2. 计算 SignKey
    //    SignKey = HMAC-SHA1(SecretKey, KeyTime)
    QByteArray signKey = hmacSha1(m_secretKey.toUtf8(), keyTime.toUtf8());

    // 3. 构造 HttpString
    //    HttpMethod\nUriPathname\nHttpParameters\nHttpHeaders\n
    QString httpString;
    httpString += httpMethod.toUpper() + "\n";
    httpString += uriPathname + "\n";

    // HttpParameters: 字典序 key=value&... 最后加 \n
    {
        QStringList paramParts;
        for (auto it = params.begin(); it != params.end(); ++it) {
            paramParts << (it.key().toLower() + "=" + it.value());
        }
        paramParts.sort();
        httpString += paramParts.join("&") + "\n";
    }

    // HttpHeaders: 字典序 lowercase_key=url_encode(value)&... 最后加 \n
    //   只签 host 和 x-cos-*
    {
        QStringList headerParts;
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            QString k = it.key().toLower();
            if (k == "host" || k.startsWith("x-cos-")) {
                headerParts << (k + "=" + it.value().trimmed());
            }
        }
        headerParts.sort();
        httpString += headerParts.join("&") + "\n";
    }

    // 4. 计算 StringToSign
    //    sha1\n[KeyTime]\n[SHA1(HttpString)]\n
    QByteArray sha1HttpString = sha1(httpString.toUtf8());
    QString stringToSign = QString("sha1\n%1\n%2\n")
                          .arg(keyTime, QString(sha1HttpString));

    // 5. 计算 Signature
    QByteArray signature = hmacSha1(signKey, stringToSign.toUtf8());

    // 6. 构造 Authorization
    //    q-sign-algorithm=sha1
    //    &q-ak=[SecretId]
    //    &q-sign-time=[SignTime]
    //    &q-key-time=[KeyTime]
    //    &q-header-list=[SignedHeaderList]
    //    &q-url-param-list=[SignedParameterList]
    //    &q-signature=[Signature]

    QStringList headerKeys;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        QString k = it.key().toLower();
        if (k == "host" || k.startsWith("x-cos-")) {
            headerKeys << k;
        }
    }
    headerKeys.sort();
    QString headerList = headerKeys.join(";");

    QStringList paramKeys;
    for (auto it = params.begin(); it != params.end(); ++it) {
        paramKeys << it.key().toLower();
    }
    paramKeys.sort();
    QString paramList = paramKeys.join(";");

    QString auth;
    auth += "q-sign-algorithm=sha1";
    auth += "&q-ak=" + m_secretId;
    auth += "&q-sign-time=" + signTime;
    auth += "&q-key-time=" + keyTime;
    auth += "&q-header-list=" + headerList;
    auth += "&q-url-param-list=" + paramList;
    auth += "&q-signature=" + QString(signature.toHex());

    qDebug() << "[CosManager] Authorization:" << auth;
    return auth;
}

QString CosManager::buildUrl(const QString &bucket, const QString &path,
                              const QMap<QString, QString> &params)
{
    QString baseUrl;
    if (bucket.isEmpty()) {
        // Service 级别
        baseUrl = QString("https://cos.%1.myqcloud.com/").arg(m_region);
    } else {
        baseUrl = QString("https://%1.cos.%2.myqcloud.com%3")
                      .arg(bucket, m_region, path);
    }

    if (!params.isEmpty()) {
        QUrlQuery query;
        for (auto it = params.begin(); it != params.end(); ++it) {
            query.addQueryItem(it.key(), it.value());
        }
        baseUrl += "?" + query.toString();
    }
    return baseUrl;
}

void CosManager::listBuckets()
{
    QString path = "/";
    QString url  = buildUrl("", path);

    QMap<QString, QString> headers;
    headers["Host"] = QString("cos.%1.myqcloud.com").arg(m_region);

    QString auth = buildAuthorization("GET", path, headers, {});

    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", auth.toUtf8());
    request.setRawHeader("Host", headers["Host"].toUtf8());

    qDebug() << "[CosManager] GET listBuckets, URL:" << url;

    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_activeReplies.removeOne(reply);
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            QList<BucketInfo> buckets = parseBucketList(reply->readAll());
            emit bucketListReady(buckets);
            emit loginResult(true, "连接成功");
            qDebug() << "[CosManager] listBuckets 成功, 数量:" << buckets.size();
        } else {
            QString errorMsg = QString("获取Bucket列表失败: %1").arg(reply->errorString());
            emit loginResult(false, errorMsg);
            emit networkError(errorMsg);
            qWarning() << "[CosManager]" << errorMsg;
        }
    });
}

void CosManager::listObjects(const QString &prefix, const QString &delimiter)
{
    if (m_bucket.isEmpty()) {
        emit networkError("未选择Bucket");
        qDebug() << "[CosManager] listObjects 失败: 未选择Bucket";
        return;
    }

    QString path = "/";
    QMap<QString, QString> params;
    if (!prefix.isEmpty()) {
        params["prefix"] = prefix;
    }
    if (!delimiter.isEmpty()) {
        params["delimiter"] = delimiter;
    }

    QString url = buildUrl(m_bucket, path, params);

    QMap<QString, QString> headers;
    headers["Host"] = QString("%1.cos.%2.myqcloud.com").arg(m_bucket, m_region);

    QString auth = buildAuthorization("GET", path, headers, params);

    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", auth.toUtf8());
    request.setRawHeader("Host", headers["Host"].toUtf8());

    qDebug() << "[CosManager] GET listObjects, prefix:" << prefix << "URL:" << url;

    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_activeReplies.removeOne(reply);
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            QStringList commonPrefixes;
            QList<CosObjectInfo> objects = parseObjectList(reply->readAll(), commonPrefixes);
            emit objectListReady(objects, commonPrefixes);
            qDebug() << "[CosManager] listObjects 成功, 文件数:" << objects.size();
        } else {
            emit networkError(QString("获取对象列表失败: %1").arg(reply->errorString()));
            qWarning() << "[CosManager] listObjects 失败:" << reply->errorString();
        }
    });
}

void CosManager::uploadFile(const QString &key, const QString &localPath)
{
    if (m_bucket.isEmpty()) {
        emit uploadFinished(key, false, "未选择Bucket");
        return;
    }

    QFile *file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        emit uploadFinished(key, false, "无法打开文件: " + localPath);
        delete file;
        return;
    }

    qDebug() << "[CosManager] PUT uploadFile, key:" << key << "size:" << file->size();

    QString path = "/" + key;
    QMap<QString, QString> headers;
    headers["Host"] = QString("%1.cos.%2.myqcloud.com").arg(m_bucket, m_region);
    // 必须带 Content-Length，否则 COS 返回 411
    headers["Content-Length"] = QString::number(file->size());

    QString auth = buildAuthorization("PUT", path, headers, {});

    QString url = buildUrl(m_bucket, path);
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", auth.toUtf8());
    request.setRawHeader("Host", headers["Host"].toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    request.setHeader(QNetworkRequest::ContentLengthHeader, file->size());

    QNetworkReply *reply = m_networkManager->put(request, file);
    m_activeReplies.append(reply);
    m_replyKeyMap[reply]  = key;
    m_replyFileMap[reply] = file;

    // 进度
    connect(reply, &QNetworkReply::uploadProgress, this, [this, reply](qint64 sent, qint64 total) {
        QString k = m_replyKeyMap.value(reply, "");
        if (!k.isEmpty()) {
            emit uploadProgress(k, sent, total);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_activeReplies.removeOne(reply);
        QString key = m_replyKeyMap.take(reply);
        QFile *file = m_replyFileMap.take(reply);
        if (file) { file->close(); delete file; }
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            emit uploadFinished(key, true, "上传成功");
            qDebug() << "[CosManager] uploadFile 成功:" << key;
        } else {
            emit uploadFinished(key, false, "上传失败: " + reply->errorString());
            qWarning() << "[CosManager] uploadFile 失败:" << reply->errorString();
        }
    });
}

void CosManager::downloadFile(const QString &key, const QString &savePath)
{
    if (m_bucket.isEmpty()) {
        emit downloadFinished(key, false, "未选择Bucket");
        return;
    }

    qDebug() << "[CosManager] GET downloadFile, key:" << key << "saveTo:" << savePath;

    QString path = "/" + key;
    QMap<QString, QString> headers;
    headers["Host"] = QString("%1.cos.%2.myqcloud.com").arg(m_bucket, m_region);

    QString auth = buildAuthorization("GET", path, headers, {});

    QString url = buildUrl(m_bucket, path);
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", auth.toUtf8());
    request.setRawHeader("Host", headers["Host"].toUtf8());

    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);
    m_replyKeyMap[reply] = key;

    // 进度
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        QString k = m_replyKeyMap.value(reply, "");
        if (!k.isEmpty()) {
            emit downloadProgress(k, received, total);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, savePath]() {
        m_activeReplies.removeOne(reply);
        QString key = m_replyKeyMap.take(reply);
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            // 确保父目录存在
            QFileInfo fi(savePath);
            QDir().mkpath(fi.absolutePath());

            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                emit downloadFinished(key, true, "下载成功");
                qDebug() << "[CosManager] downloadFile 成功:" << key;
            } else {
                emit downloadFinished(key, false, "无法写入文件: " + savePath);
            }
        } else {
            emit downloadFinished(key, false, "下载失败: " + reply->errorString());
            qWarning() << "[CosManager] downloadFile 失败:" << reply->errorString();
        }
    });
}

void CosManager::headObject(const QString &key)
{
    if (m_bucket.isEmpty()) {
        emit headObjectError(key, "未选择Bucket");
        return;
    }

    QString path = "/" + key;
    QMap<QString, QString> headers;
    headers["Host"] = QString("%1.cos.%2.myqcloud.com").arg(m_bucket, m_region);

    QString auth = buildAuthorization("HEAD", path, headers, {});

    QString url = buildUrl(m_bucket, path);
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", auth.toUtf8());
    request.setRawHeader("Host", headers["Host"].toUtf8());

    QNetworkReply *reply = m_networkManager->head(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        m_activeReplies.removeOne(reply);
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            qint64 size = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            QString lastModified = reply->rawHeader("Last-Modified");
            QString contentType  = reply->rawHeader("Content-Type");
            emit headObjectResult(key, size, lastModified, contentType);
        } else {
            emit headObjectError(key, reply->errorString());
        }
    });
}

void CosManager::deleteObject(const QString &key)
{
    if (m_bucket.isEmpty()) {
        emit networkError("未选择Bucket");
        return;
    }

    QString path = "/" + key;
    QMap<QString, QString> headers;
    headers["Host"] = QString("%1.cos.%2.myqcloud.com").arg(m_bucket, m_region);

    QString auth = buildAuthorization("DELETE", path, headers, {});

    QString url = buildUrl(m_bucket, path);
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization", auth.toUtf8());
    request.setRawHeader("Host", headers["Host"].toUtf8());

    QNetworkReply *reply = m_networkManager->deleteResource(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_activeReplies.removeOne(reply);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit networkError("删除失败: " + reply->errorString());
        }
    });
}

void CosManager::abortAll()
{
    for (QNetworkReply *reply : m_activeReplies) {
        reply->abort();
    }
    m_activeReplies.clear();
}

// ========================================================================
// XML 解析
// ========================================================================

QList<BucketInfo> CosManager::parseBucketList(const QByteArray &xml)
{
    QList<BucketInfo> buckets;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == "Bucket") {
            BucketInfo info;
            while (!(reader.isEndElement() && reader.name() == "Bucket")) {
                reader.readNext();
                if (reader.isStartElement()) {
                    if (reader.name() == "Name") {
                        info.name = reader.readElementText();
                    } else if (reader.name() == "Location") {
                        info.location = reader.readElementText();
                    } else if (reader.name() == "CreationDate") {
                        info.creationDate = reader.readElementText();
                    }
                }
            }
            buckets.append(info);
        }
    }
    return buckets;
}

QList<CosObjectInfo> CosManager::parseObjectList(const QByteArray &xml, QStringList &commonPrefixes)
{
    QList<CosObjectInfo> objects;
    commonPrefixes.clear();
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == "Contents") {
                CosObjectInfo info;
                while (!(reader.isEndElement() && reader.name() == "Contents")) {
                    reader.readNext();
                    if (reader.isStartElement()) {
                        if (reader.name() == "Key") {
                            info.key = reader.readElementText();
                        } else if (reader.name() == "LastModified") {
                            info.lastModified = reader.readElementText();
                        } else if (reader.name() == "Size") {
                            info.size = reader.readElementText().toLongLong();
                        } else if (reader.name() == "ETag") {
                            info.etag = reader.readElementText();
                        }
                    }
                }
                objects.append(info);
            } else if (reader.name() == "CommonPrefixes") {
                while (!(reader.isEndElement() && reader.name() == "CommonPrefixes")) {
                    reader.readNext();
                    if (reader.isStartElement() && reader.name() == "Prefix") {
                        commonPrefixes.append(reader.readElementText());
                    }
                }
            }
        }
    }
    return objects;
}
