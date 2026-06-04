#include "projectpage.h"
#include "authmanager.h"
#include "sshmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <QApplication>
#include <QStyle>
#include <QScrollArea>
#include <QFormLayout>
#include <QDebug>

// ========================================================================
// ProjectPage 实现
// ========================================================================
ProjectPage::ProjectPage(QWidget *parent)
    : QWidget(parent)
    , m_currentProjectId(-1)
    , m_currentTaskId(-1)
    , m_editingProjectId(-1)
    , m_editingTaskId(-1)
{
    initUI();
    loadProjectList();
}

ProjectPage::~ProjectPage() {}

void ProjectPage::initUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 12, 16, 12);
    mainLayout->setSpacing(12);

    // ======== 左侧：项目列表 ========
    QWidget *leftPanel = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    QLabel *listTitle = new QLabel(QString::fromUtf8("项目列表"));
    listTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #373E65;");
    leftLayout->addWidget(listTitle);

    m_projectList = new QListWidget();
    m_projectList->setMinimumWidth(220);
    m_projectList->setMaximumWidth(300);
    m_projectList->setAlternatingRowColors(true);
    connect(m_projectList, &QListWidget::currentRowChanged,
            this, &ProjectPage::onProjectSelectionChanged);
    leftLayout->addWidget(m_projectList, 1);

    QHBoxLayout *projBtnLayout = new QHBoxLayout();
    m_addProjectBtn = new QPushButton(QString::fromUtf8("新增项目"));
    m_addProjectBtn->setCursor(Qt::PointingHandCursor);
    // 管理员和内业人员均可新增项目
    m_addProjectBtn->setVisible(AuthManager::instance()->isCompanyAdmin()
                                || AuthManager::instance()->isOfficeWorker());
    connect(m_addProjectBtn, &QPushButton::clicked, this, &ProjectPage::onAddProject);
    projBtnLayout->addWidget(m_addProjectBtn);

    m_deleteProjectBtn = new QPushButton(QString::fromUtf8("删除项目"));
    m_deleteProjectBtn->setCursor(Qt::PointingHandCursor);
    // 管理员和内业人员均可删除项目
    m_deleteProjectBtn->setVisible(AuthManager::instance()->isCompanyAdmin()
                                   || AuthManager::instance()->isOfficeWorker());
    connect(m_deleteProjectBtn, &QPushButton::clicked, this, &ProjectPage::onDeleteProject);
    projBtnLayout->addWidget(m_deleteProjectBtn);
    leftLayout->addLayout(projBtnLayout);

    mainLayout->addWidget(leftPanel);

    // ======== 右侧：详情区（StackedWidget） ========
    m_rightStack = new QStackedWidget();
    mainLayout->addWidget(m_rightStack, 1);

    // --- 第1页：项目详情 + 任务管理 ---
    QWidget *detailPage = new QWidget();
    QVBoxLayout *detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(8);

    // 滚动区域包裹项目表单
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    QWidget *formWidget = new QWidget();
    QFormLayout *formLayout = new QFormLayout(formWidget);
    formLayout->setContentsMargins(12, 8, 12, 8);
    formLayout->setSpacing(8);
    formLayout->setHorizontalSpacing(16);

    m_projNameEdit = new QLineEdit();
    m_projNameEdit->setPlaceholderText(QString::fromUtf8("请输入项目名称"));
    m_projCodeEdit = new QLineEdit();
    m_projCodeEdit->setPlaceholderText(QString::fromUtf8("请输入项目编号"));
    m_projLocationEdit = new QLineEdit();
    m_projLocationEdit->setPlaceholderText(QString::fromUtf8("请输入项目地点"));
    m_projClientEdit = new QLineEdit();
    m_projClientEdit->setPlaceholderText(QString::fromUtf8("请输入委托单位"));

    m_projDescEdit = new QTextEdit();
    m_projDescEdit->setMaximumHeight(80);
    m_projDescEdit->setPlaceholderText(QString::fromUtf8("请输入测量任务描述"));
    m_projNotesEdit = new QTextEdit();
    m_projNotesEdit->setMaximumHeight(60);
    m_projNotesEdit->setPlaceholderText(QString::fromUtf8("附加说明、特殊标注等"));

    formLayout->addRow(QString::fromUtf8("项目名称:"), m_projNameEdit);
    formLayout->addRow(QString::fromUtf8("项目编号:"), m_projCodeEdit);
    formLayout->addRow(QString::fromUtf8("项目地点:"), m_projLocationEdit);
    formLayout->addRow(QString::fromUtf8("委托单位:"), m_projClientEdit);
    formLayout->addRow(QString::fromUtf8("测量任务:"), m_projDescEdit);
    formLayout->addRow(QString::fromUtf8("备注:"), m_projNotesEdit);

    // 信息标签
    m_projCreatedByLabel = new QLabel("-");
    m_projCreatedByLabel->setStyleSheet("color: #969696; font-size: 11px;");
    m_projUpdatedLabel = new QLabel("-");
    m_projUpdatedLabel->setStyleSheet("color: #969696; font-size: 11px;");
    formLayout->addRow(QString::fromUtf8("创建人:"), m_projCreatedByLabel);
    formLayout->addRow(QString::fromUtf8("更新时间:"), m_projUpdatedLabel);

    scrollArea->setWidget(formWidget);
    detailLayout->addWidget(scrollArea, 0);

    // 项目保存按钮（所有管理员可编辑自己客户的项目信息）
    QHBoxLayout *saveProjLayout = new QHBoxLayout();
    saveProjLayout->addStretch();
    m_saveProjectBtn = new QPushButton(QString::fromUtf8("保存项目"));
    m_saveProjectBtn->setFixedWidth(120);
    m_saveProjectBtn->setCursor(Qt::PointingHandCursor);
    m_saveProjectBtn->setVisible(AuthManager::instance()->isCompanyAdmin()
                                 || AuthManager::instance()->isOfficeWorker());
    connect(m_saveProjectBtn, &QPushButton::clicked, this, &ProjectPage::onSaveProject);
    saveProjLayout->addWidget(m_saveProjectBtn);
    m_cancelProjectBtn = new QPushButton(QString::fromUtf8("取消"));
    m_cancelProjectBtn->setFixedWidth(80);
    m_cancelProjectBtn->setCursor(Qt::PointingHandCursor);
    m_cancelProjectBtn->setVisible(AuthManager::instance()->isCompanyAdmin()
                                   || AuthManager::instance()->isOfficeWorker());
    saveProjLayout->addWidget(m_cancelProjectBtn);
    detailLayout->addLayout(saveProjLayout);

    // 非管理员且非内业人员：项目详情只读
    if (!AuthManager::instance()->isCompanyAdmin()
        && !AuthManager::instance()->isOfficeWorker()) {
        m_projNameEdit->setReadOnly(true);
        m_projCodeEdit->setReadOnly(true);
        m_projLocationEdit->setReadOnly(true);
        m_projClientEdit->setReadOnly(true);
        m_projDescEdit->setReadOnly(true);
        m_projNotesEdit->setReadOnly(true);
    }

    // 分隔线
    QFrame *separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("color: #e0e0e0;");
    detailLayout->addWidget(separator);

    // 测量任务区域
    QHBoxLayout *taskHeaderLayout = new QHBoxLayout();
    QLabel *taskTitle = new QLabel(QString::fromUtf8("测量任务"));
    taskTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #373E65;");
    taskHeaderLayout->addWidget(taskTitle);
    taskHeaderLayout->addStretch();

    m_taskStatsLabel = new QLabel(QString::fromUtf8("进度: -"));
    m_taskStatsLabel->setStyleSheet("color: #4ec9b0; font-size: 12px; font-weight: bold;");
    taskHeaderLayout->addWidget(m_taskStatsLabel);

    m_addTaskBtn = new QPushButton(QString::fromUtf8("新增任务"));
    m_addTaskBtn->setFixedWidth(90);
    m_addTaskBtn->setCursor(Qt::PointingHandCursor);
    // 管理员和内业人员可管理任务
    m_addTaskBtn->setVisible(AuthManager::instance()->isCompanyAdmin() || AuthManager::instance()->isOfficeWorker());
    connect(m_addTaskBtn, &QPushButton::clicked, this, &ProjectPage::onAddTask);
    taskHeaderLayout->addWidget(m_addTaskBtn);

    m_editTaskBtn = new QPushButton(QString::fromUtf8("编辑任务"));
    m_editTaskBtn->setFixedWidth(90);
    m_editTaskBtn->setCursor(Qt::PointingHandCursor);
    m_editTaskBtn->setVisible(AuthManager::instance()->isCompanyAdmin() || AuthManager::instance()->isOfficeWorker());
    connect(m_editTaskBtn, &QPushButton::clicked, this, &ProjectPage::onEditTask);
    taskHeaderLayout->addWidget(m_editTaskBtn);

    m_deleteTaskBtn = new QPushButton(QString::fromUtf8("删除任务"));
    m_deleteTaskBtn->setFixedWidth(90);
    m_deleteTaskBtn->setCursor(Qt::PointingHandCursor);
    m_deleteTaskBtn->setVisible(AuthManager::instance()->isCompanyAdmin() || AuthManager::instance()->isOfficeWorker());
    connect(m_deleteTaskBtn, &QPushButton::clicked, this, &ProjectPage::onDeleteTask);
    taskHeaderLayout->addWidget(m_deleteTaskBtn);

    detailLayout->addLayout(taskHeaderLayout);

    // 任务表格
    m_taskTable = new QTableWidget();
    m_taskTable->setColumnCount(7);
    m_taskTable->setHorizontalHeaderLabels({
        QString::fromUtf8("任务名称"),
        QString::fromUtf8("测量日期"),
        QString::fromUtf8("测量人员"),
        QString::fromUtf8("管道起/终点"),
        QString::fromUtf8("材质/管径/长度"),
        QString::fromUtf8("状态"),
        QString::fromUtf8("备注")
    });
    m_taskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_taskTable->horizontalHeader()->setStretchLastSection(true);
    m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_taskTable->setAlternatingRowColors(true);
    m_taskTable->verticalHeader()->setVisible(false);
    connect(m_taskTable, &QTableWidget::cellClicked,
            this, &ProjectPage::onTaskSelectionChanged);
    detailLayout->addWidget(m_taskTable, 1);

    m_detailPageIndex = m_rightStack->addWidget(detailPage);

    // --- 第2页：任务编辑表单 ---
    QWidget *taskFormPage = new QWidget();
    QVBoxLayout *taskFormLayout = new QVBoxLayout(taskFormPage);
    taskFormLayout->setContentsMargins(16, 12, 16, 12);
    taskFormLayout->setSpacing(10);

    QLabel *taskFormTitle = new QLabel(QString::fromUtf8("编辑测量任务"));
    taskFormTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #373E65;");
    taskFormLayout->addWidget(taskFormTitle);

    QFormLayout *taskForm = new QFormLayout();
    taskForm->setSpacing(8);
    taskForm->setHorizontalSpacing(16);

    m_taskNameEdit = new QLineEdit();
    m_taskNameEdit->setPlaceholderText(QString::fromUtf8("请输入任务名称"));
    m_taskDateEdit = new QDateEdit();
    m_taskDateEdit->setCalendarPopup(true);
    m_taskDateEdit->setDisplayFormat(QString("yyyy-MM-dd"));
    m_taskDateEdit->setDate(QDate::currentDate());
    m_taskPersonnelEdit = new QLineEdit();
    m_taskPersonnelEdit->setPlaceholderText(QString::fromUtf8("请输入测量人员"));

    QHBoxLayout *pipeLayout = new QHBoxLayout();
    m_pipeStartEdit = new QLineEdit();
    m_pipeStartEdit->setPlaceholderText(QString::fromUtf8("起点"));
    m_pipeEndEdit = new QLineEdit();
    m_pipeEndEdit->setPlaceholderText(QString::fromUtf8("终点"));
    pipeLayout->addWidget(m_pipeStartEdit);
    pipeLayout->addWidget(new QLabel(QString::fromUtf8(" → ")));
    pipeLayout->addWidget(m_pipeEndEdit);

    QHBoxLayout *pipeSpecLayout = new QHBoxLayout();
    m_pipeMaterialCombo = new QComboBox();
    m_pipeMaterialCombo->addItems({
        QString::fromUtf8("PE管"), QString::fromUtf8("钢管"),
        QString::fromUtf8("球墨铸铁管"), QString::fromUtf8("PVC管"),
        QString::fromUtf8("不锈钢管"), QString::fromUtf8("其他")
    });
    m_pipeMaterialCombo->setEditable(true);
    m_pipeDiameterEdit = new QLineEdit();
    m_pipeDiameterEdit->setPlaceholderText(QString::fromUtf8("管径"));
    m_pipeLengthEdit = new QLineEdit();
    m_pipeLengthEdit->setPlaceholderText(QString::fromUtf8("长度"));
    pipeSpecLayout->addWidget(m_pipeMaterialCombo);
    pipeSpecLayout->addWidget(new QLabel(QString::fromUtf8(" / ")));
    pipeSpecLayout->addWidget(m_pipeDiameterEdit);
    pipeSpecLayout->addWidget(new QLabel(QString::fromUtf8("mm")));
    pipeSpecLayout->addWidget(new QLabel(QString::fromUtf8(" / ")));
    pipeSpecLayout->addWidget(m_pipeLengthEdit);
    pipeSpecLayout->addWidget(new QLabel(QString::fromUtf8("m")));

    m_taskStatusCombo = new QComboBox();
    m_taskStatusCombo->addItems({
        QString::fromUtf8("待测量"), QString::fromUtf8("进行中"), QString::fromUtf8("已完成")
    });

    m_taskNotesEdit = new QTextEdit();
    m_taskNotesEdit->setMaximumHeight(80);
    m_taskNotesEdit->setPlaceholderText(QString::fromUtf8("附加说明"));

    taskForm->addRow(QString::fromUtf8("任务名称:"), m_taskNameEdit);
    taskForm->addRow(QString::fromUtf8("测量日期:"), m_taskDateEdit);
    taskForm->addRow(QString::fromUtf8("测量人员:"), m_taskPersonnelEdit);
    taskForm->addRow(QString::fromUtf8("管道起/终点:"), pipeLayout);
    taskForm->addRow(QString::fromUtf8("管道规格:"), pipeSpecLayout);
    taskForm->addRow(QString::fromUtf8("状态:"), m_taskStatusCombo);
    taskForm->addRow(QString::fromUtf8("备注:"), m_taskNotesEdit);

    taskFormLayout->addLayout(taskForm);
    taskFormLayout->addStretch();

    QHBoxLayout *taskSaveLayout = new QHBoxLayout();
    taskSaveLayout->addStretch();
    m_saveTaskBtn = new QPushButton(QString::fromUtf8("保存任务"));
    m_saveTaskBtn->setFixedWidth(120);
    m_saveTaskBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveTaskBtn, &QPushButton::clicked, this, &ProjectPage::onSaveTask);
    taskSaveLayout->addWidget(m_saveTaskBtn);
    m_cancelTaskBtn = new QPushButton(QString::fromUtf8("返回"));
    m_cancelTaskBtn->setFixedWidth(80);
    m_cancelTaskBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelTaskBtn, &QPushButton::clicked, this, [this]() {
        m_rightStack->setCurrentIndex(m_detailPageIndex);
    });
    taskSaveLayout->addWidget(m_cancelTaskBtn);
    taskFormLayout->addLayout(taskSaveLayout);

    m_taskFormPageIndex = m_rightStack->addWidget(taskFormPage);
}

