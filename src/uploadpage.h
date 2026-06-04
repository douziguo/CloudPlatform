#ifndef UPLOADPAGE_H
#define UPLOADPAGE_H

#include <QWidget>
#include <QListView>
#include <QStandardItemModel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QMutex>
#include <QList>
#include <QComboBox>
#include <QMap>
#include <QTemporaryDir>
#include <QScopedPointer>

// 上传文件信息
struct UploadFileInfo {
    QString filePath;     // 本地路径
    QString fileName;     // 文件名
    qint64  fileSize;   // 文件大小
    QString status;       // 状态: waiting/uploading/success/failed
    QString errorMsg;     // 错误信息
    qint64  uploadedBytes; // 已上传字节
};

class UploadPage : public QWidget
{
    Q_OBJECT

public:
    explicit UploadPage(QWidget *parent = nullptr);

    // 三级目录设置（由 MainWindow 调用）- 保留兼容
    void setCustomer(const QString &name) { m_customer = name; }
    void setProject(const QString &name)  { m_project  = name; }
    void setTask(const QString &name)     { m_task     = name; }

public slots:
    void refreshProjectCombo();  // 刷新项目下拉框
    void reset();                // 退出登录时重置所有状态

private slots:
    void onAddFiles();
    void onRemoveFile();
    void onClearFiles();
    void onUploadClicked();
    void onToggleView();
    void onUploadProgress(const QString &key, qint64 sent, qint64 total);
    void onUploadFinished(const QString &key, bool success, const QString &message);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void initUI();
    void addFileToList(const QString &filePath);
    void updateFileStatus(int row, const QString &status, const QString &msg = "");
    void updateTotalProgress();
    void startNextUpload();
    QString formatFileSize(qint64 bytes);
    QString buildRemotePath();
    qint64 getTotalSize() const;  // 计算所有文件总大小
    void loadProjectCombo();     // 加载项目下拉框
    void loadTaskCombo();        // 加载测量任务下拉框
    // 自动生成测量信息txt并加入上传队列
    void generateAndAddMeasurementInfo();

    // 文件列表数据
    QList<UploadFileInfo>  m_fileInfoList;
    QStandardItemModel*     m_listModel   = nullptr;
    QStandardItemModel*     m_cardModel   = nullptr;

    // UI 控件
    QLabel*         m_dropLabel         = nullptr;
    QListView*      m_fileListView      = nullptr;
    QPushButton*    m_addBtn            = nullptr;
    QPushButton*    m_removeBtn         = nullptr;
    QPushButton*    m_clearBtn          = nullptr;
    QPushButton*    m_toggleViewBtn    = nullptr;
    QPushButton*    m_uploadBtn         = nullptr;
    QProgressBar*   m_totalProgressBar  = nullptr;
    QLabel*         m_progressLabel      = nullptr;
    QLabel*         m_resultLabel        = nullptr;
    QLabel*         m_pathPreviewLabel   = nullptr;
    QLabel*         m_countLabel         = nullptr;

    // 三级目录输入控件（隐藏，自动关联）
    QLineEdit*      m_customerEdit      = nullptr;
    QLineEdit*      m_projectEdit       = nullptr;
    QLineEdit*      m_taskEdit          = nullptr;

    // 项目/任务下拉选择（替代手动输入）
    QComboBox*      m_projectCombo      = nullptr;
    QComboBox*      m_taskCombo         = nullptr;

    // 状态
    bool             m_isCardView        = false;
    bool             m_isUploading       = false;
    int              m_currentUploadIndex = 0;
    int              m_successCount      = 0;
    int              m_failCount         = 0;
    int              m_concurrentCount   = 0;
    static const int  MAX_CONCURRENT    = 3;
    QMutex           m_uploadMutex;

    // 三级目录标识
    QString           m_customer;
    QString           m_project;
    QString           m_task;

    // projectId -> 纯项目名（不含 [项目代码] 后缀）
    QMap<int, QString> m_projectNameMap;

    // 临时目录（存放自动生成的测量信息txt）
    QScopedPointer<QTemporaryDir> m_tempDir;
};

#endif // UPLOADPAGE_H
