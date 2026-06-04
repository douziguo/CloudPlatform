#ifndef PROJECTPAGE_H
#define PROJECTPAGE_H

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QSplitter>
#include <QStackedWidget>
#include "authmanager.h"

class ProjectPage : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectPage(QWidget *parent = nullptr);
    ~ProjectPage();

public slots:
    void refreshProjectList();

    // 登录/角色变更后刷新权限配置（由 MainWindow 调用）
    void refreshPermissions();

    // 退出登录时重置所有状态
    void reset();

signals:
    // 项目/任务变更后通知外部（如 UploadPage 刷新下拉框）
    void projectsChanged();

private slots:
    void onProjectSelectionChanged();
    void onAddProject();
    void onEditProject();
    void onDeleteProject();
    void onSaveProject();

    void onAddTask();
    void onEditTask();
    void onDeleteTask();
    void onSaveTask();
    void onTaskSelectionChanged();

private:
    void initUI();
    void loadProjectList();
    void loadProjectDetail(int projectId);
    void loadTasks(int projectId);
    void clearProjectForm();
    void clearTaskForm();
    void fillProjectForm(const ProjectInfo &info);
    void fillTaskForm(const MeasureTask &task);
    ProjectInfo collectProjectFromForm();
    MeasureTask collectTaskFromForm();
    void updateProjectStats(int projectId);

    // 左侧项目列表
    QListWidget *m_projectList = nullptr;
    QPushButton *m_addProjectBtn = nullptr;
    QPushButton *m_deleteProjectBtn = nullptr;

    // 右侧详情区
    QStackedWidget *m_rightStack = nullptr;

    // 项目信息表单
    QLineEdit   *m_projNameEdit   = nullptr;
    QLineEdit   *m_projCodeEdit   = nullptr;
    QLineEdit   *m_projLocationEdit = nullptr;
    QLineEdit   *m_projClientEdit = nullptr;
    QTextEdit   *m_projDescEdit   = nullptr;
    QTextEdit   *m_projNotesEdit  = nullptr;
    QLabel      *m_projCreatedByLabel = nullptr;
    QLabel      *m_projUpdatedLabel   = nullptr;
    QPushButton *m_saveProjectBtn = nullptr;
    QPushButton *m_cancelProjectBtn = nullptr;

    // 测量任务表格
    QTableWidget *m_taskTable = nullptr;
    QPushButton   *m_addTaskBtn  = nullptr;
    QPushButton   *m_editTaskBtn = nullptr;
    QPushButton *m_deleteTaskBtn = nullptr;
    QLabel      *m_taskStatsLabel = nullptr;

    // 任务编辑表单
    QLineEdit   *m_taskNameEdit      = nullptr;
    QLineEdit   *m_taskPersonnelEdit = nullptr;
    QDateEdit   *m_taskDateEdit      = nullptr;
    QLineEdit   *m_pipeStartEdit     = nullptr;
    QLineEdit   *m_pipeEndEdit       = nullptr;
    QComboBox   *m_pipeMaterialCombo = nullptr;
    QLineEdit   *m_pipeDiameterEdit  = nullptr;
    QLineEdit   *m_pipeLengthEdit    = nullptr;
    QComboBox   *m_taskStatusCombo   = nullptr;
    QTextEdit   *m_taskNotesEdit     = nullptr;
    QPushButton *m_saveTaskBtn       = nullptr;
    QPushButton *m_cancelTaskBtn     = nullptr;

    // 当前状态
    int m_currentProjectId = -1;
    int m_currentTaskId    = -1;
    int m_editingProjectId = -1;  // -1=新建, >0=编辑
    int m_editingTaskId    = -1;

    // 项目表单页和任务表页的索引
    int m_detailPageIndex = 0;
    int m_taskFormPageIndex = 0;
};

#endif // PROJECTPAGE_H
