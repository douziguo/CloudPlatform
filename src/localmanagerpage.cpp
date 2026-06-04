#include "localmanagerpage.h"
#include "authmanager.h"
#include "sshmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QSplitter>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QProcess>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QTimer>
#include <QScrollBar>
#include <QFileInfo>
#include <QTextStream>
#include <QCoreApplication>
#include <QListWidget>

// ──────────────────────────────────────────
//  LocalManagerPage  构造
// ──────────────────────────────────────────
LocalManagerPage::LocalManagerPage(QWidget *parent)
    : QWidget(parent)
    , m_uploadTable(nullptr)
    , m_downloadTable(nullptr)
    , m_projectListWidget(nullptr)
    , m_backupAllBtn(nullptr)
    , m_backupSelBtn(nullptr)
    , m_restoreAllBtn(nullptr)
    , m_restoreSelBtn(nullptr)
    , m_progressBar(nullptr)
    , m_statusLabel(nullptr)
    , m_logEdit(nullptr)
{
    initUI();
    refreshHistory();
}

// ──────────────────────────────────────────
//  initUI
// ──────────────────────────────────────────
void LocalManagerPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    QTabWidget *tabs = new QTabWidget();
    tabs->addTab(buildHistoryTab(), QString::fromUtf8("操作历史"));
    tabs->addTab(buildBackupTab(),  QString::fromUtf8("备份 / 恢复"));

    mainLayout->addWidget(tabs);
}

// ──────────────────────────────────────────
//  buildHistoryTab
// ──────────────────────────────────────────
QWidget *LocalManagerPage::buildHistoryTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(6);

    // ----- 上传记录 -----
    QGroupBox *upGrp = new QGroupBox(QString::fromUtf8("上传记录"));
    QVBoxLayout *upLay = new QVBoxLayout(upGrp);

    m_uploadTable = new QTableWidget(0, 4);
    m_uploadTable->setHorizontalHeaderLabels({
        QString::fromUtf8("文件名"),
        QString::fromUtf8("远程路径"),
        QString::fromUtf8("上传用户"),
        QString::fromUtf8("上传时间")
    });
    m_uploadTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_uploadTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_uploadTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_uploadTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_uploadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_uploadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_uploadTable->setAlternatingRowColors(true);
    m_uploadTable->setMaximumHeight(160);
    upLay->addWidget(m_uploadTable);
    lay->addWidget(upGrp);

    // ----- 下载记录 -----
    QGroupBox *dlGrp = new QGroupBox(QString::fromUtf8("下载记录"));
    QVBoxLayout *dlLay = new QVBoxLayout(dlGrp);

    m_downloadTable = new QTableWidget(0, 5);
    m_downloadTable->setHorizontalHeaderLabels({
        QString::fromUtf8("文件名"),
        QString::fromUtf8("远程路径"),
        QString::fromUtf8("本地路径"),
        QString::fromUtf8("大小"),
        QString::fromUtf8("下载时间")
    });
    m_downloadTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_downloadTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_downloadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_downloadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadTable->setAlternatingRowColors(true);
    m_downloadTable->setMaximumHeight(160);
    dlLay->addWidget(m_downloadTable);
    lay->addWidget(dlGrp);

    // ----- 操作按钮 -----
    QHBoxLayout *btnLay = new QHBoxLayout();
    btnLay->addStretch();

    QPushButton *refreshBtn = new QPushButton(QString::fromUtf8("刷新记录"));
    refreshBtn->setFixedWidth(100);
    connect(refreshBtn, &QPushButton::clicked, this, &LocalManagerPage::refreshHistory);
    btnLay->addWidget(refreshBtn);

    QPushButton *exportBtn = new QPushButton(QString::fromUtf8("导出CSV"));
    exportBtn->setFixedWidth(100);
    connect(exportBtn, &QPushButton::clicked, this, &LocalManagerPage::onExportHistory);
    btnLay->addWidget(exportBtn);

    lay->addLayout(btnLay);
    lay->addStretch();

    return w;
}

