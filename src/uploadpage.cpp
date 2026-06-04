#include "uploadpage.h"
#include "sshmanager.h"
#include "authmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMimeData>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QApplication>
#include <QStandardPaths>
#include <QDebug>
#include <QLabel>
#include <QComboBox>
#include <QFile>

UploadPage::UploadPage(QWidget *parent)
    : QWidget(parent)
    , m_listModel(new QStandardItemModel(this))
    , m_cardModel(new QStandardItemModel(this))
    , m_isCardView(false)
    , m_isUploading(false)
    , m_currentUploadIndex(0)
    , m_successCount(0)
    , m_failCount(0)
    , m_concurrentCount(0)
{
    setAcceptDrops(true);

    // 隐藏的三级目录输入控件（用于 buildRemotePath）
    m_customerEdit = new QLineEdit(this);
    m_customerEdit->setVisible(false);
    m_projectEdit   = new QLineEdit(this);
    m_projectEdit->setVisible(false);
    m_taskEdit      = new QLineEdit(this);
    m_taskEdit->setVisible(false);

    initUI();

    // 项目下拉框切换时刷新任务下拉框
    connect(m_projectCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        Q_UNUSED(index);
        loadTaskCombo();
    });

    connect(SshManager::instance(), &SshManager::uploadProgress, this, &UploadPage::onUploadProgress);
    connect(SshManager::instance(), &SshManager::uploadFinished, this, &UploadPage::onUploadFinished);
}

void UploadPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 12, 16, 12);
    mainLayout->setSpacing(10);

    // === 项目/任务选择区（自动关联登录账户） ===
    QWidget *pathWidget = new QWidget();
    pathWidget->setObjectName("pathWidget");
    pathWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout *pathOuterLayout = new QVBoxLayout(pathWidget);
    pathOuterLayout->setContentsMargins(14, 10, 14, 10);
    pathOuterLayout->setSpacing(8);

    QLabel *pathTitleLabel = new QLabel(QString::fromUtf8("上传目标（自动关联当前账户）"));
    pathTitleLabel->setObjectName("pathTitleLabel");
    pathTitleLabel->setStyleSheet("font-weight: bold; color: #373E65; font-size: 14px; background: transparent;");
    pathOuterLayout->addWidget(pathTitleLabel);

    QGridLayout *pathGrid = new QGridLayout();
    pathGrid->setSpacing(10);
    pathGrid->setColumnStretch(0, 0);
    pathGrid->setColumnStretch(1, 1);
    pathGrid->setColumnMinimumWidth(0, 90);

    // 第0行：项目选择
    QLabel *projectLabel = new QLabel(QString::fromUtf8("项目:"));
    projectLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    projectLabel->setStyleSheet("font-size: 13px; color: #000000; background: transparent;");
    m_projectCombo = new QComboBox();
    m_projectCombo->setMinimumHeight(26);
    m_projectCombo->setStyleSheet("font-size: 13px;");
    m_projectCombo->addItem(QString::fromUtf8("-- 请选择项目 --"), -1);
    pathGrid->addWidget(projectLabel, 0, 0);
    pathGrid->addWidget(m_projectCombo, 0, 1);

    // 第1行：测量任务选择
    QLabel *taskLabel = new QLabel(QString::fromUtf8("测量任务:"));
    taskLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    taskLabel->setStyleSheet("font-size: 13px; color: #000000; background: transparent;");
    m_taskCombo = new QComboBox();
    m_taskCombo->setMinimumHeight(26);
    m_taskCombo->setStyleSheet("font-size: 13px;");
    m_taskCombo->addItem(QString::fromUtf8("-- 请选择测量任务 --"), -1);
    pathGrid->addWidget(taskLabel, 1, 0);
    pathGrid->addWidget(m_taskCombo, 1, 1);

    pathOuterLayout->addLayout(pathGrid);
    mainLayout->addWidget(pathWidget);

    // === 拖拽区域 ===
    m_dropLabel = new QLabel();
    m_dropLabel->setObjectName("dropLabel");
    m_dropLabel->setAlignment(Qt::AlignCenter);
    m_dropLabel->setWordWrap(true);
    m_dropLabel->setMinimumHeight(60);
    m_dropLabel->setText(QString::fromUtf8("将文件拖拽到此处\n或点击下方按钮选择文件"));
    m_dropLabel->setAcceptDrops(true);
    m_dropLabel->installEventFilter(this);
    mainLayout->addWidget(m_dropLabel);

    // === 文件列表 ===
    m_fileListView = new QListView();
    m_fileListView->setModel(m_listModel);
    m_fileListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileListView->setMinimumHeight(150);
    m_fileListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 让子控件的拖拽事件向上转发到 UploadPage 处理
    m_fileListView->setAcceptDrops(true);
    m_fileListView->installEventFilter(this);
    mainLayout->addWidget(m_fileListView, 1);

    // === 文件计数标签 ===
    m_countLabel = new QLabel(QString::fromUtf8("共 0 个文件"));
    mainLayout->addWidget(m_countLabel);

    // === 按钮区 ===
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(QString::fromUtf8("添加文件"));
    m_removeBtn = new QPushButton(QString::fromUtf8("移除选中"));
    m_clearBtn = new QPushButton(QString::fromUtf8("清空列表"));
    m_toggleViewBtn = new QPushButton(QString::fromUtf8("切换视图"));
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addWidget(m_toggleViewBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // === 总进度条 ===
    m_totalProgressBar = new QProgressBar();
    m_totalProgressBar->setMinimum(0);
    m_totalProgressBar->setMaximum(100);
    m_totalProgressBar->setValue(0);
    m_totalProgressBar->setMinimumHeight(16);
    m_totalProgressBar->setMaximumHeight(18);
    mainLayout->addWidget(m_totalProgressBar);

    // === 进度与结果标签（水平排列，更紧凑）===
    QHBoxLayout *statusLayout = new QHBoxLayout();
    statusLayout->setSpacing(8);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    m_progressLabel = new QLabel(QString::fromUtf8("就绪"));
    m_resultLabel = new QLabel("");
    statusLayout->addWidget(m_progressLabel);
    statusLayout->addWidget(m_resultLabel);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    // === 上传按钮 ===
    m_uploadBtn = new QPushButton(QString::fromUtf8("开始上传"));
    m_uploadBtn->setObjectName("uploadBtn");
    m_uploadBtn->setFixedHeight(34);
    mainLayout->addWidget(m_uploadBtn);

    // === 信号连接 ===
    connect(m_addBtn,       &QPushButton::clicked, this, &UploadPage::onAddFiles);
    connect(m_removeBtn,    &QPushButton::clicked, this, &UploadPage::onRemoveFile);
    connect(m_clearBtn,     &QPushButton::clicked, this, &UploadPage::onClearFiles);
    connect(m_toggleViewBtn, &QPushButton::clicked, this, &UploadPage::onToggleView);
    connect(m_uploadBtn,    &QPushButton::clicked, this, &UploadPage::onUploadClicked);
}

