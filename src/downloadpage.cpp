#include "downloadpage.h"
#include "sshmanager.h"
#include "authmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QProcess>
#include <QStandardPaths>
#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QDebug>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>

DownloadPage::DownloadPage(QWidget *parent)
    : QWidget(parent)
    , m_downloadingCount(0)
    , m_downloadedCount(0)
    , m_downloadFailCount(0)
    , m_isDownloading(false)
    , m_currentDownloadIndex(0)
    , m_permissionsInitialized(false)
{
    initUI();

    // 连接SSH下载信号（只连一次）
    connect(SshManager::instance(), &SshManager::downloadProgress,
            this, &DownloadPage::onDownloadProgress);
    connect(SshManager::instance(), &SshManager::downloadFinished,
            this, &DownloadPage::onDownloadFinished);
}

void DownloadPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 15, 20, 15);
    mainLayout->setSpacing(12);

    // === 顶部路径导航栏 ===
    QHBoxLayout *navRow = new QHBoxLayout();
    navRow->setSpacing(8);

    m_goUpBtn = new QPushButton("返回上级");
    m_goUpBtn->setFixedHeight(28);
    m_goUpBtn->setCursor(Qt::PointingHandCursor);
    connect(m_goUpBtn, &QPushButton::clicked, this, &DownloadPage::onGoUpClicked);
    navRow->addWidget(m_goUpBtn);

    m_pathEdit = new QLineEdit();
    m_pathEdit->setFixedHeight(28);
    m_pathEdit->setPlaceholderText("输入远程路径按回车跳转...");
    connect(m_pathEdit, &QLineEdit::returnPressed,
            this, &DownloadPage::onPathEditReturnPressed);
    navRow->addWidget(m_pathEdit, 1);

    m_goBtn = new QPushButton("前往");
    m_goBtn->setFixedHeight(28);
    m_goBtn->setCursor(Qt::PointingHandCursor);
    connect(m_goBtn, &QPushButton::clicked, this, &DownloadPage::onPathEditReturnPressed);
    navRow->addWidget(m_goBtn);

    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setFixedHeight(28);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DownloadPage::onRefreshClicked);
    navRow->addWidget(m_refreshBtn);

    mainLayout->addLayout(navRow);

    // === 当前路径显示 ===
    m_pathLabel = new QLabel("当前路径: /");
    m_pathLabel->setObjectName("pathLabel");
    mainLayout->addWidget(m_pathLabel);

    // === 文件树 ===
    m_fileTree = new QTreeWidget();
    m_fileTree->setHeaderLabels({"", "文件名", "大小", "修改时间", "操作"});
    // 列宽策略：勾选框和操作用固定宽度，文件名自动拉伸
    m_fileTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_fileTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileTree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_fileTree->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_fileTree->header()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_fileTree->setColumnWidth(0, 40);
    m_fileTree->setColumnWidth(2, 80);
    m_fileTree->setColumnWidth(3, 150);
    m_fileTree->setColumnWidth(4, 150);
    m_fileTree->setMinimumHeight(180);
    m_fileTree->setAlternatingRowColors(true);
    m_fileTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileTree->setRootIsDecorated(false);  // 扁平列表，不需要展开箭头
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 强制设置选中文本为白色（深色背景），修复浅蓝底+白字不可见的问题
    // Windows 系统默认使用浅蓝色选中背景，与白字冲突
    QPalette treePalette = m_fileTree->palette();
    treePalette.setColor(QPalette::Highlight, QColor(0x37, 0x3E, 0x65));       // 深蓝选中背景
    treePalette.setColor(QPalette::HighlightedText, Qt::white);                 // 白色选中文字
    treePalette.setColor(QPalette::Active, QPalette::Highlight, QColor(0x37, 0x3E, 0x65));
    treePalette.setColor(QPalette::Active, QPalette::HighlightedText, Qt::white);
    treePalette.setColor(QPalette::Inactive, QPalette::Highlight, QColor(0x50, 0x58, 0x80));
    treePalette.setColor(QPalette::Inactive, QPalette::HighlightedText, Qt::white);
    m_fileTree->setPalette(treePalette);

    connect(m_fileTree, &QTreeWidget::itemDoubleClicked,
            this, &DownloadPage::onItemDoubleClicked);
    connect(m_fileTree, &QTreeWidget::itemChanged,
            this, &DownloadPage::updateSelectionInfo);
    connect(m_fileTree, &QTreeWidget::customContextMenuRequested,
            this, &DownloadPage::onTreeContextMenu);

    // SshManager信号连接（只连一次）
    connect(SshManager::instance(), &SshManager::remoteDirListed,
            this, &DownloadPage::onRemoteDirListed);

    mainLayout->addWidget(m_fileTree, 1);

    // === 底部操作栏 ===
    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(8);

    m_selectAllBtn = new QPushButton("全选");
    m_selectAllBtn->setMinimumWidth(80);
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &DownloadPage::onSelectAll);
    bottomRow->addWidget(m_selectAllBtn);

    m_deselectAllBtn = new QPushButton("取消全选");
    m_deselectAllBtn->setMinimumWidth(100);
    m_deselectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &DownloadPage::onDeselectAll);
    bottomRow->addWidget(m_deselectAllBtn);

    m_selectionInfo = new QLabel("已选择 0 项");
    m_selectionInfo->setObjectName("selectionInfo");
    bottomRow->addWidget(m_selectionInfo);

    bottomRow->addStretch();

    m_downloadBtn = new QPushButton(QString::fromUtf8("下载选中"));
    m_downloadBtn->setObjectName("downloadBtn");
    m_downloadBtn->setFixedSize(130, 32);
    // 外业人员禁用批量下载
    if (AuthManager::instance()->isFieldWorker()) {
        m_downloadBtn->setEnabled(false);
        m_downloadBtn->setToolTip(QString::fromUtf8("外业人员无下载权限"));
    }
    connect(m_downloadBtn, &QPushButton::clicked, this, &DownloadPage::onDownloadSelected);
    bottomRow->addWidget(m_downloadBtn);

    mainLayout->addLayout(bottomRow);

    // === 进度区域 ===
    QHBoxLayout *progressRow = new QHBoxLayout();
    progressRow->setSpacing(12);

    m_downloadProgressBar = new QProgressBar();
    m_downloadProgressBar->setFixedHeight(22);
    m_downloadProgressBar->setTextVisible(true);
    m_downloadProgressBar->setValue(0);
    m_downloadProgressBar->setFormat("%p%");
    progressRow->addWidget(m_downloadProgressBar, 1);  // stretch=1，占满剩余宽度

    m_progressLabel = new QLabel("就绪");
    m_progressLabel->setObjectName("progressLabel");
    m_progressLabel->setMinimumWidth(180);
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(m_progressLabel);

    mainLayout->addLayout(progressRow);

    m_resultLabel = new QLabel("");
    m_resultLabel->setObjectName("resultLabel");
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setMinimumHeight(24);
    mainLayout->addWidget(m_resultLabel);
}