// ──────────────────────────────────────────
//  buildBackupTab
// ──────────────────────────────────────────
QWidget *LocalManagerPage::buildBackupTab()
{
    QWidget *w = new QWidget();
    QHBoxLayout *hlay = new QHBoxLayout(w);
    hlay->setContentsMargins(4, 4, 4, 4);
    hlay->setSpacing(8);

    // ── 左侧：项目列表 ──
    QGroupBox *projGrp = new QGroupBox(QString::fromUtf8("项目列表（多选）"));
    QVBoxLayout *projLay = new QVBoxLayout(projGrp);
    projGrp->setFixedWidth(220);

    m_projectListWidget = new QListWidget();
    m_projectListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_projectListWidget, &QListWidget::itemSelectionChanged,
            this, &LocalManagerPage::onProjectSelectionChanged);
    projLay->addWidget(m_projectListWidget);

    QPushButton *refreshProjBtn = new QPushButton(QString::fromUtf8("刷新项目列表"));
    refreshProjBtn->setFixedHeight(28);
    connect(refreshProjBtn, &QPushButton::clicked, this, [this]() {
        m_cachedProjects = AuthManager::instance()->getAllProjects();
        m_projectListWidget->clear();
        for (const auto &p : m_cachedProjects) {
            QListWidgetItem *item = new QListWidgetItem(
                QString("[%1] %2").arg(p.code, p.name));
            item->setData(Qt::UserRole, p.id);
            m_projectListWidget->addItem(item);
        }
        log(QString::fromUtf8("[刷新] 项目列表已更新，共 %1 个项目").arg(m_cachedProjects.size()));
    });
    projLay->addWidget(refreshProjBtn);
    hlay->addWidget(projGrp);

    // ── 右侧：操作区 ──
    QWidget *rightWidget = new QWidget();
    QVBoxLayout *rightLay = new QVBoxLayout(rightWidget);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(8);

    // 说明文字
    QLabel *hintLabel = new QLabel(
        QString::fromUtf8(
            "<b>备份</b>：将项目元数据（JSON）及云端文件下载打包为 ZIP 压缩包。<br>"
            "<b>恢复</b>：从 ZIP 包解压，将项目元数据导入本地数据库，文件上传回云端。<br>"
            "<font color='#f44747'>提示：备份/恢复操作依赖SSH连接，请先确保SSH已连接。</font>"));
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet("background:#2d2d30; padding:6px; border-radius:4px; color:#9cdcfe;");
    rightLay->addWidget(hintLabel);

    // 操作按钮组
    QGroupBox *opGrp = new QGroupBox(QString::fromUtf8("操作"));
    QGridLayout *opGrid = new QGridLayout(opGrp);
    opGrid->setSpacing(8);

    m_backupAllBtn = new QPushButton(QString::fromUtf8("⬇  一键全量备份"));
    m_backupAllBtn->setFixedHeight(36);
    m_backupAllBtn->setToolTip(QString::fromUtf8("将云端账户下所有项目数据备份到本地ZIP"));
    connect(m_backupAllBtn, &QPushButton::clicked, this, &LocalManagerPage::onBackupAll);

    m_backupSelBtn = new QPushButton(QString::fromUtf8("⬇  备份选定项目"));
    m_backupSelBtn->setFixedHeight(36);
    m_backupSelBtn->setEnabled(false);
    m_backupSelBtn->setToolTip(QString::fromUtf8("将左侧选中的项目备份到本地ZIP"));
    connect(m_backupSelBtn, &QPushButton::clicked, this, &LocalManagerPage::onBackupSelected);

    m_restoreAllBtn = new QPushButton(QString::fromUtf8("⬆  一键全量恢复"));
    m_restoreAllBtn->setFixedHeight(36);
    m_restoreAllBtn->setToolTip(QString::fromUtf8("从本地备份ZIP恢复所有项目数据到云端"));
    connect(m_restoreAllBtn, &QPushButton::clicked, this, &LocalManagerPage::onRestoreAll);

    m_restoreSelBtn = new QPushButton(QString::fromUtf8("⬆  恢复选定项目"));
    m_restoreSelBtn->setFixedHeight(36);
    m_restoreSelBtn->setEnabled(false);
    m_restoreSelBtn->setToolTip(QString::fromUtf8("从本地备份ZIP恢复左侧选中的项目到云端"));
    connect(m_restoreSelBtn, &QPushButton::clicked, this, &LocalManagerPage::onRestoreSelected);

    opGrid->addWidget(m_backupAllBtn, 0, 0);
    opGrid->addWidget(m_backupSelBtn, 0, 1);
    opGrid->addWidget(m_restoreAllBtn, 1, 0);
    opGrid->addWidget(m_restoreSelBtn, 1, 1);
    rightLay->addWidget(opGrp);

    // 进度条 + 状态
    QGroupBox *progGrp = new QGroupBox(QString::fromUtf8("进度"));
    QVBoxLayout *progLay = new QVBoxLayout(progGrp);
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    progLay->addWidget(m_progressBar);
    m_statusLabel = new QLabel(QString::fromUtf8("就绪"));
    m_statusLabel->setStyleSheet("color:#9cdcfe;");
    progLay->addWidget(m_statusLabel);
    rightLay->addWidget(progGrp);

    // 操作日志
    QGroupBox *logGrp = new QGroupBox(QString::fromUtf8("操作日志"));
    QVBoxLayout *logLay = new QVBoxLayout(logGrp);
    m_logEdit = new QTextEdit();
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(150);
    logLay->addWidget(m_logEdit);

    QPushButton *clearLogBtn = new QPushButton(QString::fromUtf8("清除日志"));
    clearLogBtn->setFixedWidth(90);
    connect(clearLogBtn, &QPushButton::clicked, m_logEdit, &QTextEdit::clear);
    QHBoxLayout *logBtnLay = new QHBoxLayout();
    logBtnLay->addStretch();
    logBtnLay->addWidget(clearLogBtn);
    logLay->addLayout(logBtnLay);
    rightLay->addWidget(logGrp);

    rightLay->addStretch();
    hlay->addWidget(rightWidget, 1);

    return w;
}

// ──────────────────────────────────────────
//  退出登录时重置所有状态
// ──────────────────────────────────────────
void LocalManagerPage::reset()
{
    // 清空历史记录表
    if (m_uploadTable) m_uploadTable->setRowCount(0);
    if (m_downloadTable) m_downloadTable->setRowCount(0);

    // 清空备份项目列表
    if (m_projectListWidget) m_projectListWidget->clear();

    // 清空缓存的项目数据
    m_cachedProjects.clear();

    // 清空日志
    if (m_logEdit) m_logEdit->clear();

    // 重置进度条和状态
    if (m_progressBar) m_progressBar->setValue(0);
    if (m_statusLabel) m_statusLabel->clear();
}

// ──────────────────────────────────────────
//  刷新历史记录
// ──────────────────────────────────────────
void LocalManagerPage::refreshHistory()
{
    // 上传记录
    if (m_uploadTable) {
        auto uploads = AuthManager::instance()->getUploadRecords();
        m_uploadTable->setRowCount(0);
        for (const auto &r : uploads) {
            int row = m_uploadTable->rowCount();
            m_uploadTable->insertRow(row);
            m_uploadTable->setItem(row, 0, new QTableWidgetItem(r.fileName));
            m_uploadTable->setItem(row, 1, new QTableWidgetItem(r.remotePath));
            m_uploadTable->setItem(row, 2, new QTableWidgetItem(r.username));
            m_uploadTable->setItem(row, 3, new QTableWidgetItem(r.uploadedAt));
        }
        m_uploadTable->scrollToBottom();
    }

    // 下载记录
    if (m_downloadTable) {
        auto downloads = AuthManager::instance()->getDownloadRecords();
        m_downloadTable->setRowCount(0);
        for (const auto &r : downloads) {
            int row = m_downloadTable->rowCount();
            m_downloadTable->insertRow(row);
            m_downloadTable->setItem(row, 0, new QTableWidgetItem(r.fileName));
            m_downloadTable->setItem(row, 1, new QTableWidgetItem(r.remotePath));
            m_downloadTable->setItem(row, 2, new QTableWidgetItem(r.localPath));

            // 格式化大小
            QString sizeStr;
            if (r.fileSize < 1024)
                sizeStr = QString("%1 B").arg(r.fileSize);
            else if (r.fileSize < 1024 * 1024)
                sizeStr = QString("%1 KB").arg(r.fileSize / 1024.0, 0, 'f', 1);
            else
                sizeStr = QString("%1 MB").arg(r.fileSize / (1024.0 * 1024.0), 0, 'f', 2);
            m_downloadTable->setItem(row, 3, new QTableWidgetItem(sizeStr));
            m_downloadTable->setItem(row, 4, new QTableWidgetItem(r.downloadedAt));
        }
        m_downloadTable->scrollToBottom();
    }
}