// ========================================================================
// 项目列表
// ========================================================================
void ProjectPage::loadProjectList()
{
    m_projectList->clear();
    QList<ProjectInfo> projects = AuthManager::instance()->getAllProjects();

    if (AuthManager::instance()->isSuperAdmin()) {
        qDebug() << "[ProjectPage] 超级管理员模式：加载所有项目，共" << projects.size() << "个";
    } else {
        qDebug() << "[ProjectPage] 权限过滤：只显示关联客户的项目，共" << projects.size() << "个";
    }

    for (const auto &proj : projects) {
        QString text = proj.name;
        if (!proj.code.isEmpty()) {
            text += " [" + proj.code + "]";
        }
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, proj.id);
        m_projectList->addItem(item);
    }
}

void ProjectPage::refreshProjectList()
{
    loadProjectList();
}

void ProjectPage::refreshPermissions()
{
    bool canManage = AuthManager::instance()->isCompanyAdmin() || AuthManager::instance()->isOfficeWorker();

    // 新增/删除项目：管理员和内业人员均可操作
    if (m_addProjectBtn) m_addProjectBtn->setVisible(canManage);
    if (m_deleteProjectBtn) m_deleteProjectBtn->setVisible(canManage);

    // 保存/取消按钮：管理员和内业人员均可见
    if (m_saveProjectBtn) m_saveProjectBtn->setVisible(canManage);
    if (m_cancelProjectBtn) m_cancelProjectBtn->setVisible(canManage);

    // 任务管理按钮：管理员和内业人员
    if (m_addTaskBtn) m_addTaskBtn->setVisible(canManage);
    if (m_editTaskBtn) m_editTaskBtn->setVisible(canManage);
    if (m_deleteTaskBtn) m_deleteTaskBtn->setVisible(canManage);

    // 表单可编辑性：管理员和内业人员均可编辑项目详情
    bool readOnly = !canManage;
    if (m_projNameEdit) m_projNameEdit->setReadOnly(readOnly);
    if (m_projCodeEdit) m_projCodeEdit->setReadOnly(readOnly);
    if (m_projLocationEdit) m_projLocationEdit->setReadOnly(readOnly);
    if (m_projClientEdit) m_projClientEdit->setReadOnly(readOnly);
    if (m_projDescEdit) m_projDescEdit->setReadOnly(readOnly);
    if (m_projNotesEdit) m_projNotesEdit->setReadOnly(readOnly);

    qDebug() << "[ProjectPage] refreshPermissions: canManage=" << canManage
             << "readOnly=" << readOnly;
}