// ============================================================
//  删除远程文件（仅管理员+内业人员）
// ============================================================
void DownloadPage::onDeleteRemoteFile(const QString &remotePath, const QString &fileName, bool isDir)
{
    bool canDelete = AuthManager::instance()->isCompanyAdmin()
                     || AuthManager::instance()->isOfficeWorker();
    if (!canDelete) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"),
                             QString::fromUtf8("您没有删除云端文件的权限"));
        return;
    }

    // 路径安全检查
    if (!isPathAllowed(remotePath)) {
        QMessageBox::warning(this, QString::fromUtf8("权限不足"),
                             QString::fromUtf8("无权删除此路径下的文件"));
        return;
    }

    // 目录删除额外安全检查：拒绝删除根级目录
    if (isDir) {
        QString cleanPath = SshManager::stripAnsiCodes(remotePath);
        if (cleanPath == "/" || cleanPath == "/home" || cleanPath == "/home/ubuntu" ||
            cleanPath == "/home/ubuntu/" || cleanPath.isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("安全保护"),
                                 QString::fromUtf8("不允许删除受保护的根目录"));
            return;
        }
    }

    QString typeStr = isDir ? QString::fromUtf8("文件夹") : QString::fromUtf8("文件");
    QString confirmMsg = QString::fromUtf8("确定要删除云端%1吗？").arg(typeStr);
    if (isDir) {
        confirmMsg += QString::fromUtf8("\n\n警告：%1 下的所有内容将一并删除！").arg(fileName);
    }
    confirmMsg += QString::fromUtf8("\n\n  %1").arg(fileName);

    QMessageBox::StandardButton btn = QMessageBox::warning(
        this,
        QString::fromUtf8("删除确认"),
        confirmMsg,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (btn != QMessageBox::Yes) return;

    SshManager *ssh = SshManager::instance();
    if (!ssh->isConnected()) {
        QMessageBox::warning(this, QString::fromUtf8("未连接"),
                             QString::fromUtf8("SSH 未连接，无法删除文件"));
        return;
    }

    // 执行删除命令
    bool success = false;
    QString cleanPath = SshManager::stripAnsiCodes(remotePath);
    if (isDir) {
        success = ssh->deleteRemoteDir(cleanPath);
    } else {
        success = ssh->deleteRemoteFile(cleanPath);
    }

    // 延迟刷新列表，等待服务器完成删除
    QTimer::singleShot(500, this, [this]() {
        refreshFileList();
    });

    if (success) {
        m_resultLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
        m_resultLabel->setText(QString::fromUtf8("已删除%1: %2").arg(typeStr).arg(fileName));
    } else {
        m_resultLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
        m_resultLabel->setText(QString::fromUtf8("删除%1失败: %2")
                                   .arg(typeStr).arg(fileName));
    }
}