// ──────────────────────────────────────────
//  notifyUpload / notifyDownload
// ──────────────────────────────────────────
void LocalManagerPage::notifyUpload(const QString &remotePath, const QString &fileName)
{
    QString user = AuthManager::instance()->currentUser();
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    AuthManager::instance()->recordUpload(user, remotePath, fileName, ts);
    // 追加一行到表格（不全量刷新）
    if (m_uploadTable) {
        int row = m_uploadTable->rowCount();
        m_uploadTable->insertRow(row);
        m_uploadTable->setItem(row, 0, new QTableWidgetItem(fileName));
        m_uploadTable->setItem(row, 1, new QTableWidgetItem(remotePath));
        m_uploadTable->setItem(row, 2, new QTableWidgetItem(user));
        m_uploadTable->setItem(row, 3, new QTableWidgetItem(ts));
        m_uploadTable->scrollToBottom();
    }
}

void LocalManagerPage::notifyDownload(const QString &remotePath, const QString &fileName,
                                      const QString &localPath, qint64 fileSize)
{
    QString user = AuthManager::instance()->currentUser();
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    AuthManager::instance()->recordDownload(user, remotePath, fileName, localPath, fileSize, ts);
    if (m_downloadTable) {
        int row = m_downloadTable->rowCount();
        m_downloadTable->insertRow(row);
        m_downloadTable->setItem(row, 0, new QTableWidgetItem(fileName));
        m_downloadTable->setItem(row, 1, new QTableWidgetItem(remotePath));
        m_downloadTable->setItem(row, 2, new QTableWidgetItem(localPath));
        QString sizeStr;
        if (fileSize < 1024)
            sizeStr = QString("%1 B").arg(fileSize);
        else if (fileSize < 1024 * 1024)
            sizeStr = QString("%1 KB").arg(fileSize / 1024.0, 0, 'f', 1);
        else
            sizeStr = QString("%1 MB").arg(fileSize / (1024.0 * 1024.0), 0, 'f', 2);
        m_downloadTable->setItem(row, 3, new QTableWidgetItem(sizeStr));
        m_downloadTable->setItem(row, 4, new QTableWidgetItem(ts));
        m_downloadTable->scrollToBottom();
    }
}