void UploadPage::addFileToList(const QString &filePath)
{
    QFileInfo fi(filePath);
    UploadFileInfo info;
    info.filePath     = filePath;
    info.fileName     = fi.fileName();
    info.fileSize     = fi.size();
    info.status       = "waiting";
    info.errorMsg     = "";
    info.uploadedBytes = 0;
    m_fileInfoList.append(info);

    QStandardItem *item = new QStandardItem(info.fileName);
    item->setToolTip(filePath);
    m_listModel->appendRow(item);

    if (m_countLabel) {
        m_countLabel->setText(QString::fromUtf8("共 %1 个文件，%2")
            .arg(m_fileInfoList.size()).arg(formatFileSize(getTotalSize())));
    }
}

qint64 UploadPage::getTotalSize() const
{
    qint64 total = 0;
    for (const auto &info : m_fileInfoList) total += info.fileSize;
    return total;
}

void UploadPage::onAddFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this, QString::fromUtf8("选择文件"));
    for (const QString &f : files) addFileToList(f);
}

void UploadPage::onRemoveFile()
{
    QModelIndexList idx = m_fileListView->selectionModel()->selectedIndexes();
    QList<int> rows;
    for (const auto &i : idx) rows.append(i.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) {
        if (r >= 0 && r < m_fileInfoList.size()) m_fileInfoList.removeAt(r);
    }
    // 重建 model
    m_listModel->clear();
    for (const auto &info : m_fileInfoList) {
        QStandardItem *item = new QStandardItem(info.fileName);
        m_listModel->appendRow(item);
    }
    if (m_countLabel) {
        m_countLabel->setText(QString::fromUtf8("共 %1 个文件，%2")
            .arg(m_fileInfoList.size()).arg(formatFileSize(getTotalSize())));
    }
}

void UploadPage::onClearFiles()
{
    m_fileInfoList.clear();
    m_listModel->clear();
    if (m_countLabel) m_countLabel->setText(QString::fromUtf8("共 0 个文件"));
}