void DownloadPage::onDownloadFolder(const QString &remoteDirPath)
{
    if (m_isDownloading) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("已有下载任务进行中"));
        return;
    }

    SshManager *ssh = SshManager::instance();
    if (!ssh->isConnected()) {
        QMessageBox::warning(this, QString::fromUtf8("未连接"),
                             QString::fromUtf8("SSH 未连接，无法下载文件夹"));
        return;
    }

    // 选择本地保存目录
    QString folderName = remoteDirPath.mid(remoteDirPath.lastIndexOf('/') + 1);
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                          + "/" + folderName;
    QString localDir = QFileDialog::getExistingDirectory(
        this,
        QString::fromUtf8("选择保存位置"),
        defaultPath
    );
    if (localDir.isEmpty()) return;

    // 启动文件夹下载（异步，通过 pscp -r）
    m_isDownloading = true;
    m_isFolderDownload = true;
    m_folderDownloadSavePath = localDir;
    m_progressLabel->setText(QString::fromUtf8("正在下载文件夹: %1").arg(folderName));
    m_downloadProgressBar->setValue(0);

    bool started = ssh->downloadDir(remoteDirPath, localDir);
    if (!started) {
        m_isDownloading = false;
        m_isFolderDownload = false;
        QMessageBox::warning(this, QString::fromUtf8("错误"),
                             QString::fromUtf8("启动文件夹下载失败"));
        m_progressLabel->setText("就绪");
    }
}

void DownloadPage::refreshPermissions()
{
    // 重新计算权限根路径（登录后才有正确的 AuthManager 状态）
    if (AuthManager::instance()->isSuperAdmin()) {
        m_allowedRootPath = "/home/ubuntu";
    } else {
        QString clientName = AuthManager::instance()->currentUserClient();
        if (clientName.isEmpty()) {
            clientName = AuthManager::instance()->currentUser();
        }
        m_allowedRootPath = "/home/ubuntu/" + clientName;
    }
    m_permissionsInitialized = true;

    // 如果已登录且 SSH 已连接，自动创建公司目录
    if (AuthManager::instance()->isLoggedIn() && SshManager::instance()->isConnected()) {
        QString clientName = AuthManager::instance()->currentUserClient();
        if (!clientName.isEmpty()) {
            QString dirPath = "/home/ubuntu/" + clientName;
            SshManager::instance()->executeCommand("mkdir -p " + dirPath);
        }
    }

    qDebug() << "[DownloadPage] refreshPermissions: m_allowedRootPath =" << m_allowedRootPath;

    // 更新下载按钮权限
    if (AuthManager::instance()->isFieldWorker()) {
        m_downloadBtn->setEnabled(false);
        m_downloadBtn->setToolTip(QString::fromUtf8("外业人员无下载权限"));
    } else if (AuthManager::instance()->canDownload()) {
        m_downloadBtn->setEnabled(true);
        m_downloadBtn->setToolTip("");
    }
}

void DownloadPage::reset()
{
    // 清空文件树
    m_fileTree->clear();

    // 清空路径输入
    if (m_pathEdit) m_pathEdit->clear();
    if (m_pathLabel) m_pathLabel->setText(QString::fromUtf8("当前路径: /"));

    // 重置下载状态
    m_isDownloading = false;
    m_isFolderDownload = false;
    m_downloadingCount = 0;
    m_downloadedCount = 0;
    m_downloadFailCount = 0;
    m_currentDownloadIndex = 0;

    // 清空下载队列
    m_pendingDownloads.clear();
    m_pendingDownloadSizes.clear();
    m_downloadedLocalPaths.clear();
    m_batchSaveDir.clear();
    m_batchZipSavePath.clear();
    m_singleDownloadSavePath.clear();
    m_singleDownloadRemotePath.clear();
    m_folderDownloadSavePath.clear();

    // 重置进度显示
    if (m_downloadProgressBar) m_downloadProgressBar->setValue(0);
    if (m_progressLabel) m_progressLabel->setText(QString::fromUtf8("就绪"));
    if (m_resultLabel) {
        m_resultLabel->setText("");
        m_resultLabel->setStyleSheet("");
    }
    if (m_selectionInfo) m_selectionInfo->setText(QString::fromUtf8("已选择 0 个文件"));

    // 重置权限状态（下次登录会重新初始化）
    m_allowedRootPath.clear();
    m_permissionsInitialized = false;
}

bool DownloadPage::isPathAllowed(const QString &path) const
{
    // 严格路径检查：path 必须等于 m_allowedRootPath 或以 m_allowedRootPath/ 开头
    // 防止 com1 匹配 com10 的前缀问题
    if (path == m_allowedRootPath) return true;
    if (m_allowedRootPath.endsWith('/')) {
        return path.startsWith(m_allowedRootPath);
    } else {
        return path.startsWith(m_allowedRootPath + "/");
    }
}

void DownloadPage::refreshFileList()
{
    if (!m_permissionsInitialized) {
        refreshPermissions();
    }
    QString currentPath = SshManager::instance()->currentRemoteDir();
    if (currentPath.isEmpty() || !isPathAllowed(currentPath)) {
        currentPath = m_allowedRootPath;
    }
    m_pathLabel->setText("当前路径: " + currentPath);
    m_pathEdit->setText(currentPath);
    SshManager::instance()->listRemoteDir(currentPath);
}