// ──────────────────────────────────────────
//  onClearHistory
// ──────────────────────────────────────────
void LocalManagerPage::onClearHistory()
{
    auto ret = QMessageBox::question(this,
        QString::fromUtf8("清除历史"),
        QString::fromUtf8("确定要清除所有历史记录吗？此操作不可撤销。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    if (m_uploadTable) m_uploadTable->setRowCount(0);
    if (m_downloadTable) m_downloadTable->setRowCount(0);
}

// ──────────────────────────────────────────
//  onExportHistory  导出CSV
// ──────────────────────────────────────────
void LocalManagerPage::onExportHistory()
{
    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出历史记录"),
        QDir::homePath() + QString("/CloudPlatform_History_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QString::fromUtf8("保存失败"), f.errorString());
        return;
    }
    QTextStream ts(&f);
    // UTF-8 BOM
    ts.setCodec("UTF-8");
    f.write("\xEF\xBB\xBF");

    ts << QString::fromUtf8("=== 上传记录 ===\n");
    ts << QString::fromUtf8("文件名,远程路径,上传用户,上传时间\n");
    if (m_uploadTable) {
        for (int r = 0; r < m_uploadTable->rowCount(); ++r) {
            QStringList cols;
            for (int c = 0; c < m_uploadTable->columnCount(); ++c)
                cols << QString("\"%1\"").arg(m_uploadTable->item(r,c) ?
                    m_uploadTable->item(r,c)->text() : "");
            ts << cols.join(",") << "\n";
        }
    }
    ts << "\n";
    ts << QString::fromUtf8("=== 下载记录 ===\n");
    ts << QString::fromUtf8("文件名,远程路径,本地路径,大小,下载时间\n");
    if (m_downloadTable) {
        for (int r = 0; r < m_downloadTable->rowCount(); ++r) {
            QStringList cols;
            for (int c = 0; c < m_downloadTable->columnCount(); ++c)
                cols << QString("\"%1\"").arg(m_downloadTable->item(r,c) ?
                    m_downloadTable->item(r,c)->text() : "");
            ts << cols.join(",") << "\n";
        }
    }
    f.close();
    QMessageBox::information(this, QString::fromUtf8("导出成功"),
        QString::fromUtf8("历史记录已导出到：\n") + path);
}

// ──────────────────────────────────────────
//  项目选中状态变化
// ──────────────────────────────────────────
void LocalManagerPage::onProjectSelectionChanged()
{
    bool hasSelect = !m_projectListWidget->selectedItems().isEmpty();
    if (m_backupSelBtn) m_backupSelBtn->setEnabled(hasSelect);
    if (m_restoreSelBtn) m_restoreSelBtn->setEnabled(hasSelect);
}

// ──────────────────────────────────────────
//  辅助：压缩目录为ZIP
// ──────────────────────────────────────────
bool LocalManagerPage::compressToZip(const QString &sourceDir, const QString &zipPath)
{
    // 先删除目标ZIP（Compress-Archive 不允许覆盖）
    QFile::remove(zipPath);

    // 使用 PowerShell Compress-Archive
    QString ps = QString(
        "Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
        .arg(QDir::toNativeSeparators(sourceDir))
        .arg(QDir::toNativeSeparators(zipPath));

    QProcess proc;
    proc.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", ps});
    if (!proc.waitForFinished(120000)) {
        log(QString::fromUtf8("[错误] PowerShell 压缩超时"));
        return false;
    }
    if (proc.exitCode() != 0) {
        QString err = QString::fromLocal8Bit(proc.readAllStandardError());
        log(QString::fromUtf8("[错误] 压缩失败：") + err.trimmed());
        return false;
    }
    return QFile::exists(zipPath);
}

// ──────────────────────────────────────────
//  辅助：解压ZIP到目录
// ──────────────────────────────────────────
bool LocalManagerPage::extractZip(const QString &zipPath, const QString &destDir)
{
    QDir().mkpath(destDir);
    QString ps = QString(
        "Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
        .arg(QDir::toNativeSeparators(zipPath))
        .arg(QDir::toNativeSeparators(destDir));

    QProcess proc;
    proc.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", ps});
    if (!proc.waitForFinished(120000)) {
        log(QString::fromUtf8("[错误] PowerShell 解压超时"));
        return false;
    }
    if (proc.exitCode() != 0) {
        QString err = QString::fromLocal8Bit(proc.readAllStandardError());
        log(QString::fromUtf8("[错误] 解压失败：") + err.trimmed());
        return false;
    }
    return true;
}

// ──────────────────────────────────────────
//  辅助：将项目元数据导出为JSON
// ──────────────────────────────────────────
bool LocalManagerPage::exportProjectsToJson(const QList<int> &projectIds, const QString &jsonPath)
{
    QJsonArray projArray;
    for (int pid : projectIds) {
        ProjectInfo p = AuthManager::instance()->getProject(pid);
        if (p.id < 0) continue;

        QJsonObject pObj;
        pObj["id"] = p.id;
        pObj["name"] = p.name;
        pObj["code"] = p.code;
        pObj["location"] = p.location;
        pObj["client"] = p.client;
        pObj["clientName"] = p.clientName;
        pObj["description"] = p.description;
        pObj["notes"] = p.notes;
        pObj["createdBy"] = p.createdBy;
        pObj["createdAt"] = p.createdAt;
        pObj["updatedAt"] = p.updatedAt;

        // 测量任务
        QJsonArray taskArray;
        auto tasks = AuthManager::instance()->getMeasureTasks(pid);
        for (const auto &t : tasks) {
            QJsonObject tObj;
            tObj["taskName"] = t.taskName;
            tObj["measureDate"] = t.measureDate;
            tObj["personnel"] = t.personnel;
            tObj["pipeStart"] = t.pipeStart;
            tObj["pipeEnd"] = t.pipeEnd;
            tObj["pipeMaterial"] = t.pipeMaterial;
            tObj["pipeDiameter"] = t.pipeDiameter;
            tObj["pipeLength"] = t.pipeLength;
            tObj["status"] = t.status;
            tObj["progress"] = t.progress;
            tObj["notes"] = t.notes;
            tObj["createdAt"] = t.createdAt;
            tObj["updatedAt"] = t.updatedAt;
            taskArray.append(tObj);
        }
        pObj["tasks"] = taskArray;
        projArray.append(pObj);
    }

    QJsonObject root;
    root["version"] = "1.0";
    root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["exportedBy"] = AuthManager::instance()->currentUser();
    root["projects"] = projArray;

    QJsonDocument doc(root);
    QFile f(jsonPath);
    if (!f.open(QIODevice::WriteOnly)) {
        log(QString::fromUtf8("[错误] 无法创建JSON文件：") + jsonPath);
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    log(QString::fromUtf8("[备份] 元数据JSON已写入：") + jsonPath);
    return true;
}

// ──────────────────────────────────────────
//  辅助：从JSON导入项目元数据
// ──────────────────────────────────────────
bool LocalManagerPage::importProjectsFromJson(const QString &jsonPath,
                                               QStringList &importLog,
                                               bool overwrite)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        importLog << QString::fromUtf8("[错误] 无法打开JSON：") + jsonPath;
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (doc.isNull() || !doc.isObject()) {
        importLog << QString::fromUtf8("[错误] JSON格式无效");
        return false;
    }

    QJsonArray projArray = doc.object()["projects"].toArray();
    int imported = 0;
    for (const auto &pVal : projArray) {
        QJsonObject pObj = pVal.toObject();
        ProjectInfo p;
        p.name        = pObj["name"].toString();
        p.code        = pObj["code"].toString();
        p.location    = pObj["location"].toString();
        p.client      = pObj["client"].toString();
        p.clientName  = pObj["clientName"].toString();
        p.description = pObj["description"].toString();
        p.notes       = pObj["notes"].toString();
        p.createdBy   = pObj["createdBy"].toString();
        p.createdAt   = pObj["createdAt"].toString();

        int newPid = AuthManager::instance()->addProject(p);
        if (newPid < 0) {
            importLog << QString::fromUtf8("[跳过] 项目已存在或导入失败：") + p.name;
            continue;
        }
        imported++;
        importLog << QString::fromUtf8("[导入] 项目：") + p.name;

        // 导入任务
        QJsonArray taskArray = pObj["tasks"].toArray();
        for (const auto &tVal : taskArray) {
            QJsonObject tObj = tVal.toObject();
            MeasureTask t;
            t.projectId = newPid;
            t.taskName = tObj["taskName"].toString();
            t.measureDate = tObj["measureDate"].toString();
            t.personnel = tObj["personnel"].toString();
            t.pipeStart = tObj["pipeStart"].toString();
            t.pipeEnd = tObj["pipeEnd"].toString();
            t.pipeMaterial = tObj["pipeMaterial"].toString();
            t.pipeDiameter = tObj["pipeDiameter"].toString();
            t.pipeLength = tObj["pipeLength"].toString();
            t.status = tObj["status"].toString();
            t.progress = tObj["progress"].toInt();
            t.notes = tObj["notes"].toString();
            AuthManager::instance()->addMeasureTask(t);
        }
        importLog << QString::fromUtf8("  └ 共导入 %1 条测量任务").arg(taskArray.size());
    }

    importLog << QString::fromUtf8("[完成] 共成功导入 %1 个项目").arg(imported);
    return true;
}

// ──────────────────────────────────────────
//  辅助：从SSH下载项目文件到本地
//  下载路径：/home/ubuntu/<clientName>/<taskName>/
// ──────────────────────────────────────────
bool LocalManagerPage::downloadProjectFilesFromServer(const QList<int> &projectIds,
                                                       const QString &localDir,
                                                       QString &errMsg)
{
    SshManager *ssh = SshManager::instance();
    if (!ssh->isConnected()) {
        errMsg = QString::fromUtf8("SSH未连接，无法下载云端文件");
        return false;
    }

    QString clientName = AuthManager::instance()->currentUserClient();
    if (clientName.isEmpty()) clientName = AuthManager::instance()->currentUser();

    QDir().mkpath(localDir);
    bool anyFail = false;

    // pscp 路径（程序目录 → 当前目录 → PATH）
    QString pscpPath = QCoreApplication::applicationDirPath() + "/pscp.exe";
    if (!QFile::exists(pscpPath)) pscpPath = "pscp.exe";

    for (int pid : projectIds) {
        ProjectInfo p = AuthManager::instance()->getProject(pid);
        if (p.id < 0) continue;

        // 使用项目自身的 clientName（若有），否则回退到当前用户的 clientName
        QString projClient = p.clientName.isEmpty() ? clientName : p.clientName;

        // 获取该项目下所有测量任务
        auto tasks = AuthManager::instance()->getMeasureTasks(pid);

        if (tasks.isEmpty()) {
            // 无任务：直接下载项目级目录 /home/ubuntu/<client>/<projectName>/
            // 注意：不能预创建目标目录，否则 pscp 会在已存在目录内再建一层
            QString remoteDir = QString("/home/ubuntu/%1/%2").arg(projClient, p.name);

            log(QString::fromUtf8("[下载] 项目 [%1]（无任务） 远端目录：%2").arg(p.name, remoteDir));

            QStringList args;
            args << "-r" << "-batch"
                 << "-pw" << ssh->password()
                 << QString("%1@%2:%3").arg(ssh->user(), ssh->host(), remoteDir)
                 << QDir::toNativeSeparators(localDir);  // 下载到父目录，pscp 自动创建 projectName/

            QProcess proc;
            proc.start(pscpPath, args);
            if (!proc.waitForFinished(300000)) {
                errMsg = QString::fromUtf8("下载超时：") + p.name;
                log(QString::fromUtf8("[错误] ") + errMsg);
                anyFail = true;
            } else if (proc.exitCode() != 0) {
                QString err = QString::fromLocal8Bit(proc.readAllStandardError());
                log(QString::fromUtf8("[警告] 项目 %1 下载可能失败：%2")
                    .arg(p.name).arg(err.trimmed().left(200)));
            } else {
                log(QString::fromUtf8("[下载] 项目 %1 完成").arg(p.name));
            }
            continue;
        }

        // 有任务：远程路径 /home/ubuntu/<clientName>/<taskName>/
        // 下载到 localDir（父目录），pscp 自动创建 taskName/ 子目录，避免嵌套
        for (const auto &task : tasks) {
            QString remoteDir = QString("/home/ubuntu/%1/%2")
                                    .arg(projClient, task.taskName);

            log(QString::fromUtf8("[下载] 任务 [%1/%2] 远端目录：%3")
                .arg(p.name, task.taskName, remoteDir));

            QStringList args;
            args << "-r" << "-batch"
                 << "-pw" << ssh->password()
                 << QString("%1@%2:%3").arg(ssh->user(), ssh->host(), remoteDir)
                 << QDir::toNativeSeparators(localDir);  // 下载到父目录，pscp 自动创建 taskName/

            QProcess proc;
            proc.start(pscpPath, args);
            if (!proc.waitForFinished(300000)) {
                errMsg = QString::fromUtf8("下载超时：") + p.name + "/" + task.taskName;
                log(QString::fromUtf8("[错误] ") + errMsg);
                anyFail = true;
            } else if (proc.exitCode() != 0) {
                QString err = QString::fromLocal8Bit(proc.readAllStandardError());
                log(QString::fromUtf8("[警告] 任务 %1/%2 下载可能失败：%3")
                    .arg(p.name, task.taskName).arg(err.trimmed().left(200)));
                // 不 fatal，继续其他任务
            } else {
                log(QString::fromUtf8("[下载] 任务 %1/%2 完成").arg(p.name, task.taskName));
            }
        }
    }
    return !anyFail;
}

// ──────────────────────────────────────────
//  辅助：将本地备份文件上传回服务器
//  本地目录结构：localDir/<taskName>/
//  上传目标：  /home/ubuntu/<clientName>/<taskName>/
// ──────────────────────────────────────────
bool LocalManagerPage::uploadProjectFilesToServer(const QString &localDir, QString &errMsg)
{
    SshManager *ssh = SshManager::instance();
    if (!ssh->isConnected()) {
        errMsg = QString::fromUtf8("SSH未连接，无法上传文件");
        return false;
    }

    QString clientName = AuthManager::instance()->currentUserClient();
    if (clientName.isEmpty()) clientName = AuthManager::instance()->currentUser();

    // 枚举 localDir 下所有目录（每个对应一个任务）
    QDir dir(localDir);
    QStringList taskDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (taskDirs.isEmpty()) {
        log(QString::fromUtf8("[恢复] 本地无任务文件目录，跳过文件上传"));
        return true;
    }

    QString pscpPath = QCoreApplication::applicationDirPath() + "/pscp.exe";
    if (!QFile::exists(pscpPath)) pscpPath = "pscp.exe";

    // 确保客户端目录存在
    QString remoteClientDir = QString("/home/ubuntu/%1").arg(clientName);
    ssh->executeCommand(QString("mkdir -p \"%1\"").arg(remoteClientDir));

    bool anyFail = false;
    for (const QString &taskName : taskDirs) {
        QString localTaskDir = QDir(localDir).filePath(taskName);

        // 上传前扁平化：消除 tar 归档自带目录层级
        flattenTaskDir(localTaskDir);

        log(QString::fromUtf8("[上传] 恢复任务 %1 → %2/%3")
            .arg(taskName, remoteClientDir, taskName));

        QStringList args;
        args << "-r" << "-batch"
             << "-pw" << ssh->password()
             << QDir::toNativeSeparators(localTaskDir)
             << QString("%1@%2:%3").arg(ssh->user(), ssh->host(), remoteClientDir);

        QProcess proc;
        proc.start(pscpPath, args);
        if (!proc.waitForFinished(300000)) {
            errMsg = QString::fromUtf8("上传超时：") + taskName;
            log(QString::fromUtf8("[错误] ") + errMsg);
            anyFail = true;
        } else if (proc.exitCode() != 0) {
            QString err = QString::fromLocal8Bit(proc.readAllStandardError());
            log(QString::fromUtf8("[警告] 任务 %1 上传部分失败：%2")
                .arg(taskName).arg(err.trimmed().left(200)));
        } else {
            log(QString::fromUtf8("[上传] 任务 %1 完成").arg(taskName));
        }
    }
    return !anyFail;
}

// ──────────────────────────────────────────
//  通用备份逻辑（内部）
// ──────────────────────────────────────────
static void setButtonsEnabled(QList<QPushButton *> btns, bool enabled)
{
    for (auto *b : btns) if (b) b->setEnabled(enabled);
}

void LocalManagerPage::onBackupAll()
{
    m_cachedProjects = AuthManager::instance()->getAllProjects();
    if (m_cachedProjects.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("无项目"),
            QString::fromUtf8("当前账户下没有项目数据，无需备份。"));
        return;
    }

    // 选保存路径
    QString defaultName = QString("CloudPlatform_Backup_ALL_%1.zip")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString zipPath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("选择备份ZIP保存位置"),
        QDir::homePath() + "/" + defaultName,
        "ZIP (*.zip)");
    if (zipPath.isEmpty()) return;

    QList<int> ids;
    for (const auto &p : m_cachedProjects) ids << p.id;

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, false);
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString::fromUtf8("正在备份..."));
    log(QString::fromUtf8("[备份] 开始全量备份，共 %1 个项目").arg(ids.size()));
    QApplication::processEvents();

    // 1. 临时目录
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        QMessageBox::critical(this, QString::fromUtf8("错误"), QString::fromUtf8("无法创建临时目录"));
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        return;
    }
    QString workDir = tmpDir.path();

    m_progressBar->setValue(10);
    QApplication::processEvents();

    // 2. 写元数据JSON
    QString jsonPath = QDir(workDir).filePath("projects_metadata.json");
    exportProjectsToJson(ids, jsonPath);
    m_progressBar->setValue(20);
    QApplication::processEvents();

    // 3. 下载云端文件（可选，如SSH连接则下载）
    if (SshManager::instance()->isConnected()) {
        m_statusLabel->setText(QString::fromUtf8("正在从云端下载文件..."));
        QApplication::processEvents();
        QString filesDir = QDir(workDir).filePath("files");
        QString errMsg;
        downloadProjectFilesFromServer(ids, filesDir, errMsg);
        if (!errMsg.isEmpty()) {
            log(QString::fromUtf8("[警告] 部分文件下载出错：") + errMsg);
        }
    } else {
        log(QString::fromUtf8("[备份] SSH未连接，仅备份项目元数据（不含云端文件）"));
    }
    m_progressBar->setValue(70);
    QApplication::processEvents();

    // 4. 压缩
    m_statusLabel->setText(QString::fromUtf8("正在压缩..."));
    QApplication::processEvents();
    bool ok = compressToZip(workDir, zipPath);
    m_progressBar->setValue(95);
    QApplication::processEvents();

    if (ok) {
        QFileInfo fi(zipPath);
        QString sizeMB = QString("%1 MB").arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 2);
        m_progressBar->setValue(100);
        m_statusLabel->setText(QString::fromUtf8("备份完成！"));
        log(QString::fromUtf8("[备份] ✓ ZIP已生成：%1  大小：%2").arg(zipPath, sizeMB));
        QMessageBox::information(this, QString::fromUtf8("备份成功"),
            QString::fromUtf8("全量备份完成！\n\n文件：%1\n大小：%2").arg(zipPath, sizeMB));
    } else {
        m_statusLabel->setText(QString::fromUtf8("备份失败"));
        QMessageBox::warning(this, QString::fromUtf8("备份失败"),
            QString::fromUtf8("ZIP压缩失败，请查看操作日志。"));
    }

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
    onProjectSelectionChanged(); // 恢复选定按钮状态
}