void ProjectPage::reset()
{
    // 清空项目列表
    m_projectList->clear();

    // 清空任务表格
    m_taskTable->setRowCount(0);
    m_taskStatsLabel->setText(QString::fromUtf8("进度: -"));

    // 清空表单
    clearProjectForm();
    clearTaskForm();

    // 切换到默认详情页
    m_rightStack->setCurrentIndex(m_detailPageIndex);

    // 重置状态变量
    m_currentProjectId = -1;
    m_currentTaskId = -1;
    m_editingProjectId = -1;
    m_editingTaskId = -1;
}

void ProjectPage::onProjectSelectionChanged()
{
    int row = m_projectList->currentRow();
    if (row < 0) {
        m_currentProjectId = -1;
        return;
    }

    QListWidgetItem *item = m_projectList->item(row);
    m_currentProjectId = item->data(Qt::UserRole).toInt();
    loadProjectDetail(m_currentProjectId);
    loadTasks(m_currentProjectId);
}

// ========================================================================
// 项目详情
// ========================================================================
void ProjectPage::loadProjectDetail(int projectId)
{
    ProjectInfo info = AuthManager::instance()->getProject(projectId);
    if (info.id < 0) return;

    fillProjectForm(info);
    m_editingProjectId = projectId;
    updateProjectStats(projectId);
}