void DownloadPage::onRemoteDirListed(const QString &path, const QList<RemoteFileInfo> &fileList)
{
    m_pathLabel->setText("当前路径: " + path);
    m_pathEdit->setText(path);

    // 扁平列表：清空树，重新填充当前目录内容
    m_fileTree->clear();
    m_resultLabel->setText("");

    // 更新"返回上级"按钮状态（不能超出权限根路径）
    m_goUpBtn->setEnabled(isPathAllowed(path) && path != m_allowedRootPath);

    // 获取上传记录用于过滤和显示
    auto uploadRecords = AuthManager::instance()->getUploadRecords(path);

    // 判断当前用户角色，外业人员只能查看自己上传的文件
    bool isFieldWorker = AuthManager::instance()->isFieldWorker();
    QString currentUser = AuthManager::instance()->currentUser();

    // 先添加目录项
    for (const auto &info : fileList) {
        if (!info.isDir) continue;

        QTreeWidgetItem *dirItem = new QTreeWidgetItem();
        dirItem->setCheckState(0, Qt::Unchecked);
        dirItem->setIcon(1, style()->standardIcon(QStyle::SP_DirIcon));
        dirItem->setText(1, info.name);
        dirItem->setData(1, Qt::UserRole, path + "/" + info.name);
        dirItem->setData(1, Qt::UserRole + 1, true);  // 标记为目录
        dirItem->setData(1, Qt::UserRole + 2, (qint64)-1); // 目录无大小
        dirItem->setText(2, "--");
        dirItem->setText(3, info.modTime);
        // 目录项文字加粗，提示可双击进入
        QFont font = dirItem->font(1);
        font.setBold(true);
        dirItem->setFont(1, font);

        m_fileTree->addTopLevelItem(dirItem);

        // === 为目录项添加操作按钮（下载 + 删除） ===
        QWidget *opWidget = new QWidget();
        QHBoxLayout *opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 3, 4, 3);
        opLayout->setSpacing(6);

        // 下载按钮
        QPushButton *dlBtn = new QPushButton(QString::fromUtf8("下载"));
        dlBtn->setFixedSize(65, 28);
        dlBtn->setCursor(Qt::PointingHandCursor);
        if (isFieldWorker) {
            dlBtn->setEnabled(false);
            dlBtn->setToolTip(QString::fromUtf8("外业人员无下载权限"));
        }
        QString remotePath = path + "/" + info.name;
        connect(dlBtn, &QPushButton::clicked, this, [this, remotePath]() {
            onDownloadFolder(remotePath);
        });
        opLayout->addWidget(dlBtn);

        // 删除按钮
        bool canDelete = AuthManager::instance()->isCompanyAdmin()
                         || AuthManager::instance()->isOfficeWorker();
        if (canDelete) {
            QPushButton *delBtn = new QPushButton(QString::fromUtf8("删除"));
            delBtn->setFixedSize(65, 28);
            delBtn->setCursor(Qt::PointingHandCursor);
            QString dirName = info.name;
            connect(delBtn, &QPushButton::clicked, this, [this, remotePath, dirName]() {
                onDeleteRemoteFile(remotePath, dirName, true);
            });
            opLayout->addWidget(delBtn);
        }

        opLayout->addStretch();
        m_fileTree->setItemWidget(dirItem, 4, opWidget);
    }

    // 再添加文件项
    for (const auto &info : fileList) {
        if (info.isDir) continue;

        // 查找该文件的上传记录（通过文件名匹配）
        QString uploader;
        for (const auto &rec : uploadRecords) {
            if (rec.fileName == info.name) {
                uploader = rec.username;
                break;
            }
        }

        // 外业人员：只能看到自己上传的文件（通过数据库记录判断）
        if (isFieldWorker) {
            // 没有上传记录 或 上传人不是自己 → 隐藏
            if (uploader.isEmpty() || uploader != currentUser) {
                continue;
            }
        }

        QTreeWidgetItem *fileItem = new QTreeWidgetItem();
        fileItem->setCheckState(0, Qt::Unchecked);
        fileItem->setIcon(1, style()->standardIcon(QStyle::SP_FileIcon));
        fileItem->setText(1, info.name);
        fileItem->setData(1, Qt::UserRole, path + "/" + info.name);
        fileItem->setData(1, Qt::UserRole + 1, false);  // 标记为文件
        fileItem->setData(1, Qt::UserRole + 2, info.size); // 存储文件大小（需求③）
        fileItem->setText(2, formatFileSize(info.size));
        fileItem->setText(3, info.modTime);

        QWidget *opWidget = new QWidget();
        QHBoxLayout *opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 3, 4, 3);
        opLayout->setSpacing(6);

        QPushButton *dlBtn = new QPushButton(QString::fromUtf8("下载"));
        dlBtn->setFixedSize(65, 28);
        dlBtn->setCursor(Qt::PointingHandCursor);
        // 外业人员不能下载
        if (isFieldWorker) {
            dlBtn->setEnabled(false);
            dlBtn->setToolTip(QString::fromUtf8("外业人员无下载权限"));
        }
        QString remotePath = path + "/" + info.name;
        qint64 fileSize = info.size;
        connect(dlBtn, &QPushButton::clicked, this, [this, remotePath, fileSize]() {
            downloadSingleFile(remotePath, fileSize);
        });
        opLayout->addWidget(dlBtn);

        // 仅管理员和内业人员显示删除按钮
        bool canDelete = AuthManager::instance()->isCompanyAdmin()
                         || AuthManager::instance()->isOfficeWorker();
        if (canDelete) {
            QPushButton *delBtn = new QPushButton(QString::fromUtf8("删除"));
            delBtn->setFixedSize(65, 28);
            delBtn->setCursor(Qt::PointingHandCursor);
            QString fileName = info.name;
            bool itemIsDir = info.isDir;
            connect(delBtn, &QPushButton::clicked, this, [this, remotePath, fileName, itemIsDir]() {
                onDeleteRemoteFile(remotePath, fileName, itemIsDir);
            });
            opLayout->addWidget(delBtn);
        }

        opLayout->addStretch();

        m_fileTree->addTopLevelItem(fileItem);
        m_fileTree->setItemWidget(fileItem, 4, opWidget);
    }

    updateSelectionInfo();
}