void LocalManagerPage::onBackupSelected()
{
    auto selected = m_projectListWidget->selectedItems();
    if (selected.isEmpty()) return;

    QList<int> ids;
    QStringList names;
    for (auto *item : selected) {
        ids << item->data(Qt::UserRole).toInt();
        names << item->text();
    }

    QString defaultName = QString("CloudPlatform_Backup_SEL_%1.zip")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString zipPath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("选择备份ZIP保存位置"),
        QDir::homePath() + "/" + defaultName,
        "ZIP (*.zip)");
    if (zipPath.isEmpty()) return;

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, false);
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString::fromUtf8("正在备份选定项目..."));
    log(QString::fromUtf8("[备份] 选定项目备份：") + names.join(", "));
    QApplication::processEvents();

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }
    QString workDir = tmpDir.path();

    // 写JSON
    exportProjectsToJson(ids, QDir(workDir).filePath("projects_metadata.json"));
    m_progressBar->setValue(20);
    QApplication::processEvents();

    // 下载云端文件
    if (SshManager::instance()->isConnected()) {
        m_statusLabel->setText(QString::fromUtf8("正在下载云端文件..."));
        QApplication::processEvents();
        QString filesDir = QDir(workDir).filePath("files");
        QString errMsg;
        downloadProjectFilesFromServer(ids, filesDir, errMsg);
    } else {
        log(QString::fromUtf8("[备份] SSH未连接，仅备份项目元数据"));
    }
    m_progressBar->setValue(70);
    QApplication::processEvents();

    m_statusLabel->setText(QString::fromUtf8("正在压缩..."));
    QApplication::processEvents();
    bool ok = compressToZip(workDir, zipPath);
    m_progressBar->setValue(100);

    if (ok) {
        QFileInfo fi(zipPath);
        QString sizeMB = QString("%1 MB").arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 2);
        m_statusLabel->setText(QString::fromUtf8("备份完成！"));
        log(QString::fromUtf8("[备份] ✓ 选定项目备份完成：%1  大小：%2").arg(zipPath, sizeMB));
        QMessageBox::information(this, QString::fromUtf8("备份成功"),
            QString::fromUtf8("选定项目备份完成！\n\n文件：%1\n大小：%2").arg(zipPath, sizeMB));
    } else {
        m_statusLabel->setText(QString::fromUtf8("备份失败"));
        QMessageBox::warning(this, QString::fromUtf8("备份失败"),
            QString::fromUtf8("ZIP压缩失败，请查看操作日志。"));
    }

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
    onProjectSelectionChanged();
}

