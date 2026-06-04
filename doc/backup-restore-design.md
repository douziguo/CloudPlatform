# CloudPlatform 备份恢复功能设计文档

## 1. 概述

`LocalManagerPage` 提供项目数据的本地备份与恢复功能，支持：
- **全量备份**：将所有项目元数据（JSON）和云端文件打包为 ZIP
- **选定项目备份**：仅备份指定项目
- **全量恢复**：从 ZIP 解压后导入元数据并将文件回传云端
- **选定项目恢复**：仅恢复指定项目

## 2. 核心文件

| 文件 | 职责 |
|------|------|
| `src/localmanagerpage.h` | 类声明、信号、槽、辅助函数声明 |
| `src/localmanagerpage.cpp` | 完整实现 |
| `src/mainwindow.cpp` | 连接 `backupRestoreCompleted` 信号刷新 `ProjectPage` |
| `src/projectpage.cpp` | 删除任务时同步更新云端路径 |

## 3. 架构设计

### 3.1 云端目录结构

```
/home/ubuntu/<clientName>/
  ├── <taskName1>/        # 任务目录，包含文件
  ├── <taskName2>/
  └── ...
```

云端路径为 **两层**（client → task），无 project 层级。所有路径读写均基于此结构。

### 3.2 本地临时结构（备份/恢复工作目录）

```
<tempDir>/
  ├── projects_metadata.json       # 项目元数据
  └── files/                       # 云端文件镜像
       ├── <taskName1>/
       │    ├── file1.dat
       │    └── file2.dat
       └── <taskName2>/
            └── ...
```

### 3.3 备份流程（onBackupAll / onBackupSelected）

```
步骤1: 创建临时目录 (QTemporaryDir)
步骤2: 序列化项目元数据 → projects_metadata.json
步骤3: 若SSH已连接，下载云端文件到 files/ 目录
步骤4: compressToZip(临时目录) → 生成最终 ZIP
```

ZIP 生成使用 PowerShell `Compress-Archive`（`-CompressionLevel Optimal`），并限制超时 120s。

### 3.4 恢复流程（onRestoreAll / onRestoreSelected）

```
步骤1: extractZip(ZIP) → 临时目录
步骤2: importProjectsFromJson(projects_metadata.json) → 恢复元数据
步骤3: decompressArchivesInDir(files/) → 本地解压压缩包
步骤4: (可选) flattenTaskDir → 消除 tar 归档目录层级
步骤5: uploadProjectFilesToServer(files/) → 上传回云端
步骤6: emit backupRestoreCompleted() → 刷新 ProjectPage
```

## 4. 关键函数说明

### 4.1 downloadProjectFilesFromServer（下载）

**签名**：`bool downloadProjectFilesFromServer(const QList<int> &projectIds, const QString &localDir, QString &errMsg)`

**关键设计**：
- 不预创建目标子目录，让 pscp 自然创建一层目录。若 `mkpath` 后再 `pscp`，pscp 会在已存在目录内再嵌套一层，导致路径多级嵌套。
- 下载目标为 `localDir`（父目录），pscp 自动在其下创建 `taskName/` 子目录。

```
下载前: localDir/
下载后: localDir/<taskName>/<files>   ✅ 两层
```

### 4.2 uploadProjectFilesToServer（上传）

**签名**：`bool uploadProjectFilesToServer(const QString &localDir, QString &errMsg)`

**流程**：
1. 枚举 `localDir` 下的所有子目录（每个对应一个 task）
2. 对每个 task 目录调用 `flattenTaskDir` 消除嵌套
3. `pscp -r <localTaskDir> <host>:/home/ubuntu/<client>/` 上传
4. pscp 在远端自动创建 `<taskName>/` 子目录

```
上传前: localDir/<taskName>/<files>   (本地两层)
上传后: /home/ubuntu/<client>/<taskName>/<files>   (云端两层)
```

### 4.3 decompressArchivesInDir（解压扫描）

**签名**：`void decompressArchivesInDir(const QString &dir)`

**流程**：
1. **递归**进入所有子目录（先序）
2. 在当前目录按扩展名匹配压缩包：
   - `.zip` → `extractZip` (PowerShell `Expand-Archive`)
   - `.tar.gz` / `.tar.bz2` / `.tar.xz` / `.tgz` / `.tbz2` / `.tar` → `extractTar` (Windows `tar.exe`)
3. 解压后删除原压缩包
4. 调用 `safeMergeDirs` 将解压目录内容合并到父目录，删除空壳

### 4.4 safeMergeDirs（安全合并）

