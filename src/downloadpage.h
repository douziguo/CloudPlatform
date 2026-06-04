#ifndef DOWNLOADPAGE_H
#define DOWNLOADPAGE_H

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMutex>

#include "sshmanager.h"

class DownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadPage(QWidget *parent = nullptr);

public slots:
    // 刷新远程文件列表
    void refreshFileList();

    // 登录/角色变更后刷新权限配置（由 MainWindow 调用）
    void refreshPermissions();

    // 退出登录时重置所有状态
    void reset();

    // 远程目录列表就绪
    void onRemoteDirListed(const QString &path, const QList<RemoteFileInfo> &fileList);

private slots:
    void onRefreshClicked();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onDownloadSingle();
    void onDownloadSelected();
    void onDeleteRemoteFile(const QString &remotePath, const QString &fileName, bool isDir = false);
    void onDownloadFolder(const QString &remoteDirPath);
    void onSelectAll();
    void onDeselectAll();
    void onTreeContextMenu(const QPoint &pos);
    void onGoUpClicked();
    void onPathEditReturnPressed();
    // 下载进度和完成信号
    void onDownloadProgress(const QString &key, qint64 received, qint64 total);
    void onDownloadFinished(const QString &key, bool success, const QString &message);

private:
    void initUI();
    void updateSelectionInfo();
    void downloadSingleFile(const QString &remotePath, qint64 fileSize = -1);
    QString formatFileSize(qint64 bytes);

    // 批量下载完成后打包为 ZIP（Windows: 调用 PowerShell Compress-Archive）
    void packageToZip(const QStringList &localFiles, const QString &zipSavePath);

    // UI控件
    QLabel *m_pathLabel;
    QPushButton *m_goUpBtn;         // 返回上级按钮
    QLineEdit *m_pathEdit;          // 路径输入框
    QPushButton *m_goBtn;           // 前往按钮
    QTreeWidget *m_fileTree;
    QPushButton *m_refreshBtn;
    QPushButton *m_selectAllBtn;
    QPushButton *m_deselectAllBtn;
    QPushButton *m_downloadBtn;
    QLabel *m_selectionInfo;
    QProgressBar *m_downloadProgressBar;
    QLabel *m_progressLabel;
    QLabel *m_resultLabel;

    // 数据
    QMutex m_downloadMutex;

    // 下载追踪
    int m_downloadingCount;
    int m_downloadedCount;
    int m_downloadFailCount;
    bool m_isDownloading;

    // 权限根路径（非超级管理员被限制在此目录下）
    QString m_allowedRootPath;
    bool m_permissionsInitialized = false;
    
    // 批量下载相关
    QStringList m_pendingDownloads;      // 待下载的远程路径列表
    QList<qint64> m_pendingDownloadSizes; // 对应每个文件的大小
    int m_currentDownloadIndex;          // 当前下载索引
    QString m_batchSaveDir;              // 批量下载保存目录（临时目录或目标目录）
    QString m_batchZipSavePath;          // ZIP 压缩包最终保存路径（空表示不打包）
    QStringList m_downloadedLocalPaths;  // 已成功下载的本地路径（用于打包）
    QString m_singleDownloadSavePath;    // 单个下载保存路径
    QString m_singleDownloadRemotePath; // 单个下载远程路径
    
    // 内部方法
    void downloadNextFile();

    // 严格路径权限检查（防止 com1 匹配 com10 等前缀问题）
    bool isPathAllowed(const QString &path) const;

    // 文件夹下载状态
    bool m_isFolderDownload = false;
    QString m_folderDownloadSavePath;
};

#endif // DOWNLOADPAGE_H