void UploadPage::reset()
{
    // 清空文件列表
    m_fileInfoList.clear();
    m_listModel->clear();
    m_cardModel->clear();
    if (m_countLabel) m_countLabel->setText(QString::fromUtf8("共 0 个文件"));

    // 重置上传状态
    m_isUploading = false;
    m_currentUploadIndex = 0;
    m_successCount = 0;
    m_failCount = 0;
    m_concurrentCount = 0;
    if (m_uploadBtn) m_uploadBtn->setText(QString::fromUtf8("开始上传"));
    if (m_totalProgressBar) m_totalProgressBar->setValue(0);
    if (m_progressLabel) m_progressLabel->setText(QString::fromUtf8("就绪"));
    if (m_resultLabel) {
        m_resultLabel->setText("");
        m_resultLabel->setStyleSheet("");
    }

    // 重置三级目录显示
    if (m_customerEdit) m_customerEdit->clear();
    if (m_projectEdit) m_projectEdit->clear();
    if (m_taskEdit) m_taskEdit->clear();

    // 重置项目/任务下拉框
    if (m_projectCombo) m_projectCombo->setCurrentIndex(-1);
    if (m_taskCombo) {
        m_taskCombo->clear();
        m_taskCombo->addItem(QString::fromUtf8("请先选择项目"), -1);
    }

    // 清空数据映射
    m_projectNameMap.clear();

    // 释放临时目录
    m_tempDir.reset();
}

void UploadPage::onUploadClicked()
{
    if (m_isUploading) {
        m_isUploading = false;
        m_uploadBtn->setText(QString::fromUtf8("开始上传"));
        m_progressLabel->setText(QString::fromUtf8("上传已取消"));
        return;
    }
    if (m_fileInfoList.isEmpty()) {
        m_resultLabel->setStyleSheet("color: #ff0000;");
        m_resultLabel->setText(QString::fromUtf8("请先添加文件"));
        return;
    }
    if (!SshManager::instance()->isConnected()) {
        m_resultLabel->setStyleSheet("color: #ff0000;");
        m_resultLabel->setText(QString::fromUtf8("请先连接SSH服务器"));
        return;
    }
    // 校验项目/任务选择
    if (m_projectCombo->currentIndex() < 0 || m_projectCombo->currentData().toInt() < 0) {
        m_resultLabel->setStyleSheet("color: #ff0000;");
        m_resultLabel->setText(QString::fromUtf8("请选择项目"));
        m_projectCombo->setFocus();
        return;
    }
    if (m_taskCombo->currentIndex() < 0 || m_taskCombo->currentData().toInt() < 0) {
        m_resultLabel->setStyleSheet("color: #ff0000;");
        m_resultLabel->setText(QString::fromUtf8("请选择测量任务"));
        m_taskCombo->setFocus();
        return;
    }

    // 自动填充隐藏的三级目录字段
    QString clientName = AuthManager::instance()->currentUserClient();
    if (clientName.isEmpty()) {
        clientName = AuthManager::instance()->currentUser();
    }
    m_customerEdit->setText(clientName);
    // 使用纯项目名（不含 [项目代码] 后缀）构建远程路径
    int projectId = m_projectCombo->currentData().toInt();
    QString projectName = m_projectNameMap.value(projectId, m_projectCombo->currentText());
    m_projectEdit->setText(projectName);
    m_taskEdit->setText(m_taskCombo->currentText());

    // 自动生成测量信息txt并加入上传队列
    generateAndAddMeasurementInfo();

    m_isUploading = true;
    m_successCount = 0;
    m_failCount = 0;
    m_concurrentCount = 0;
    m_currentUploadIndex = 0;
    m_totalProgressBar->setValue(0);
    m_progressLabel->setText(QString::fromUtf8("准备上传..."));
    m_resultLabel->setText("");
    m_resultLabel->setStyleSheet("");
    m_uploadBtn->setText(QString::fromUtf8("取消上传"));
    m_addBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);
    for (int i = 0; i < MAX_CONCURRENT && i < m_fileInfoList.size(); ++i) {
        startNextUpload();
    }
}

void UploadPage::startNextUpload()
{
    if (m_currentUploadIndex >= m_fileInfoList.size()) return;
    UploadFileInfo &info = m_fileInfoList[m_currentUploadIndex];
    info.status = "uploading";
    updateFileStatus(m_currentUploadIndex, "uploading");

    // 构建三级分类远程路径
    QString remotePath = buildRemotePath() + "/" + info.fileName;

    m_concurrentCount++;
    m_currentUploadIndex++;

    qDebug() << "[Upload] 上传文件 (" << m_currentUploadIndex << "/"
             << m_fileInfoList.size() << "):" << remotePath;
    SshManager::instance()->uploadFile(info.filePath, remotePath);
    m_progressLabel->setText(QString::fromUtf8("正在上传: %1 (%2/%3)")
        .arg(info.fileName)
        .arg(m_successCount + m_failCount + 1)
        .arg(m_fileInfoList.size()));
}