void DownloadPage::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item) return;

    bool isDir = item->data(1, Qt::UserRole + 1).toBool();
    QString path = item->data(1, Qt::UserRole).toString();

    if (isDir && !path.isEmpty()) {
        // 双击目录 → 进入目录
        SshManager::instance()->changeRemoteDir(path);
    } else if (!isDir && !path.isEmpty()) {
        // 双击文件 → 读取大小后下载
        qint64 fileSize = item->data(1, Qt::UserRole + 2).toLongLong();
        downloadSingleFile(path, fileSize);
    }
}

void DownloadPage::onGoUpClicked()
{
    QString currentPath = SshManager::instance()->currentRemoteDir();
    if (currentPath.isEmpty() || currentPath == m_allowedRootPath) return;

    // 去掉末尾的 /
    if (currentPath.endsWith('/')) {
        currentPath.chop(1);
    }

    int lastSlash = currentPath.lastIndexOf('/');
    QString parentPath = (lastSlash <= 0) ? "/" : currentPath.left(lastSlash);

    // 不能超出权限根路径
    if (!isPathAllowed(parentPath)) {
        parentPath = m_allowedRootPath;
    }

    SshManager::instance()->changeRemoteDir(parentPath);
}

void DownloadPage::onPathEditReturnPressed()
{
    QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) return;

    // 确保路径以 / 开头
    if (!path.startsWith('/')) {
        path = "/" + path;
    }

    // 非超级管理员：路径必须在权限根路径下（严格匹配）
    if (!isPathAllowed(path)) {
        m_resultLabel->setStyleSheet("color: #ff0000;");
        m_resultLabel->setText(QString::fromUtf8("无权访问该路径，只能浏览: %1").arg(m_allowedRootPath));
        m_pathEdit->setText(m_allowedRootPath);
        return;
    }

    SshManager::instance()->changeRemoteDir(path);
}

void DownloadPage::onRefreshClicked()
{
    refreshFileList();
    m_resultLabel->setText("");
    m_downloadProgressBar->setValue(0);
    m_progressLabel->setText("就绪");
}

void DownloadPage::onDownloadSingle()
{
    QTreeWidgetItem *item = m_fileTree->currentItem();
    if (!item) return;
    QString key = item->data(1, Qt::UserRole).toString();
    if (!key.isEmpty() && !item->data(1, Qt::UserRole + 1).toBool()) {
        qint64 fileSize = item->data(1, Qt::UserRole + 2).toLongLong();
        downloadSingleFile(key, fileSize);
    }
}

// ============================================================
// 需求③：单文件下载前提示文件大小
// ============================================================
void DownloadPage::downloadSingleFile(const QString &remotePath, qint64 fileSize)
{
    // 最后一层防护：去除可能的 ANSI 颜色转义码
    QString cleanPath = SshManager::stripAnsiCodes(remotePath);

    // 提取文件名
    QString fileName = cleanPath.mid(cleanPath.lastIndexOf('/') + 1);

    // 构建提示文字：显示文件名 + 大小
    QString sizeStr = (fileSize >= 0) ? formatFileSize(fileSize) : QString::fromUtf8("未知");
    QString dialogTitle = QString::fromUtf8("下载文件  [大小: %1]").arg(sizeStr);

    QString savePath = QFileDialog::getSaveFileName(this, dialogTitle,
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + fileName);

    if (savePath.isEmpty()) return;

    // 保存到成员变量，用于下载完成后记录数据库
    m_singleDownloadSavePath = savePath;
    m_singleDownloadRemotePath = cleanPath;

    m_progressLabel->setText(QString("正在下载: %1  (%2)").arg(fileName).arg(sizeStr));
    m_downloadProgressBar->setValue(0);
    m_isDownloading = true;

    qDebug() << "[Download] 开始下载:" << cleanPath << "->" << savePath
             << "大小:" << sizeStr;

    SshManager::instance()->downloadFile(cleanPath, savePath);
}