void ProjectPage::fillProjectForm(const ProjectInfo &info)
{
    m_projNameEdit->setText(info.name);
    m_projCodeEdit->setText(info.code);
    m_projLocationEdit->setText(info.location);
    m_projClientEdit->setText(info.client);
    m_projDescEdit->setPlainText(info.description);
    m_projNotesEdit->setPlainText(info.notes);
    m_projCreatedByLabel->setText(info.createdBy + "  " + info.createdAt);
    m_projUpdatedLabel->setText(info.updatedAt);
}

void ProjectPage::clearProjectForm()
{
    m_projNameEdit->clear();
    m_projCodeEdit->clear();
    m_projLocationEdit->clear();
    m_projClientEdit->clear();
    m_projDescEdit->clear();
    m_projNotesEdit->clear();
    m_projCreatedByLabel->setText("-");
    m_projUpdatedLabel->setText("-");
}

ProjectInfo ProjectPage::collectProjectFromForm()
{
    ProjectInfo info;
    info.name        = m_projNameEdit->text().trimmed();
    info.code        = m_projCodeEdit->text().trimmed();
    info.location    = m_projLocationEdit->text().trimmed();
    info.client      = m_projClientEdit->text().trimmed();
    info.description = m_projDescEdit->toPlainText().trimmed();
    info.notes       = m_projNotesEdit->toPlainText().trimmed();
    info.createdBy   = AuthManager::instance()->currentUser();
    // 自动关联当前用户的客户名
    info.clientName  = AuthManager::instance()->currentUserClient();
    if (info.clientName.isEmpty()) {
        info.clientName = AuthManager::instance()->currentUser();
    }
    return info;
}