QString UploadPage::buildRemotePath()
{
    QStringList parts;
    parts << "/home/ubuntu";
    QString c = m_customerEdit->text().trimmed();
    QString p = m_projectEdit->text().trimmed();
    QString t = m_taskEdit->text().trimmed();
    if (!c.isEmpty()) parts << c;
    if (!p.isEmpty()) parts << p;
    if (!t.isEmpty()) parts << t;

    return parts.join("/");
}

void UploadPage::onUploadProgress(const QString &key, qint64 sent, qint64 total)
{
    Q_UNUSED(key);
    Q_UNUSED(sent);
    Q_UNUSED(total);
}

void UploadPage::onUploadFinished(const QString &key, bool success, const QString &message)
{
    Q_UNUSED(key);
    m_concurrentCount--;
    int row = m_currentUploadIndex - 1;
    if (row >= 0 && row < m_fileInfoList.size()) {
        m_fileInfoList[row].status = success ? "success" : "failed";
        m_fileInfoList[row].errorMsg = success ? "" : message;
        updateFileStatus(row, m_fileInfoList[row].status, m_fileInfoList[row].errorMsg);
    }
    if (success) {
        m_successCount++;
        // 记录上传到数据库
        QString remotePath = buildRemotePath();
        QString fileName = m_fileInfoList[row].fileName;
        QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        AuthManager::instance()->recordUpload(
            AuthManager::instance()->currentUser(), remotePath, fileName, now);
    } else {
        m_failCount++;
    }
    updateTotalProgress();

    if (m_isUploading && m_currentUploadIndex < m_fileInfoList.size()) {
        startNextUpload();
    } else if (m_concurrentCount <= 0) {
        m_isUploading = false;
        m_uploadBtn->setText(QString::fromUtf8("开始上传"));
        m_addBtn->setEnabled(true);
        m_removeBtn->setEnabled(true);
        m_clearBtn->setEnabled(true);
        m_progressLabel->setText(QString::fromUtf8("完成：成功 %1，失败 %2")
            .arg(m_successCount).arg(m_failCount));
    }
}

void UploadPage::updateFileStatus(int row, const QString &status, const QString &msg)
{
    if (row < 0 || row >= m_fileInfoList.size()) return;
    QString text = m_fileInfoList[row].fileName + " [" + status + "]";
    if (!msg.isEmpty()) text += " (" + msg + ")";
    QStandardItem *item = m_listModel->item(row);
    if (item) item->setText(text);
}

void UploadPage::updateTotalProgress()
{
    if (m_fileInfoList.isEmpty()) {
        m_totalProgressBar->setValue(0);
        return;
    }
    int done = m_successCount + m_failCount;
    m_totalProgressBar->setValue(done * 100 / m_fileInfoList.size());
}

QString UploadPage::formatFileSize(qint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
}

void UploadPage::onToggleView()
{
    m_isCardView = !m_isCardView;
    m_fileListView->setModel(m_isCardView ? m_cardModel : m_listModel);
}

void UploadPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void UploadPage::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void UploadPage::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        QString localPath = url.toLocalFile();
        if (!localPath.isEmpty()) addFileToList(localPath);
    }
    event->acceptProposedAction();
}