void DownloadPage::onDownloadProgress(const QString &key, qint64 received, qint64 total)
{
    QString fileName = key.mid(key.lastIndexOf('/') + 1);

    if (total > 0) {
        int percent = static_cast<int>(received * 100 / total);
        m_downloadProgressBar->setValue(percent);
        m_progressLabel->setText(QString("正在下载: %1 (%2%)")
                                 .arg(fileName).arg(percent));
    }
}

// ============================================================
// 需求②：多选下载 → 提示文件清单和总大小 → 打包 ZIP
// 需求③：批量下载前显示各文件大小及合计
// ============================================================
void DownloadPage::onDownloadSelected()
{
    QStringList keys;
    QList<qint64> sizes;
    int topLevelCount = m_fileTree->topLevelItemCount();
    for (int i = 0; i < topLevelCount; ++i) {
        QTreeWidgetItem *item = m_fileTree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) {
            QString key = item->data(1, Qt::UserRole).toString();
            if (!key.isEmpty() && !item->data(1, Qt::UserRole + 1).toBool()) {
                keys.append(key);
                sizes.append(item->data(1, Qt::UserRole + 2).toLongLong());
            }
        }
    }

    if (keys.isEmpty()) {
        QMessageBox::information(this, "提示", "请先勾选要下载的文件");
        return;
    }

    if (keys.size() == 1) {
        downloadSingleFile(keys.first(), sizes.first());
    } else {
        // ---- 需求③：弹出文件清单 + 总大小确认框 ----
        qint64 totalSize = 0;
        for (qint64 s : sizes) {
            if (s > 0) totalSize += s;
        }

        QString fileListStr;
        for (int i = 0; i < keys.size(); ++i) {
            QString name = keys[i].mid(keys[i].lastIndexOf('/') + 1);
            QString sizeStr = (sizes[i] >= 0) ? formatFileSize(sizes[i]) : QString::fromUtf8("未知");
            fileListStr += QString::fromUtf8("  • %1  (%2)\n").arg(name).arg(sizeStr);
        }

        QString confirmMsg = QString::fromUtf8(
            "共选中 %1 个文件，合计大小：%2\n\n"
            "%3\n"
            "将打包为 ZIP 压缩包，是否继续？"
        ).arg(keys.size())
         .arg(formatFileSize(totalSize))
         .arg(fileListStr);

        QMessageBox::StandardButton btn = QMessageBox::question(
            this,
            QString::fromUtf8("批量下载确认"),
            confirmMsg,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );
        if (btn != QMessageBox::Yes) return;

        // ---- 需求②：让用户选择 ZIP 保存路径 ----
        QString defaultZipName = QString("CloudPlatform_Download_%1.zip")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QString zipSavePath = QFileDialog::getSaveFileName(
            this,
            QString::fromUtf8("保存 ZIP 压缩包"),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + defaultZipName,
            QString::fromUtf8("ZIP 压缩包 (*.zip)")
        );
        if (zipSavePath.isEmpty()) return;

        // 先下载到临时目录，完成后打包
        m_batchZipSavePath = zipSavePath;

        // 在系统临时目录下创建唯一子目录
        QString tmpBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tmpDir = tmpBase + "/CloudPlatform_DL_"
            + QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
        QDir().mkpath(tmpDir);
        m_batchSaveDir = tmpDir;

        // 开始批量下载
        m_pendingDownloads = keys;
        m_pendingDownloadSizes = sizes;
        m_currentDownloadIndex = 0;
        m_downloadedCount = 0;
        m_downloadFailCount = 0;
        m_downloadedLocalPaths.clear();
        m_isDownloading = true;

        m_downloadProgressBar->setValue(0);
        m_resultLabel->setText("");

        m_progressLabel->setText(QString::fromUtf8("准备下载 %1 个文件，合计 %2 ...")
                                 .arg(keys.size()).arg(formatFileSize(totalSize)));

        // 开始第一个下载
        downloadNextFile();
    }
}