// ──────────────────────────────────────────
//  恢复
// ──────────────────────────────────────────
void LocalManagerPage::onRestoreAll()
{
    QString zipPath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择备份ZIP文件"),
        QDir::homePath(),
        "ZIP (*.zip)");
    if (zipPath.isEmpty()) return;

    auto ret = QMessageBox::warning(this,
        QString::fromUtf8("确认恢复"),
        QString::fromUtf8("将从备份文件中恢复所有项目元数据，并将文件上传回云端。\n\n"
                          "⚠ 注意：已存在的同名项目将被跳过（不覆盖）。\n\n"
                          "确定继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, false);
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString::fromUtf8("正在解压..."));
    QApplication::processEvents();

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }

    log(QString::fromUtf8("[恢复] 解压备份：") + zipPath);
    bool ok = extractZip(zipPath, tmpDir.path());
    m_progressBar->setValue(30);
    QApplication::processEvents();

    if (!ok) {
        m_statusLabel->setText(QString::fromUtf8("解压失败"));
        QMessageBox::critical(this, QString::fromUtf8("错误"), QString::fromUtf8("备份ZIP解压失败，请检查文件是否损坏。"));
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }

    // 导入元数据
    QString jsonPath = QDir(tmpDir.path()).filePath("projects_metadata.json");
    if (!QFile::exists(jsonPath)) {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("备份文件中未找到 projects_metadata.json，该备份可能已损坏。"));
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }

    QStringList importLog;
    importProjectsFromJson(jsonPath, importLog);
    for (const auto &l : importLog) log(l);
    m_progressBar->setValue(60);
    QApplication::processEvents();

    // 上传前先本地解压 files/ 目录中的压缩包（云端无法解析压缩包）
    QString filesDir = QDir(tmpDir.path()).filePath("files");
    if (QDir(filesDir).exists()) {
        m_statusLabel->setText(QString::fromUtf8("正在本地解压压缩包..."));
        QApplication::processEvents();
        decompressArchivesInDir(filesDir);
        log(QString::fromUtf8("[恢复] 压缩包本地解压完成"));
    }

    // 上传文件到云端
    if (QDir(filesDir).exists() && SshManager::instance()->isConnected()) {
        m_statusLabel->setText(QString::fromUtf8("正在上传文件到云端..."));
        QApplication::processEvents();
        QString errMsg;
        uploadProjectFilesToServer(filesDir, errMsg);
        if (!errMsg.isEmpty())
            log(QString::fromUtf8("[警告] ") + errMsg);
    } else if (!SshManager::instance()->isConnected()) {
        log(QString::fromUtf8("[恢复] SSH未连接，已恢复元数据，文件未上传云端"));
    }

    m_progressBar->setValue(100);
    m_statusLabel->setText(QString::fromUtf8("恢复完成！"));
    log(QString::fromUtf8("[恢复] ✓ 全量恢复完成"));
    emit backupRestoreCompleted();
    QMessageBox::information(this, QString::fromUtf8("恢复成功"),
        QString::fromUtf8("全量恢复完成！\n\n请刷新项目管理页面查看恢复的项目。"));

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
    onProjectSelectionChanged();
}

