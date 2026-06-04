#ifndef LOCALMANAGERPAGE_H
#define LOCALMANAGERPAGE_H

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QProgressBar>
#include <QGroupBox>
#include <QTextEdit>
#include <QSplitter>
#include "authmanager.h"

// ============================================================
//  LocalManagerPage  本地管理页面
//
//  功能：
//   1. 历史记录  ── 上传/下载记录表
//   2. 备份/恢复 ── 一键全量 or 选定项目 备份→ZIP / ZIP→恢复
// ============================================================
class LocalManagerPage : public QWidget
{
    Q_OBJECT
public:
    explicit LocalManagerPage(QWidget *parent = nullptr);

    // 刷新历史记录
    void refreshHistory();

    // 供 MainWindow 调用：记录一次上传/下载（由各页面完成操作后统一回调）
    void notifyUpload(const QString &remotePath, const QString &fileName);
    void notifyDownload(const QString &remotePath, const QString &fileName,
                        const QString &localPath, qint64 fileSize);

    // 退出登录时重置所有状态
    void reset();

signals:
    // 备份恢复完成后通知（如刷新项目管理页面）
    void backupRestoreCompleted();

private slots:
    // 历史
    void onClearHistory();
    void onExportHistory();

    // 备份
    void onBackupAll();                // 全量备份
    void onBackupSelected();           // 选定项目备份
    void onRestoreAll();               // 全量恢复（从ZIP）
    void onRestoreSelected();          // 选定项目恢复

    // 项目列表选中时刷新 "备份选定项目" 按钮状态
    void onProjectSelectionChanged();

private:
    // ----- UI 初始化 -----
    void initUI();
    QWidget *buildHistoryTab();
    QWidget *buildBackupTab();

    // ----- 备份辅助 -----
    // 将所有/指定项目的元数据（JSON）序列化到文件
    bool exportProjectsToJson(const QList<int> &projectIds, const QString &jsonPath);
    // 从JSON文件导入项目元数据
    bool importProjectsFromJson(const QString &jsonPath,
                                QStringList &importLog, bool overwrite = false);
    // 调用 PowerShell Compress-Archive 打包目录为ZIP
    bool compressToZip(const QString &sourceDir, const QString &zipPath);
    // 解压ZIP到目录（调用PowerShell Expand-Archive）
    bool extractZip(const QString &zipPath, const QString &destDir);

    // 将 SCP 目录结构下载到本地临时目录
    bool downloadProjectFilesFromServer(const QList<int> &projectIds,
                                        const QString &localDir,
                                        QString &errMsg);
    // 将本地临时目录文件上传回服务器
    bool uploadProjectFilesToServer(const QString &localDir, QString &errMsg);

    // 递归扫描目录中的压缩包并解压（上传前本地解压）
    void decompressArchivesInDir(const QString &dir);
    // 解压 tar 系列压缩包（.tar / .tar.gz / .tar.bz2 / .tar.xz）
    bool extractTar(const QString &tarPath, const QString &destDir);
    // 扁平化目录：如果任务目录下只有一个子目录且无文件，循环提升直到稳定
    // 解决 tar 归档自带根目录层级导致路径嵌套的问题
    void flattenTaskDir(const QString &dir);

    // 向日志面板追加一行
    void log(const QString &msg);

    // ----- 历史 tab -----
    QTableWidget *m_uploadTable;
    QTableWidget *m_downloadTable;

    // ----- 备份 tab -----
    QListWidget  *m_projectListWidget; // 项目列表（多选）
    QPushButton  *m_backupAllBtn;
    QPushButton  *m_backupSelBtn;
    QPushButton  *m_restoreAllBtn;
    QPushButton  *m_restoreSelBtn;
    QProgressBar *m_progressBar;
    QLabel       *m_statusLabel;
    QTextEdit    *m_logEdit;           // 操作日志

    // ----- 内部状态 -----
    QList<ProjectInfo> m_cachedProjects;
};

#endif // LOCALMANAGERPAGE_H