void DownloadPage::downloadNextFile()
{
    if (m_currentDownloadIndex >= m_pendingDownloads.size()) {
        // 全部完成
        m_isDownloading = false;
        m_downloadProgressBar->setValue(100);

        if (!m_batchZipSavePath.isEmpty()) {
            // 需求②：打包为 ZIP
            m_progressLabel->setText(QString::fromUtf8("正在打包 ZIP..."));
            packageToZip(m_downloadedLocalPaths, m_batchZipSavePath);
            m_batchZipSavePath.clear();
        } else {
            if (m_downloadFailCount == 0) {
                m_resultLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
                m_resultLabel->setText(QString("下载完成！成功 %1 个文件").arg(m_downloadedCount));
            } else {
                m_resultLabel->setStyleSheet("color: #e67e22; font-weight: bold;");
                m_resultLabel->setText(QString("下载完成：成功 %1 个，失败 %2 个")
                                      .arg(m_downloadedCount).arg(m_downloadFailCount));
            }
            m_progressLabel->setText(QString("总计: %1 个文件").arg(m_pendingDownloads.size()));
        }
        return;
    }

    QString remotePath = m_pendingDownloads[m_currentDownloadIndex];
    QString fileName = remotePath.mid(remotePath.lastIndexOf('/') + 1);
    QString localPath = m_batchSaveDir + "/" + fileName;

    m_progressLabel->setText(QString("正在下载: %1 (%2/%3)")
                             .arg(fileName)
                             .arg(m_currentDownloadIndex + 1)
                             .arg(m_pendingDownloads.size()));

    qDebug() << "[Download] 批量下载 (" << m_currentDownloadIndex + 1 << "/"
             << m_pendingDownloads.size() << "):" << remotePath;

    SshManager::instance()->downloadFile(remotePath, localPath);
}

void DownloadPage::onDownloadFinished(const QString &key, bool success, const QString &message)
{
    Q_UNUSED(key);

    // 文件夹下载完成处理
    if (m_isFolderDownload) {
        m_isFolderDownload = false;
        m_isDownloading = false;
        if (success) {
            m_resultLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
            m_resultLabel->setText(QString::fromUtf8("文件夹下载完成: %1").arg(key));
            m_progressLabel->setText(QString::fromUtf8("保存至: %1").arg(m_folderDownloadSavePath));
        } else {
            m_resultLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
            m_resultLabel->setText(QString::fromUtf8("文件夹下载失败: %1 - %2").arg(key).arg(message));
            m_progressLabel->setText("就绪");
        }
        return;
    }

    if (m_pendingDownloads.contains(key)) {
        // 批量下载
        if (success) {
            m_downloadedCount++;
            QString fileName = key.mid(key.lastIndexOf('/') + 1);
            QString localPath = m_batchSaveDir + "/" + fileName;
            m_downloadedLocalPaths.append(localPath); // 记录本地路径供打包使用

            // 记录下载到数据库
            QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            AuthManager::instance()->recordDownload(
                AuthManager::instance()->currentUser(),
                key, fileName,
                localPath, 0, now);
        } else {
            m_downloadFailCount++;
            qDebug() << "下载失败:" << key << message;
        }

        // 更新总进度
        int done = m_downloadedCount + m_downloadFailCount;
        m_downloadProgressBar->setValue(static_cast<int>(done * 100.0 / m_pendingDownloads.size()));

        // 继续下一个
        m_currentDownloadIndex++;
        if (m_currentDownloadIndex < m_pendingDownloads.size()) {
            downloadNextFile();
        } else {
            // 全部文件下载完成，触发打包或结束
            m_isDownloading = false;
            m_downloadProgressBar->setValue(100);

            if (!m_batchZipSavePath.isEmpty()) {
                // 需求②：打包为 ZIP
                m_progressLabel->setText(QString::fromUtf8("正在打包 ZIP..."));
                packageToZip(m_downloadedLocalPaths, m_batchZipSavePath);
                m_batchZipSavePath.clear();
            } else {
                if (m_downloadFailCount == 0) {
                    m_resultLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
                    m_resultLabel->setText(QString("下载完成！成功 %1 个文件").arg(m_downloadedCount));
                } else {
                    m_resultLabel->setStyleSheet("color: #e67e22; font-weight: bold;");
                    m_resultLabel->setText(QString("下载完成：成功 %1 个，失败 %2 个")
                                          .arg(m_downloadedCount).arg(m_downloadFailCount));
                }
                m_progressLabel->setText(QString("总计: %1 个文件").arg(m_pendingDownloads.size()));
            }
        }
    } else {
        // 单个下载
        m_isDownloading = false;

        QString fileName = m_singleDownloadRemotePath.mid(m_singleDownloadRemotePath.lastIndexOf('/') + 1);

        if (success) {
            m_resultLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
            m_resultLabel->setText(QString::fromUtf8("下载完成: ") + fileName);
            // 记录下载到数据库
            QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            AuthManager::instance()->recordDownload(
                AuthManager::instance()->currentUser(),
                m_singleDownloadRemotePath, fileName,
                m_singleDownloadSavePath, 0, now);
        } else {
            m_resultLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
            m_resultLabel->setText("下载失败: " + fileName + " - " + message);
        }

        m_progressLabel->setText("就绪");
    }
}