**签名**：`static bool safeMergeDirs(const QString &srcDir, const QString &destDir)`

**设计原则**：不使用 `robocopy /MOVE`（在临时目录中可能静默失败导致文件丢失）。

**实现**：
1. 优先使用 `QFile::rename`（同盘原子操作，要么全成要么全败）
2. 若 rename 失败（跨盘/权限），回退到 `QFile::copy` + `QFile::remove`
3. 目录递归处理

### 4.5 flattenTaskDir（扁平化）

**签名**：`void flattenTaskDir(const QString &dir)`

**设计原理**：tar/ZIP 归档打包时会把根目录名（如 `1/`）写入归档内部，解压后形成嵌套结构。此函数检测并消除这种冗余层级。

**处理逻辑**（循环直到稳定）：
```
while dir 下只有一个子目录 且 没有文件:
    1. QDir::rename(唯一子目录 → tmpName)     # 隔离
    2. safeMergeDirs(tmpName → dir)             # 合并内容
    3. QDir.removeRecursively(tmpName)          # 删除空壳
    4. 继续循环（可能还有嵌套）
```

带失败回滚：若 safeMergeDirs 失败，将临时目录 rename 回原名。

### 4.6 辅助函数

| 函数 | 功能 |
|------|------|
| `exportProjectsToJson` | 将项目/任务数据序列化为 JSON |
| `importProjectsFromJson` | 从 JSON 恢复项目元数据（已存在跳过） |
| `compressToZip` | PowerShell `Compress-Archive` 打包 |
| `extractZip` | PowerShell `Expand-Archive` 解压 |
| `extractTar` | Windows `tar.exe -xf` 解压 tar 系列 |
| `log` | 向操作日志面板追加带时间戳的富文本 |

## 5. 信号连接

```
LocalManagerPage::backupRestoreCompleted()
    → MainWindow 连接 → ProjectPage::refreshProjectList()
```

恢复完成（全量/选定）后自动刷新项目管理页面，无需手动操作。

## 6. 历史踩坑记录

### 6.1 路径嵌套问题

| 尝试 | 方案 | 结果 |
|------|------|------|
| 1 | `mkdir -p` 父目录，让 pscp 创建目标 | ❌ 仍嵌套 |
| 2 | `pscp dir/*` 通配符 | ❌ Windows QProcess 不展开 `*` |
| 3 | SSH 后置 `cp -r ... && rm -rf` | ❌ 导致文件丢失 |
| 4 | 上传到父目录 | ❌ 解压残留层导致嵌套 |
| 5 | `decompressArchivesInDir` + robocopy 合并 | ❌ robocopy 只处理当前目录 |
| 6 | 递归子目录 + robocopy 合并 | ❌ tar 内部目录层未被处理 |
| 7 | **新增 `flattenTaskDir` + 下载不预创建目录** | ✅ 两层结构正确 |

### 6.2 robocopy /MOVE 文件丢失

`robocopy /MOVE` 在 Windows 临时目录中存在边界条件：文件已复制但源文件未被删除时，后续 `removeRecursively()` 会误删未移动的文件。

**修复**：全文件不再使用 `robocopy /MOVE`，改为 `safeMergeDirs`（基于 `QDir::rename` + 回退 `QFile::copy`）。

### 6.3 下载时 pscp 嵌套

**根因**：`mkdir` 预创建目标目录后，`pscp` 在该目录**内部**再创建同名子目录。

**修复**：下载目标改为父目录，让 pscp 自然创建唯一一层。

## 7. 构建与运行

- **工具链**：CMake 3.15+ / MSVC 2022 / Qt 5.12.9
- **依赖工具**：`pscp.exe`、`plink.exe`（PuTTY 套件）、`tar.exe`（Windows 内置）
- **编译选项**：`/utf-8`（MSVC 源码字符集 UTF-8）

```
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## 8. 注意事项

1. **服务器脏数据清理**：若之前测试在服务器上留下了嵌套目录（如 `/client/1/1/1/`），需 SSH 手动 `rm -rf` 清理后再验证。
2. **SSH 连接前置**：备份/恢复的文件传输依赖 SSH 连接，未连接时仅处理元数据。
3. **临时目录**：所有操作在 `QTemporaryDir` 中进行，程序退出自动清理。
4. **同名项目**：恢复时已存在的同名项目被跳过（不覆盖），以保护已有数据。
5. **编码**：`tar.exe` 和 `pscp.exe` 均通过 UTF-8 路径调用（CMake `/utf-8` 编译选项）。