// ========================================================================
// 项目 CRUD
// ========================================================================
void ProjectPage::onAddProject()
{
    clearProjectForm();
    m_editingProjectId = -1;
    m_currentProjectId = -1;
    m_projectList->clearSelection();

    // 清空任务表格
    m_taskTable->setRowCount(0);
    m_taskStatsLabel->setText(QString::fromUtf8("进度: -"));

    m_projNameEdit->setFocus();
}

void ProjectPage::onEditProject()
{
    // 当前项目已在表单中，直接编辑即可（表单始终可编辑）
    if (m_currentProjectId < 0) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一个项目"));
    }
}

void ProjectPage::onDeleteProject()
{
    if (m_currentProjectId < 0) return;

    // 提前获取项目信息，用于后续远程清理
    ProjectInfo projInfo = AuthManager::instance()->getProject(m_currentProjectId);

    auto reply = QMessageBox::question(this,
        QString::fromUtf8("删除项目"),
        QString::fromUtf8("确定要删除项目 \"%1\" 及其所有测量任务吗？\n\n此操作将同时删除云端对应的所有数据文件，不可恢复。")
            .arg(projInfo.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    if (AuthManager::instance()->deleteProject(m_currentProjectId)) {
        // 删除远程项目目录：/home/ubuntu/<clientName>/<projectName>/
        SshManager *ssh = SshManager::instance();
        if (ssh->isConnected() && !projInfo.name.isEmpty()) {
            QString clientName = projInfo.clientName;
            if (clientName.isEmpty()) {
                clientName = AuthManager::instance()->currentUserClient();
                if (clientName.isEmpty()) clientName = AuthManager::instance()->currentUser();
            }
            QString remotePath = QString("/home/ubuntu/%1/%2").arg(clientName, projInfo.name);
            bool remoteDeleted = ssh->deleteRemoteDir(remotePath);
            if (!remoteDeleted) {
                qWarning() << "[ProjectPage] 远程项目目录删除失败或SSH未连接:" << remotePath;
            }
        }

        loadProjectList();
        clearProjectForm();
        m_currentProjectId = -1;
        m_editingProjectId = -1;
        m_taskTable->setRowCount(0);
        m_taskStatsLabel->setText(QString::fromUtf8("进度: -"));
        // 通知 UploadPage 刷新项目下拉框
        emit projectsChanged();
    } else {
        QMessageBox::warning(this, QString::fromUtf8("删除失败"),
                             QString::fromUtf8("删除项目失败，请重试"));
    }
}

void ProjectPage::onSaveProject()
{
    ProjectInfo info = collectProjectFromForm();

    if (info.name.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("项目名称不能为空"));
        m_projNameEdit->setFocus();
        return;
    }

    bool ok;
    if (m_editingProjectId < 0) {
        // 新增
        int id = AuthManager::instance()->addProject(info);
        ok = (id > 0);
        if (ok) {
            m_editingProjectId = id;
            m_currentProjectId = id;
        }
    } else {
        // 更新
        ok = AuthManager::instance()->updateProject(m_editingProjectId, info);
    }

    if (ok) {
        loadProjectList();
        // 重新选中刚保存的项目
        for (int i = 0; i < m_projectList->count(); ++i) {
            if (m_projectList->item(i)->data(Qt::UserRole).toInt() == m_editingProjectId) {
                m_projectList->setCurrentRow(i);
                break;
            }
        }
        // 通知 UploadPage 刷新项目下拉框
        emit projectsChanged();
    } else {
        QMessageBox::warning(this, QString::fromUtf8("保存失败"),
                             QString::fromUtf8("保存项目失败，请重试"));
    }
}

// ========================================================================
// 测量任务表格
// ========================================================================
void ProjectPage::loadTasks(int projectId)
{
    m_taskTable->setRowCount(0);
    if (projectId < 0) return;

    QList<MeasureTask> tasks = AuthManager::instance()->getMeasureTasks(projectId);

    for (const auto &task : tasks) {
        int row = m_taskTable->rowCount();
        m_taskTable->insertRow(row);

        // 任务名称
        m_taskTable->setItem(row, 0, new QTableWidgetItem(task.taskName));

        // 测量日期
        m_taskTable->setItem(row, 1, new QTableWidgetItem(task.measureDate));

        // 测量人员
        m_taskTable->setItem(row, 2, new QTableWidgetItem(task.personnel));

        // 管道起/终点
        QString pipeRange = task.pipeStart;
        if (!task.pipeEnd.isEmpty()) {
            pipeRange += " -> " + task.pipeEnd;
        }
        m_taskTable->setItem(row, 3, new QTableWidgetItem(pipeRange));

        // 材质/管径/长度
        QStringList specs;
        if (!task.pipeMaterial.isEmpty()) specs << task.pipeMaterial;
        if (!task.pipeDiameter.isEmpty()) specs << (task.pipeDiameter + "mm");
        if (!task.pipeLength.isEmpty()) specs << (task.pipeLength + "m");
        m_taskTable->setItem(row, 4, new QTableWidgetItem(specs.join(" / ")));

        // 状态
        QString statusText;
        if (task.status == "completed") {
            statusText = QString::fromUtf8("已完成");
        } else if (task.status == "in_progress") {
            statusText = QString::fromUtf8("进行中");
        } else {
            statusText = QString::fromUtf8("待测量");
        }
        QTableWidgetItem *statusItem = new QTableWidgetItem(statusText);
        if (task.status == "completed") {
            statusItem->setForeground(QColor("#27ae60"));
        } else if (task.status == "in_progress") {
            statusItem->setForeground(QColor("#e67e22"));
        }
        m_taskTable->setItem(row, 5, statusItem);

        // 备注（截断显示）
        QString notePreview = task.notes;
        if (notePreview.length() > 30) {
            notePreview = notePreview.left(30) + "...";
        }
        m_taskTable->setItem(row, 6, new QTableWidgetItem(notePreview));

        // 存储任务 ID
        m_taskTable->item(row, 0)->setData(Qt::UserRole, task.id);
    }

    updateProjectStats(projectId);
}

void ProjectPage::updateProjectStats(int projectId)
{
    QList<MeasureTask> tasks = AuthManager::instance()->getMeasureTasks(projectId);
    if (tasks.isEmpty()) {
        m_taskStatsLabel->setText(QString::fromUtf8("进度: 无任务"));
        return;
    }

    int completed = 0;
    int total = tasks.size();
    for (const auto &t : tasks) {
        if (t.status == "completed") completed++;
    }

    m_taskStatsLabel->setText(
        QString::fromUtf8("进度: %1/%2 完成")
            .arg(completed).arg(total));
}

void ProjectPage::onTaskSelectionChanged()
{
    int row = m_taskTable->currentRow();
    if (row < 0) {
        m_currentTaskId = -1;
        return;
    }
    QTableWidgetItem *item = m_taskTable->item(row, 0);
    if (item) {
        m_currentTaskId = item->data(Qt::UserRole).toInt();
    }
}

// ========================================================================
// 任务 CRUD
// ========================================================================
void ProjectPage::onAddTask()
{
    if (m_currentProjectId < 0) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择或创建一个项目"));
        return;
    }
    clearTaskForm();
    m_editingTaskId = -1;
    m_taskStatusCombo->setCurrentIndex(0);
    m_rightStack->setCurrentIndex(m_taskFormPageIndex);
    m_taskNameEdit->setFocus();
}