// ============================================================
// 需求②：打包为 ZIP（调用 PowerShell Compress-Archive）
// ============================================================
void DownloadPage::packageToZip(const QStringList &localFiles, const QString &zipSavePath)
{
    if (localFiles.isEmpty()) {
        m_resultLabel->setStyleSheet("color: #e67e22; font-weight: bold;");
        m_resultLabel->setText(QString::fromUtf8("没有可打包的文件（全部下载失败）"));
        m_progressLabel->setText("就绪");
        return;
    }

    // 构建文件列表字符串（PowerShell 数组格式）
    // 例如: @("C:\tmp\a.txt","C:\tmp\b.shp")
    QStringList quotedPaths;
    for (const QString &p : localFiles) {
        quotedPaths.append("\"" + QDir::toNativeSeparators(p) + "\"");
    }
    QString psFileList = "@(" + quotedPaths.join(",") + ")";
    QString psZipPath = QDir::toNativeSeparators(zipSavePath);

    // 如果目标 ZIP 已存在先删除（PowerShell Compress-Archive 在目标存在时默认报错）
    QString psScript = QString(
        "$files = %1; "
        "if (Test-Path '%2') { Remove-Item '%2' -Force }; "
        "Compress-Archive -Path $files -DestinationPath '%2'"
    ).arg(psFileList).arg(psZipPath);

    qDebug() << "[ZIP] 执行打包脚本:" << psScript;

    QProcess ps;
    ps.setProgram("powershell.exe");
    ps.setArguments(QStringList() << "-NoProfile" << "-NonInteractive" << "-Command" << psScript);
    ps.start();

    if (!ps.waitForFinished(60000)) {
        // 超时
        ps.kill();
        m_resultLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
        m_resultLabel->setText(QString::fromUtf8("ZIP 打包超时，请手动压缩文件夹: ") + m_batchSaveDir);
        m_progressLabel->setText("就绪");
        return;
    }

    int exitCode = ps.exitCode();
    if (exitCode == 0 && QFile::exists(zipSavePath)) {
        // 打包成功
        QFileInfo fi(zipSavePath);
        m_resultLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
        m_resultLabel->setText(QString::fromUtf8("ZIP 打包完成！共 %1 个文件  →  %2  (%3)")
                               .arg(localFiles.size())
                               .arg(fi.fileName())
                               .arg(formatFileSize(fi.size())));
        m_progressLabel->setText(QString::fromUtf8("打包完成: %1").arg(fi.fileName()));

        // 清理临时目录
        QDir(m_batchSaveDir).removeRecursively();
        m_batchSaveDir.clear();

        qDebug() << "[ZIP] 打包成功:" << zipSavePath;
    } else {
        QString errOutput = QString::fromLocal8Bit(ps.readAllStandardError());
        m_resultLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
        m_resultLabel->setText(QString::fromUtf8("ZIP 打包失败（退出码 %1），文件在: %2")
                               .arg(exitCode).arg(m_batchSaveDir));
        m_progressLabel->setText("就绪");
        qDebug() << "[ZIP] 打包失败:" << errOutput;
    }
}

void DownloadPage::onSelectAll()
{
    int count = m_fileTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = m_fileTree->topLevelItem(i);
        if (!item->data(1, Qt::UserRole + 1).toBool()) {
            item->setCheckState(0, Qt::Checked);
        }
    }
    updateSelectionInfo();
}

void DownloadPage::onDeselectAll()
{
    int count = m_fileTree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        m_fileTree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    }
    updateSelectionInfo();
}

void DownloadPage::updateSelectionInfo()
{
    int selected = 0;
    qint64 totalSize = 0;
    int topLevelCount = m_fileTree->topLevelItemCount();
    for (int i = 0; i < topLevelCount; ++i) {
        QTreeWidgetItem *item = m_fileTree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) {
            selected++;
            qint64 sz = item->data(1, Qt::UserRole + 2).toLongLong();
            if (sz > 0) totalSize += sz;
        }
    }

    if (selected > 0 && totalSize > 0) {
        m_selectionInfo->setText(QString::fromUtf8("已选择 %1 项  合计 %2")
                                 .arg(selected).arg(formatFileSize(totalSize)));
    } else {
        m_selectionInfo->setText(QString("已选择 %1 项").arg(selected));
    }
}

void DownloadPage::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_fileTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QString key = item->data(1, Qt::UserRole).toString();
    qint64 fileSize = item->data(1, Qt::UserRole + 2).toLongLong();
    bool isDir = item->data(1, Qt::UserRole + 1).toBool();
    QString fileName = item->text(1);

    if (!isDir) {
        QAction *downloadAction = menu.addAction("下载此文件");
        connect(downloadAction, &QAction::triggered, this, [this, key, fileSize]() {
            downloadSingleFile(key, fileSize);
        });
    }

    // 仅管理员和内业人员可删除（文件和目录均可）
    bool canDelete = AuthManager::instance()->isCompanyAdmin()
                     || AuthManager::instance()->isOfficeWorker();
    if (canDelete) {
        if (!menu.isEmpty()) menu.addSeparator();
        QString actionText = isDir ? QString::fromUtf8("删除此文件夹") : QString::fromUtf8("删除此文件");
        QAction *deleteAction = menu.addAction(actionText);
        deleteAction->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
        connect(deleteAction, &QAction::triggered, this, [this, key, fileName, isDir]() {
            onDeleteRemoteFile(key, fileName, isDir);
        });
    }

    menu.exec(m_fileTree->mapToGlobal(pos));
}

QString DownloadPage::formatFileSize(qint64 bytes)
{
    if (bytes < 0) bytes = 0;
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}