bool UploadPage::eventFilter(QObject *obj, QEvent *event)
{
    // 将子控件（dropLabel / fileListView）的拖拽事件转发给 UploadPage 处理
    if (obj == m_dropLabel || obj == m_fileListView) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *de = static_cast<QDragEnterEvent *>(event);
            if (de->mimeData()->hasUrls()) {
                de->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dm = static_cast<QDragMoveEvent *>(event);
            if (dm->mimeData()->hasUrls()) {
                dm->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            QDropEvent *de = static_cast<QDropEvent *>(event);
            for (const QUrl &url : de->mimeData()->urls()) {
                QString localPath = url.toLocalFile();
                if (!localPath.isEmpty()) addFileToList(localPath);
            }
            de->acceptProposedAction();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void UploadPage::loadProjectCombo()
{
    if (!m_projectCombo) return;
    m_projectCombo->clear();
    m_projectNameMap.clear();
    m_projectCombo->addItem(QString::fromUtf8("-- 请选择项目 --"), -1);

    QList<ProjectInfo> projects = AuthManager::instance()->getAllProjects();
    for (const auto &proj : projects) {
        QString text = proj.name;
        if (!proj.code.isEmpty()) text += " [" + proj.code + "]";
        m_projectCombo->addItem(text, proj.id);
        m_projectNameMap[proj.id] = proj.name;  // 存储纯项目名，用于构建远程路径
    }
    // 触发任务刷新
    loadTaskCombo();
}

void UploadPage::loadTaskCombo()
{
    if (!m_taskCombo) return;
    m_taskCombo->clear();
    m_taskCombo->addItem(QString::fromUtf8("-- 请选择测量任务 --"), -1);

    int projectId = -1;
    if (m_projectCombo && m_projectCombo->currentIndex() >= 0) {
        projectId = m_projectCombo->currentData().toInt();
    }

    if (projectId < 0) return;

    QList<MeasureTask> tasks = AuthManager::instance()->getMeasureTasks(projectId);
    for (const auto &task : tasks) {
        m_taskCombo->addItem(task.taskName, task.id);
    }
}

void UploadPage::refreshProjectCombo()
{
    loadProjectCombo();
}

void UploadPage::generateAndAddMeasurementInfo()
{
    int taskId = m_taskCombo->currentData().toInt();
    if (taskId < 0) return;

    MeasureTask task = AuthManager::instance()->getMeasureTask(taskId);
    if (task.id < 0) return;

    // 状态中文映射
    QString statusText;
    if (task.status == "completed") statusText = QString::fromUtf8("已完成");
    else if (task.status == "in_progress") statusText = QString::fromUtf8("进行中");
    else statusText = QString::fromUtf8("待测量");

    // 构建文本内容
    QString content;
    content += QString::fromUtf8("========================================\n");
    content += QString::fromUtf8("           测量任务信息\n");
    content += QString::fromUtf8("========================================\n\n");
    content += QString::fromUtf8("任务名称: %1\n").arg(task.taskName);
    content += QString::fromUtf8("测量日期: %1\n").arg(task.measureDate);
    content += QString::fromUtf8("测量人员: %1\n").arg(task.personnel);
    content += QString::fromUtf8("----------------------------------------\n");
    content += QString::fromUtf8("管道信息:\n");
    content += QString::fromUtf8("  起点: %1\n").arg(task.pipeStart);
    content += QString::fromUtf8("  终点: %1\n").arg(task.pipeEnd);
    content += QString::fromUtf8("  材质: %1\n").arg(task.pipeMaterial);
    content += QString::fromUtf8("  管径: %1\n").arg(task.pipeDiameter);
    content += QString::fromUtf8("  长度: %1\n").arg(task.pipeLength);
    content += QString::fromUtf8("----------------------------------------\n");
    content += QString::fromUtf8("状态: %1\n").arg(statusText);
    content += QString::fromUtf8("创建时间: %1\n").arg(task.createdAt);
    content += QString::fromUtf8("更新时间: %1\n").arg(task.updatedAt);
    if (!task.notes.isEmpty()) {
        content += QString::fromUtf8("\n备注:\n%1\n").arg(task.notes);
    }
    content += QString::fromUtf8("\n========================================\n");

    // 写入临时目录
    if (!m_tempDir) {
        m_tempDir.reset(new QTemporaryDir());
        m_tempDir->setAutoRemove(true);
    }
    if (!m_tempDir->isValid()) {
        qDebug() << "[Upload] 创建临时目录失败";
        return;
    }

    QString fileName = QString("测量信息_%1.txt").arg(task.taskName);
    QString filePath = m_tempDir->path() + "/" + fileName;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[Upload] 写入测量信息文件失败:" << file.errorString();
        return;
    }
    file.write(content.toUtf8());
    file.close();

    // 加入上传队列（排在用户文件前面）
    UploadFileInfo info;
    info.filePath = filePath;
    info.fileName = fileName;
    info.fileSize = QFileInfo(filePath).size();
    info.status = "waiting";
    info.uploadedBytes = 0;

    m_fileInfoList.prepend(info);
    m_listModel->insertRow(0);
    m_listModel->setData(m_listModel->index(0, 0), fileName + QString("  (%1)").arg(formatFileSize(info.fileSize)));

    if (m_countLabel) {
        m_countLabel->setText(QString::fromUtf8("共 %1 个文件").arg(m_fileInfoList.size()));
    }

    qDebug() << "[Upload] 自动添加测量信息文件:" << fileName;
}