void LocalManagerPage::onRestoreSelected()
{
    auto selected = m_projectListWidget->selectedItems();
    if (selected.isEmpty()) return;

    QStringList names;
    for (auto *item : selected) names << item->text();

    QString zipPath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择备份ZIP文件"),
        QDir::homePath(),
        "ZIP (*.zip)");
    if (zipPath.isEmpty()) return;

    auto ret = QMessageBox::warning(this,
        QString::fromUtf8("确认恢复"),
        QString::fromUtf8("将从备份中恢复以下项目（ZIP内匹配的项目编号）：\n\n%1\n\n"
                          "⚠ 已存在的同名项目将被跳过。确定继续？")
            .arg(names.join("\n")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, false);
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString::fromUtf8("正在解压..."));
    QApplication::processEvents();

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }

    bool ok = extractZip(zipPath, tmpDir.path());
    m_progressBar->setValue(30);
    if (!ok) {
        m_statusLabel->setText(QString::fromUtf8("解压失败"));
        QMessageBox::critical(this, QString::fromUtf8("错误"), QString::fromUtf8("解压失败"));
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }

    QString jsonPath = QDir(tmpDir.path()).filePath("projects_metadata.json");
    if (!QFile::exists(jsonPath)) {
        QMessageBox::critical(this, QString::fromUtf8("错误"), QString::fromUtf8("备份文件损坏，缺少元数据JSON"));
        setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
        onProjectSelectionChanged();
        return;
    }

    // 导入（将全量JSON导入，让系统自动跳过已存在项目）
    QStringList importLog;
    importProjectsFromJson(jsonPath, importLog);
    for (const auto &l : importLog) log(l);
    m_progressBar->setValue(70);
    QApplication::processEvents();

    // 上传前先本地解压 files/ 目录中的压缩包（云端无法解析压缩包）
    QString filesDir = QDir(tmpDir.path()).filePath("files");
    if (QDir(filesDir).exists()) {
        m_statusLabel->setText(QString::fromUtf8("正在本地解压压缩包..."));
        QApplication::processEvents();
        decompressArchivesInDir(filesDir);
        log(QString::fromUtf8("[恢复] 压缩包本地解压完成"));
    }

    // 上传选定项目文件
    // 收集选定项目编号
    QStringList selCodes;
    for (auto *item : selected) {
        int pid = item->data(Qt::UserRole).toInt();
        ProjectInfo p = AuthManager::instance()->getProject(pid);
        if (p.id >= 0) selCodes << p.code;
    }

    // 上传选定项目文件到云端（uploadProjectFilesToServer 自动处理 files/ 下所有目录）
    if (QDir(filesDir).exists() && SshManager::instance()->isConnected()) {
        m_statusLabel->setText(QString::fromUtf8("正在上传文件到云端..."));
        QApplication::processEvents();
        QString errMsg;
        uploadProjectFilesToServer(filesDir, errMsg);
    }

    m_progressBar->setValue(100);
    m_statusLabel->setText(QString::fromUtf8("恢复完成！"));
    log(QString::fromUtf8("[恢复] ✓ 选定项目恢复完成"));
    emit backupRestoreCompleted();
    QMessageBox::information(this, QString::fromUtf8("恢复成功"),
        QString::fromUtf8("选定项目恢复完成！\n\n请刷新项目管理页面查看恢复的项目。"));

    setButtonsEnabled({m_backupAllBtn, m_backupSelBtn, m_restoreAllBtn, m_restoreSelBtn}, true);
    onProjectSelectionChanged();
}

// ── 前置声明：safeMergeDirs（在 decompressArchivesInDir 中调用）──
static bool safeMergeDirs(const QString &srcDir, const QString &destDir);