void ProjectPage::onEditTask()
{
    if (m_currentTaskId < 0) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先在表格中选择一个任务"));
        return;
    }

    MeasureTask task = AuthManager::instance()->getMeasureTask(m_currentTaskId);
    if (task.id < 0) return;

    fillTaskForm(task);
    m_editingTaskId = task.id;
    m_rightStack->setCurrentIndex(m_taskFormPageIndex);
}

void ProjectPage::onDeleteTask()
{
    if (m_currentTaskId < 0) return;

    // 提前获取任务和项目信息，用于后续远程清理
    MeasureTask taskInfo = AuthManager::instance()->getMeasureTask(m_currentTaskId);
    ProjectInfo projInfo = AuthManager::instance()->getProject(m_currentProjectId);

    auto reply = QMessageBox::question(this,
        QString::fromUtf8("删除任务"),
        QString::fromUtf8("确定要删除测量任务 \"%1\" 吗？\n\n此操作将同时删除云端对应的所有数据文件。")
            .arg(taskInfo.taskName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    if (AuthManager::instance()->deleteMeasureTask(m_currentTaskId)) {
        // 删除远程任务目录：/home/ubuntu/<clientName>/<taskName>/
        SshManager *ssh = SshManager::instance();
        if (ssh->isConnected() && !taskInfo.taskName.isEmpty() && !projInfo.name.isEmpty()) {
            QString clientName = projInfo.clientName;
            if (clientName.isEmpty()) {
                clientName = AuthManager::instance()->currentUserClient();
                if (clientName.isEmpty()) clientName = AuthManager::instance()->currentUser();
            }
            QString remotePath = QString("/home/ubuntu/%1/%2")
                                     .arg(clientName, taskInfo.taskName);
            bool remoteDeleted = ssh->deleteRemoteDir(remotePath);
            if (!remoteDeleted) {
                qWarning() << "[ProjectPage] 远程任务目录删除失败或SSH未连接:" << remotePath;
            }
        }

        loadTasks(m_currentProjectId);
        m_currentTaskId = -1;
        // 通知 UploadPage 刷新任务下拉框
        emit projectsChanged();
    } else {
        QMessageBox::warning(this, QString::fromUtf8("删除失败"),
                             QString::fromUtf8("删除任务失败"));
    }
}

void ProjectPage::fillTaskForm(const MeasureTask &task)
{
    m_taskNameEdit->setText(task.taskName);
    m_taskDateEdit->setDate(QDate::fromString(task.measureDate, QString("yyyy-MM-dd")));
    m_taskPersonnelEdit->setText(task.personnel);
    m_pipeStartEdit->setText(task.pipeStart);
    m_pipeEndEdit->setText(task.pipeEnd);

    // 设置材质下拉框
    int matIdx = m_pipeMaterialCombo->findText(task.pipeMaterial);
    if (matIdx >= 0) {
        m_pipeMaterialCombo->setCurrentIndex(matIdx);
    } else {
        m_pipeMaterialCombo->setCurrentText(task.pipeMaterial);
    }

    m_pipeDiameterEdit->setText(task.pipeDiameter);
    m_pipeLengthEdit->setText(task.pipeLength);

    // 状态
    if (task.status == "completed") {
        m_taskStatusCombo->setCurrentIndex(2);
    } else if (task.status == "in_progress") {
        m_taskStatusCombo->setCurrentIndex(1);
    } else {
        m_taskStatusCombo->setCurrentIndex(0);
    }

    m_taskNotesEdit->setPlainText(task.notes);
}

void ProjectPage::clearTaskForm()
{
    m_taskNameEdit->clear();
    m_taskDateEdit->setDate(QDate::currentDate());
    m_taskPersonnelEdit->clear();
    m_pipeStartEdit->clear();
    m_pipeEndEdit->clear();
    m_pipeMaterialCombo->setCurrentIndex(0);
    m_pipeDiameterEdit->clear();
    m_pipeLengthEdit->clear();
    m_taskStatusCombo->setCurrentIndex(0);
    m_taskNotesEdit->clear();
}

MeasureTask ProjectPage::collectTaskFromForm()
{
    MeasureTask task;
    task.projectId    = m_currentProjectId;
    task.taskName      = m_taskNameEdit->text().trimmed();
    task.measureDate   = m_taskDateEdit->date().toString(QString("yyyy-MM-dd"));
    task.personnel     = m_taskPersonnelEdit->text().trimmed();
    task.pipeStart     = m_pipeStartEdit->text().trimmed();
    task.pipeEnd       = m_pipeEndEdit->text().trimmed();
    task.pipeMaterial  = m_pipeMaterialCombo->currentText().trimmed();
    task.pipeDiameter  = m_pipeDiameterEdit->text().trimmed();
    task.pipeLength    = m_pipeLengthEdit->text().trimmed();
    task.notes         = m_taskNotesEdit->toPlainText().trimmed();

    int statusIdx = m_taskStatusCombo->currentIndex();
    switch (statusIdx) {
    case 2: task.status = "completed";    break;
    case 1: task.status = "in_progress"; break;
    default: task.status = "pending";   break;
    }

    return task;
}

void ProjectPage::onSaveTask()
{
    if (m_currentProjectId < 0) return;

    MeasureTask task = collectTaskFromForm();

    if (task.taskName.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("任务名称不能为空"));
        m_taskNameEdit->setFocus();
        return;
    }

    bool ok;
    if (m_editingTaskId < 0) {
        int id = AuthManager::instance()->addMeasureTask(task);
        ok = (id > 0);
    } else {
        task.id = m_editingTaskId;
        ok = AuthManager::instance()->updateMeasureTask(m_editingTaskId, task);
    }

    if (ok) {
        loadTasks(m_currentProjectId);
        m_rightStack->setCurrentIndex(m_detailPageIndex);
        m_editingTaskId = -1;
        // 通知 UploadPage 刷新任务下拉框
        emit projectsChanged();
    } else {
        QMessageBox::warning(this, QString::fromUtf8("保存失败"),
                             QString::fromUtf8("保存任务失败"));
    }
}