// ──────────────────────────────────────────
//  辅助：递归扫描目录中的压缩包并本地解压
//  恢复备份时，云端无法解析压缩包，需本地解压后再上传
// ──────────────────────────────────────────
void LocalManagerPage::decompressArchivesInDir(const QString &dir)
{
    QDir d(dir);

    // ── 第一步：递归进入所有子目录 ──
    QStringList subDirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subDir : subDirs) {
        decompressArchivesInDir(d.filePath(subDir));
    }

    // ── 第二步：处理当前目录下的压缩包 ──
    QStringList allFiles = d.entryList(QDir::Files);
    for (const QString &fileName : allFiles) {
        QString filePath = d.filePath(fileName);
        QString extractDir;

        // 按扩展名判断压缩包类型（长扩展名优先匹配）
        if (fileName.endsWith(".tar.gz")) {
            extractDir = filePath.left(filePath.length() - 7);
        } else if (fileName.endsWith(".tar.bz2")) {
            extractDir = filePath.left(filePath.length() - 8);
        } else if (fileName.endsWith(".tar.xz")) {
            extractDir = filePath.left(filePath.length() - 7);
        } else if (fileName.endsWith(".tgz")) {
            extractDir = filePath.left(filePath.length() - 4);
        } else if (fileName.endsWith(".tbz2")) {
            extractDir = filePath.left(filePath.length() - 5);
        } else if (fileName.endsWith(".tar")) {
            extractDir = filePath.left(filePath.length() - 4);
        } else if (fileName.endsWith(".zip")) {
            extractDir = filePath.left(filePath.length() - 4);
        } else {
            continue;
        }

        // 解压目标目录已存在则跳过
        if (QDir(extractDir).exists()) {
            log(QString::fromUtf8("[跳过] 解压目标已存在: %1").arg(fileName));
            continue;
        }

        QDir().mkpath(extractDir);

        // 解压
        bool ok = false;
        if (fileName.endsWith(".zip")) {
            ok = extractZip(filePath, extractDir);
        } else {
            ok = extractTar(filePath, extractDir);
        }

        if (ok) {
            log(QString::fromUtf8("[解压] %1 → %2").arg(fileName, extractDir));

            // 删除原压缩包
            QFile::remove(filePath);

            // 将 extractDir 内容合并到父目录，然后删除空壳
            // 使用 safeMergeDirs（内部 QDir::rename），不再依赖 robocopy
            {
                QString parentPath = d.absolutePath();
                if (safeMergeDirs(extractDir, parentPath)) {
                    QDir(extractDir).removeRecursively();
                    log(QString::fromUtf8("[合并] %1 内容已提升到 %2")
                        .arg(fileName, parentPath));
                } else {
                    log(QString::fromUtf8("[警告] 合并 %1 失败")
                        .arg(fileName));
                }
            }
        } else {
            log(QString::fromUtf8("[警告] 解压失败，保留原文件: %1").arg(fileName));
        }
    }
}

// ──────────────────────────────────────────
//  辅助：解压 tar 系列压缩包（使用 Windows tar.exe）
// ──────────────────────────────────────────
bool LocalManagerPage::extractTar(const QString &tarPath, const QString &destDir)
{
    QDir().mkpath(destDir);

    QProcess proc;
    proc.setWorkingDirectory(destDir);
    proc.start("tar.exe", {"-xf", QDir::toNativeSeparators(tarPath)});
    if (!proc.waitForFinished(120000)) {
        log(QString::fromUtf8("[错误] tar 解压超时"));
        return false;
    }
    if (proc.exitCode() != 0) {
        QString err = QString::fromLocal8Bit(proc.readAllStandardError());
        log(QString::fromUtf8("[错误] tar 解压失败：") + err.trimmed());
        return false;
    }
    return true;
}

// ──────────────────────────────────────────
//  辅助：安全合并目录 — 将 srcDir 下所有内容移入 destDir
//  使用 QDir::rename（同盘原子操作），失败则 copy+delete
// ──────────────────────────────────────────
static bool safeMergeDirs(const QString &srcDir, const QString &destDir)
{
    QDir src(srcDir);
    QDir dest(destDir);
    QStringList entries = src.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QString &name : entries) {
        QString srcPath  = src.filePath(name);
        QString destPath = dest.filePath(name);
        if (QFile::rename(srcPath, destPath))
            continue;   // 同盘原子移动，成功
        // rename 失败（跨盘或权限），用 copy + delete
        QFileInfo fi(srcPath);
        if (fi.isDir()) {
            QDir().mkpath(destPath);
            if (!safeMergeDirs(srcPath, destPath))
                return false;
            QDir(srcPath).removeRecursively();
        } else {
            if (!QFile::copy(srcPath, destPath))
                return false;
            QFile::remove(srcPath);
        }
    }
    return true;
}

// ──────────────────────────────────────────
//  辅助：扁平化目录 — 消除 tar/ZIP 归档自带根目录层
//  若 dir 下只有一个子目录且没有文件，循环提升直到稳定
//  使用 safeMergeDirs（QDir::rename），不再依赖 robocopy /MOVE
// ──────────────────────────────────────────
void LocalManagerPage::flattenTaskDir(const QString &dir)
{
    QString current = dir;
    while (true) {
        QDir d(current);
        QStringList subDirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QStringList files   = d.entryList(QDir::Files);
        if (subDirs.size() != 1 || !files.isEmpty())
            break;
        QString tmpDir = d.filePath(subDirs.first() + "_tmp_rename_");
        QString onlySub = d.filePath(subDirs.first());
        // 先 rename 到临时名，再 merge 回当前目录
        if (!QDir().rename(onlySub, tmpDir)) {
            log(QString::fromUtf8("[扁平化] rename 失败，停止处理 %1").arg(subDirs.first()));
            break;
        }
        if (!safeMergeDirs(tmpDir, current)) {
            log(QString::fromUtf8("[扁平化] merge 失败，停止处理"));
            QDir().rename(tmpDir, onlySub);  // 回滚
            break;
        }
        QDir(tmpDir).removeRecursively();
        log(QString::fromUtf8("[扁平化] 消除多余层级 %1 → %2").arg(subDirs.first(), current));
    }
}

// ──────────────────────────────────────────
//  log
// ──────────────────────────────────────────
void LocalManagerPage::log(const QString &msg)
{
    if (!m_logEdit) return;
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logEdit->append(
        QString("<span style='color:#6a9955;'>[%1]</span> %2").arg(ts, msg));
    QScrollBar *sb = m_logEdit->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}
