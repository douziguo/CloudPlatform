# CloudPlatform 软件设计文档

> 版本 1.0 | 2026-06-04 | 测绘行业测量数据管理平台

---

## 目录

1. [项目概述](#1-项目概述)
2. [总体架构](#2-总体架构)
3. [模块设计](#3-模块设计)
4. [数据库设计](#4-数据库设计)
5. [UI 设计](#5-ui-设计)
6. [核心流程](#6-核心流程)
7. [权限体系](#7-权限体系)
8. [SSH 通信设计](#8-ssh-通信设计)
9. [COS 集成设计](#9-cos-集成设计)
10. [构建与部署](#10-构建与部署)
11. [数据格式](#11-数据格式)

---

## 1. 项目概述

### 1.1 基本信息

| 项目 | 内容 |
|------|------|
| **项目名称** | CloudPlatform |
| **应用定位** | 测绘行业测量数据管理平台（桌面客户端） |
| **技术栈** | Qt 5.12.9 / C++17 / CMake 3.15+ / MSVC 2022 |
| **数据库** | SQLite（本地持久化） |
| **远程传输** | SSH（pscp/plink）、腾讯云 COS V5 API |
| **目标平台** | Windows 10+ |
| **许可证** | Apache License 2.0 |

### 1.2 功能概要

| 功能域 | 说明 |
|--------|------|
| **用户认证** | 多角色登录、SHA-256 + salt 密码加密、记住密码 |
| **项目管理** | 项目 CRUD、测量任务管理、进度统计 |
| **文件上传** | 本地文件/拖拽上传到远程服务器、三级路径选择、测量信息自动生成 |
| **文件下载** | 远程目录浏览、批量下载、ZIP 打包、递归目录下载 |
| **本地管理** | 上传/下载历史记录、全量/选定备份恢复 |
| **用户管理** | 用户增删、密码重置、角色分配 |
| **COS 存储** | 腾讯云对象存储集成（预留） |

---

## 2. 总体架构

### 2.1 架构图

```
┌──────────────────────────────────────────────────────────┐
│                      MainWindow                          │
│  ┌─────────┬─────────┬──────────┬──────────────────┐    │
│  │ Upload  │Download │ Project  │ LocalManager     │    │
│  │ Page    │ Page    │ Page     │ Page             │    │
│  └────┬────┴────┬────┴────┬─────┴───────┬──────────┘    │
│       │         │         │             │                │
│  ┌────┴─────────┴─────────┴─────────────┴────────┐      │
│  │              AuthManager (单例)                │      │
│  │   认证 | 权限 | 项目管理 | DB CRUD | 迁移      │      │
│  └──────────────────────────┬────────────────────┘      │
│                             │                            │
│  ┌──────────────────────────┴────────────────────┐      │
│  │              SQLite (cloudplatform_users.db)   │      │
│  │  users | projects | measurement_tasks         │      │
│  │  file_uploads | file_downloads                │      │
│  └───────────────────────────────────────────────┘      │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │              SshManager (单例)                    │   │
│  │  plink/pscp 进程管理 | 命令执行 | 文件传输       │   │
│  │  ls 解析 | 中文编码 | 工具自动查找               │   │
│  └──────────────────────┬───────────────────────────┘   │
│                         │                                │
│  ┌──────────────────────┴───────────────────────────┐   │
│  │            CosManager (单例)                      │   │
│  │  COS V5 HMAC-SHA1 签名 | GET/PUT/DELETE          │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
         │                              │
    pscp/plink                    HTTPS (COS API)
         │                              │
    ┌────┴────┐                  ┌──────┴──────┐
    │ Ubuntu  │                  │ 腾讯云 COS  │
    │ Server  │                  │ 对象存储    │
    └─────────┘                  └─────────────┘
```

### 2.2 设计模式

| 模式 | 应用位置 | 说明 |
|------|---------|------|
| **单例** | AuthManager, SshManager, CosManager | 全局唯一实例，Q_GLOBAL_STATIC 或静态指针 |
| **MVC 变体** | 所有 Page 类 | QWidget 同时承担 View + Controller，AuthManager 为 Model |
| **信号槽** | 模块间通信 | Qt 原生机制，解耦模块依赖 |
| **策略模式** | SshManager::execRemoteCmd | 多策略 fallback：plink(-pw) → ssh → sshpass → SSH 密钥 |

### 2.3 类关系

```
main.cpp
  └── MainWindow (主窗口，QMainWindow)
        ├── AuthManager::instance()     → 认证数据库
        ├── SshManager::instance()      → SSH 连接
        ├── CosManager::instance()      → COS 存储
        ├── LoginDialog                 → 登录入口
        ├── UserManageDialog            → 用户管理
        ├── UploadPage                  → 上传 tab
        ├── DownloadPage                → 下载 tab
        ├── ProjectPage                 → 项目管理 tab
        └── LocalManagerPage            → 本地管理 tab
```

---

## 3. 模块设计

### 3.1 AuthManager — 认证与权限管理

**文件**：`src/authmanager.h` / `src/authmanager.cpp`

**职责**：全局用户认证、角色权限、数据库管理的核心模块。所有数据库操作均通过此类进行。

**核心接口**：

| 方法 | 说明 |
|------|------|
| `instance()` | 获取单例 |
| `login(user, pwd)` | SHA-256(salt + pwd) 验证登录，记录 last_login |
| `logout()` | 清除当前用户状态 |
| `currentUser()` | 获取当前 UserInfo |
| `isAdmin()` / `isFieldWorker()` / `isOfficeWorker()` / `isCustomer()` | 角色判断 |
| `addUser()` / `deleteUser()` / `resetPassword()` | 用户管理 |
| `getAllUsers()` | 获取用户列表 |
| `addProject()` / `deleteProject()` / `updateProject()` / `getAllProjects()` / `getProjectById()` | 项目 CRUD |
| `addTask()` / `deleteTask()` / `updateTask()` / `getTasksByProject()` | 任务 CRUD |
| `addUploadRecord()` / `getUploadRecords()` / `clearUploadRecords()` | 上传记录 |
| `addDownloadRecord()` / `getDownloadRecords()` / `clearDownloadRecords()` | 下载记录 |
| `getProjectsByUser()` | 按角色权限过滤项目列表 |

**密码安全**：
- 存储方式：`SHA-256(salt + password)` → hex
- salt：16 字节随机数（hex 存储），注册时生成
- 验证：重新计算 hash 与存储值比对

**数据库初始化**：`initDatabase()` 自动建表 + 向后兼容迁移（ALTER TABLE ADD COLUMN）。

### 3.2 SshManager — SSH 连接管理

**文件**：`src/sshmanager.h` / `src/sshmanager.cpp` / `src/sshmanager_append.cpp`

**职责**：管理与远程 Linux 服务器的 SSH 连接和文件传输。

**核心能力**：

| 功能 | 实现方式 | 说明 |
|------|---------|------|
| **连接** | plink -pw / ssh + sshpass + 密钥 fallback | 4 级 fallback 策略 |
| **断开** | kill 进程 | 通过 QProcess 管理 |
| **执行命令** | `execRemoteCmd(cmd, output)` | 同步执行，返回 exitCode |
| **异步执行** | `execRemoteCmdAsync(cmd, callback)` | 回调获取输出 |
| **文件上传** | `pscp -r local host:remote` | 递归目录上传 |
| **管道上传** | `ssh cat > remotePath` | 解决中文文件名编码（sshmanager_append.cpp） |
| **文件下载** | `plink + cat` 管道下载 / `pscp -r` 递归下载 | 单文件用管道（避免编码问题），目录用 pscp |
| **目录列表** | `ls -l` 输出解析 | 状态机解析 long-iso 格式 |
| **删除** | `rm -rf`（带路径安全校验） | 防止误删根目录 |
| **文件存在** | `test -f` | 快速检测 |

**工具查找优先级**：程序目录 → CWD → PATH → PuTTY 安装目录

**关键信号**：
- `connectionEstablished()` / `connectionLost()` / `connectionError(QString)`
- `remoteDirListed(QString, QStringList)` — 目录浏览结果

**编码处理**：
- `plink + cat` 管道下载避免 pscp 中文文件名乱码
- `ls -l` 输出用 ANSI 转义码剥离状态机清理后解析

### 3.3 CosManager — 腾讯云 COS 集成

**文件**：`src/cosmanager.h` / `src/cosmanager.cpp`

**职责**：腾讯云对象存储（COS）V5 API 的完整客户端实现。

**核心能力**：

| 功能 | API | 方法 |
|------|-----|------|
| 列举 Bucket | GET / | `listBuckets()` |
| 列举对象 | GET /{bucket} | `listObjects(bucket, prefix, delimiter)` |
| 上传文件 | PUT /{bucket}/{key} | `uploadFile(bucket, key, localPath)` |
| 下载文件 | GET /{bucket}/{key} | `downloadFile(bucket, key, localPath)` |
| 获取元数据 | HEAD /{bucket}/{key} | `headObject(bucket, key)` |
| 删除对象 | DELETE /{bucket}/{key} | `deleteObject(bucket, key)` |

**签名算法**：HMAC-SHA1，构造 Authorization Header，包含 SecretId/Signature/ExpireTime 等字段。

**XML 解析**：QXmlStreamReader 解析 ListBucketResult 和 ListAllMyBucketsResult。

**状态**：目前作为预留模块，UI 尚未直接集成调用。可供未来扩展使用。

### 3.4 页面模块一览

| 模块 | 文件 | 核心功能 |
|------|------|---------|
| **MainWindow** | `mainwindow.h/.cpp` | 主窗口框架、自定义标题栏、tab 管理、角色权限控制、日志面板 |
| **LoginDialog** | `logindialog.h/.cpp` | 登录验证、用户管理 tab、记住密码(QSettings) |
| **UploadPage** | `uploadpage.h/.cpp` | 文件拖拽上传、三级路径选择(client/project/task)、并发队列(max=3)、进度追踪、测量信息自动生成 |
| **DownloadPage** | `downloadpage.h/.cpp` | 远程目录浏览(QTreeWidget)、多选下载、递归下载、ZIP 打包、右键菜单、权限路径限制 |
| **ProjectPage** | `projectpage.h/.cpp` | 项目/任务 CRUD、任务状态管理、进度统计、远程目录同步删除、权限过滤 |
| **LocalManagerPage** | `localmanagerpage.h/.cpp` | 上传/下载历史记录表、全量/选定备份恢复、压缩包自动解压、扁平化目录 |
| **UserManageDialog** | `usermanagedialog.h/.cpp` | 用户列表管理、添加/删除/重置密码、角色/客户分配、权限保护 |

### 3.5 信号槽通信

```
┌────────────────┐      connectionEstablished/Lost/Error       ┌────────────┐
│  SshManager    │ ──────────────────────────────────────────→ │ MainWindow │
└────────────────┘                                             └─────┬──────┘
                                                                     │
┌────────────────┐      remoteDirListed(path, items)                 │
│  SshManager    │ ──────────────────────────────────────────→ DownloadPage
└────────────────┘                                             (onRemoteDirListed)
                                                                     │
┌────────────────┐      projectsChanged                              │
│  ProjectPage   │ ──────────────────────────────────────────→ UploadPage
└────────────────┘                                             (refreshProjectCombo)
                                                                     │
┌────────────────┐      backupRestoreCompleted                       │
│LocalManagerPage│ ──────────────────────────────────────────→ ProjectPage
└────────────────┘                                             (refreshProjectList)
                                                                     │
┌────────────────┐      loginSuccess / loginFailed                   │
│ LoginDialog    │ ──────────────────────────────────────────→ accept() / reject()
└────────────────┘
```

---

## 4. 数据库设计

### 4.1 数据库文件

- **存储路径**：`QStandardPaths::AppDataLocation + "/cloudplatform_users.db"`
- **引擎**：SQLite 3（通过 Qt5::Sql QSQLITE 驱动）

### 4.2 ER 图

```
┌──────────┐       ┌───────────────┐       ┌────────────────────┐
│  users   │       │   projects    │       │ measurement_tasks  │
├──────────┤       ├───────────────┤       ├────────────────────┤
│ id (PK)  │       │ id (PK)       │       │ id (PK)            │
│ username │       │ name          │   ┌───│ project_id (FK)    │
│ pwd_hash │       │ code          │   │   │ task_name          │
│ salt     │       │ location      │   │   │ measure_date       │
│ role     │       │ client        │   │   │ personnel          │
│ client   │       │ client_name   │   │   │ pipe_start/end     │
│ created  │       │ description   │   │   │ pipe_material      │
│ last_    │       │ notes         │   │   │ pipe_diameter      │
│ login    │       │ created_by    │   │   │ pipe_length        │
└──────────┘       │ created_at    │   │   │ status             │
                   │ updated_at    │   │   │ progress           │
                   └───────────────┘   │   │ notes              │
                                       │   │ created_at         │
┌──────────────┐                       │   │ updated_at         │
│ file_uploads │                       │   └────────────────────┘
├──────────────┤                       │         1 : N
│ id (PK)      │                       └──────────────────────────
│ username     │
│ remote_path  │       ┌────────────────┐
│ file_name    │       │ file_downloads │
│ uploaded_at  │       ├────────────────┤
└──────────────┘       │ id (PK)        │
                       │ username       │
                       │ remote_path    │
                       │ file_name      │
                       │ local_path     │
                       │ file_size      │
                       │ downloaded_at  │
                       └────────────────┘
```

### 4.3 表结构 SQL

```sql
-- 用户表
CREATE TABLE users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    salt          TEXT NOT NULL,
    role          INTEGER NOT NULL DEFAULT 1,
    client_name   TEXT DEFAULT '',
    created_at    TEXT NOT NULL,
    last_login    TEXT
);

-- 项目表
CREATE TABLE projects (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    code        TEXT,
    location    TEXT,
    client      TEXT,
    client_name TEXT DEFAULT '',
    description TEXT,
    notes       TEXT,
    created_by  TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

-- 测量任务表
CREATE TABLE measurement_tasks (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id     INTEGER NOT NULL,
    task_name      TEXT NOT NULL,
    measure_date   TEXT,
    personnel      TEXT,
    pipe_start     TEXT,
    pipe_end       TEXT,
    pipe_material  TEXT,
    pipe_diameter  TEXT,
    pipe_length    TEXT,
    status         TEXT DEFAULT 'pending',
    progress       INTEGER DEFAULT 0,
    notes          TEXT,
    created_at     TEXT NOT NULL,
    updated_at     TEXT NOT NULL,
    FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE
);

-- 上传记录表
CREATE TABLE file_uploads (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT NOT NULL,
    remote_path TEXT NOT NULL,
    file_name   TEXT NOT NULL,
    uploaded_at TEXT NOT NULL
);

-- 下载记录表
CREATE TABLE file_downloads (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT NOT NULL,
    remote_path   TEXT NOT NULL,
    file_name     TEXT NOT NULL,
    local_path    TEXT,
    file_size     INTEGER DEFAULT 0,
    downloaded_at TEXT NOT NULL
);
```

### 4.4 数据库迁移

`AuthManager::initDatabase()` 使用 ALTER TABLE 实现向后兼容迁移：

```sql
ALTER TABLE users    ADD COLUMN client_name TEXT DEFAULT '';
ALTER TABLE projects ADD COLUMN client_name TEXT DEFAULT '';
```

---

## 5. UI 设计

### 5.1 整体布局

```
┌──────────────────────────────────────────────────────┐
│  [Logo]  CloudPlatform                 [─] [□] [×]  │  ← 自定义标题栏
├──────────────────────────────────────────────────────┤
│  [SSH ●] [断开]  [用户名 ▼] [登出] [用户管理]       │  ← 工具栏
├──────────────────────────────────────────────────────┤
│  [文件上传] [文件下载] [项目管理] [本地管理]         │  ← QTabWidget
│  ┌────────────────────────────────────────────────┐  │
│  │                                                │  │
│  │              页面内容区                         │  │
│  │                                                │  │
│  └────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────┤
│  拽动手柄 ═══════════════════════════════════════    │  ← QSplitter
├──────────────────────────────────────────────────────┤
│  [10:23:45] SSH 连接已建立                           │  ← 日志面板
│  [10:23:50] 文件上传完成                             │
└──────────────────────────────────────────────────────┘
```

### 5.2 页面布局

#### 文件上传页 (UploadPage)

```
┌──────────────────┬──────────────────────────┐
│  拖拽区域        │  目标路径选择            │
│  (Drop Zone)     │  [客户 ▼]               │
│  [添加文件]      │  [项目 ▼]               │
│  [清空列表]      │  [任务 ▼]               │
│  [列表/卡片]     │                          │
│                  │  远程路径预览            │
├──────────────────┤                          │
│  文件列表        │  /client/project/task    │
│  ┌────────────┐  │                          │
│  │ file1.jot  │  │  [开始上传]             │
│  │ file2.txt  │  │                          │
│  │ ...        │  │  进度条 ████░░ 60%      │
│  └────────────┘  │                          │
└──────────────────┴──────────────────────────┘
```

#### 文件下载页 (DownloadPage)

```
┌──────────────────────────────────────────────┐
│  路径: [/home/ubuntu/client] [进入] [返回]   │
├──────────────────────────────────────────────┤
│  远程目录树 (QTreeWidget)                    │
│  ├─ client1/                                 │
│  │  ├─ task1/                                │
│  │  │  ├─ file1.jot                          │
│  │  │  └─ file2.txt                          │
│  │  └─ task2/                                │
│  └─ client2/                                 │
├──────────────────────────────────────────────┤
│  [全选] [反选] [下载选中] [下载目录]         │
│  进度: ████████░░ 80%                        │
└──────────────────────────────────────────────┘
```

#### 项目管理页 (ProjectPage)

```
┌──────────┬───────────────────────────────────┐
│ 项目列表 │  项目详情                          │
│ ┌──────┐ │  名称: [___________]              │
│ │项目1 │ │  编号: [___________]              │
│ │项目2 │ │  地点: [___________]              │
│ │项目3 │ │  委托: [___________]              │
│ └──────┘ │                                    │
│ [新增]   │  ┌─ 测量任务 ──────────────────┐  │
│ [删除]   │  │ 名称│日期│人员│起终点│...│  │
│          │  │  t1 │... │    │       │   │  │
│          │  │  t2 │... │    │       │   │  │
│          │  └────────────────────────────┘  │
│          │  进度: 已完成 3/5 (60%)           │
│          │  [新增任务] [编辑] [删除]         │
└──────────┴───────────────────────────────────┘
```

#### 本地管理页 (LocalManagerPage)

```
┌─ 历史记录 ────┬─ 备份恢复 ──────────────────┐
│ 上传记录表     │  项目列表 (多选)             │
│ ┌───────────┐ │  ┌────────────────────┐      │
│ │时间│路径..│ │  │ ☑ 项目1            │      │
│ │... │ ...  │ │  │ ☐ 项目2            │      │
│ └───────────┘ │  │ ☐ 项目3            │      │
│                │  └────────────────────┘      │
│ 下载记录表     │  [全量备份] [备份选定]       │
│ ┌───────────┐ │  [全量恢复] [恢复选定]       │
│ │时间│路径..│ │                                │
│ │... │ ...  │ │  进度: ████░░░░ 50%          │
│ └───────────┘ │  状态: 正在备份...            │
│                │                                │
│ [清空] [导出] │  ┌─ 操作日志 ────────────┐   │
│                │  │ [16:23:45] 备份完成   │   │
│                │  │ [16:23:30] 打包 ZIP  │   │
│                │  └───────────────────────┘   │
└────────────────┴──────────────────────────────┘
```

### 5.3 样式系统

**文件**：`rec/style.qss`（541 行）

**设计调色板**：

| 用途 | 颜色 | 说明 |
|------|------|------|
| 主色 | `#373E65` | 深蓝灰，标题栏 / 工具栏 / 选中状态 |
| 辅色 | `#E0ECF8` / `#D0DCE8` | 浅蓝灰，按钮 / 输入框背景 |
| 强调 | `#505880` | 中蓝灰，hover 状态 |
| 危险 | `#E81123` | 关闭按钮 hover 色 |
| 文字 | `#FFFFFF`（深底）/ `#333333`（浅底） | |
| 字体 | Microsoft YaHei / Segoe UI, 13px | |

---

## 6. 核心流程

### 6.1 登录流程

```
启动应用
  │
  ▼
MainWindow 显示 → 检查记住密码
  │
  ▼
弹 LoginDialog
  │
  ├─ 登录 Tab:  输入用户名/密码 → AuthManager::login()
  │    ├─ 成功 → accept() → MainWindow 设置用户信息
  │    │         ├─ SSH 自动连接
  │    │         └─ 启用 tab 页
  │    └─ 失败 → 显示错误提示
  │
  └─ 用户管理 Tab: 仅 Admin/Customer 可见
       ├─ 添加用户 → AuthManager::addUser()
       ├─ 删除用户 → AuthManager::deleteUser()
       └─ 管理员行禁止删除
```

### 6.2 文件上传流程

```
1. 用户拖入文件 / 点击"添加文件"
2. 选择三级路径: client → project → task
3. 点击"开始上传"
4. 遍历上传队列（最大 3 并发）
   ├─ 对每个文件: pscp localPath ubuntu@host:/remotePath
   ├─ 上传完成 → notifyUpload(remotePath, fileName) 记录到 DB
   └─ 自动生成测量信息 txt 文件一并上传
5. 进度条更新
6. 全部完成 → 刷新历史记录
```

### 6.3 文件下载流程

```
1. SSH 连接后 → SshManager 执行 ls -l 获取目录列表
2. 解析 ls 输出 → 填充 QTreeWidget
3. 用户浏览目录树
4. 选择文件/目录 → 右键菜单 / 按钮
   ├─ 单文件下载: plink + cat 管道 → 本地文件
   ├─ 多文件下载: 逐个下载
   └─ 目录下载: pscp -r 递归下载
5. 批量下载完成 → PowerShell Compress-Archive 打包 ZIP
6. 下载完成 → notifyDownload() 记录到 DB
```

### 6.4 备份恢复流程

```
┌─ 备份 ──────────────────────────────────────┐
│ 1. 获取项目列表（全量或选定）               │
│ 2. exportProjectsToJson() → 元数据 JSON     │
│ 3. downloadProjectFilesFromServer()         │
│    → pscp -r 下载云端目录到本地 temp       │
│ 4. compressToZip(tempDir, backup.zip)       │
│    → PowerShell Compress-Archive           │
│ 5. 保存 ZIP → 用户选择保存路径             │
└─────────────────────────────────────────────┘

┌─ 恢复 ──────────────────────────────────────┐
│ 1. 用户选择 ZIP 文件                       │
│ 2. extractZip(zip, tempDir)                │
│    → PowerShell Expand-Archive            │
│ 3. importProjectsFromJson()                │
│    → 恢复元数据（同名跳过不覆盖）           │
│ 4. decompressArchivesInDir(tempDir)        │
│    → 递归扫描 .zip/.tar.gz/.tar.bz2/.tar.xz│
│    → 本地解压 + safeMergeDirs 合并         │
│ 5. flattenTaskDir() 消除 tar 归档嵌套层级  │
│ 6. uploadProjectFilesToServer()            │
│    → pscp -r 上传解压后内容到云端         │
│ 7. emit backupRestoreCompleted()           │
│    → ProjectPage::refreshProjectList()     │
└─────────────────────────────────────────────┘
```

**关键设计决策**：

| 问题 | 解决方案 |
|------|---------|
| tar 归档自带根目录层 | `flattenTaskDir()` 检测单子目录+无文件 → safeMergeDirs 提升 |
| pscp 目标目录已存在时嵌套 | 下载时不预创建目录，pscp 自然创建一层 |
| robocopy /MOVE 在 temp 目录丢失文件 | 替换为 `safeMergeDirs()` — QDir::rename 同盘原子操作 |
| 云端无法解析压缩包 | 恢复时本地解压后再上传 |
| 恢复后项目管理不刷新 | backupRestoreCompleted 信号 → refreshProjectList |

### 6.5 SSH 连接流程

```
SshManager::connectToHost(host, user, pwd, port)
  │
  ├─ 验证参数合法性
  ├─ 查找 plink.exe / pscp.exe
  │
  ├─ 策略1: plink -pw → 直接密码连接
  │   └─ 成功 → emit connectionEstablished()
  │
  ├─ 策略2: ssh 密钥认证
  │   └─ 成功 → emit connectionEstablished()
  │
  ├─ 策略3: sshpass + ssh
  │   └─ 成功 → emit connectionEstablished()
  │
  └─ 全部失败 → emit connectionError(msg)
```

---

## 7. 权限体系

### 7.1 角色定义

| 角色 | 枚举值 | 说明 |
|------|--------|------|
| **超级管理员 (Admin)** | 0 | 所有权限，查看所有项目 |
| **外业人员 (FieldWorker)** | 1 | 仅上传 + 查看本公司项目 |
| **内业人员 (OfficeWorker)** | 2 | 上传/下载 + 查看本公司项目 |
| **客户管理员 (Customer)** | 3 | 上传/下载/用户管理(本公司) + 查看本公司项目 |

### 7.2 权限矩阵

| 功能 | Admin | Customer | OfficeWorker | FieldWorker |
|------|:-----:|:--------:|:------------:|:-----------:|
| 文件上传 | ✓ | ✓ | ✓ | ✓ |
| 文件下载 | ✓ | ✓ | ✓ | ✗ |
| 项目查看 | 全部 | 本公司 | 本公司 | 本公司 |
| 项目管理 | ✓ | ✓ | ✓ | ✗ |
| 远程删除文件 | ✓ | ✓ | ✗ | ✗ |
| 用户管理 | 全部 | 本公司 | ✗ | ✗ |
| 备份恢复 | ✓ | ✓ | ✗ | ✗ |

### 7.3 数据隔离

通过 `client_name` 字段实现数据隔离：
- 用户表：每个用户绑定 `client_name`（所属客户）
- 项目表：每个项目绑定 `client_name`
- Admin 查询所有项目，其他角色仅查询 `client_name == currentUser().client_name` 的项目
- 上传/下载的目标路径限制在对应 client 目录下

### 7.4 权限应用点

- **MainWindow::applyRolePermissions()**：控制 tab 页显隐和全局按钮状态
- **AuthManager::getProjectsByUser()**：按角色返回过滤后的项目列表
- **DownloadPage**：非 Admin 严格限制在 `m_allowedRootPath` 路径内
- **LoginDialog**：用户管理 tab 仅 Admin/Customer 可见

---

## 8. SSH 通信设计

### 8.1 工具链

| 工具 | 用途 | 路径 |
|------|------|------|
| `pscp.exe` | SCP 文件传输（上传、递归下载目录） | `bin/pscp.exe` |
| `plink.exe` | SSH 命令行（连接、远程命令、管道传输） | `bin/plink.exe` |
| `tar.exe` | 压缩包解压（Windows 系统内置） | 系统 PATH |

### 8.2 远程命令执行

```cpp
// 同步执行
int execRemoteCmd(const QString &cmd, QString &output);

// 异步执行
void execRemoteCmdAsync(const QString &cmd,
                        std::function<void(int, const QString &)> callback);
```

### 8.3 文件传输编码策略

| 场景 | 方式 | 编码考量 |
|------|------|---------|
| 单文件下载 | `plink + cat` 管道 | 避免 pscp 中文乱码，直接读取二进制 |
| 目录下载 | `pscp -r` | 递归下载，UTF-8 文件名 |
| 文件上传 | `pscp` | 本地路径用 `QDir::toNativeSeparators` |
| `ls -l` 解析 | ANSI 剥离状态机 | 清理颜色码后逐字段解析 |

### 8.4 目录列表解析

**输入**（`ls -l` 输出）：
```
drwxr-xr-x 2 ubuntu ubuntu 4096 May 26 10:00 秦伦北斗
-rw-r--r-- 1 ubuntu ubuntu 1234 May 26 10:01 file.jot
```

**解析流程**：
1. ANSI 转义码剥离状态机清理
2. 按行分割
3. 正则匹配 long-iso 格式：`[d-]... <size> <date> <time> <name>`
4. 格式化为目录条目列表

---

## 9. COS 集成设计

### 9.1 API 端点

| 操作 | 方法 | 路径 |
|------|------|------|
| 列举 Bucket | GET | `http://service.cos.myqcloud.com/` |
| 列举对象 | GET | `http://{bucket}.cos.{region}.myqcloud.com/` |
| 上传 | PUT | `http://{bucket}.cos.{region}.myqcloud.com/{key}` |
| 下载 | GET | `http://{bucket}.cos.{region}.myqcloud.com/{key}` |
| 元数据 | HEAD | `http://{bucket}.cos.{region}.myqcloud.com/{key}` |
| 删除 | DELETE | `http://{bucket}.cos.{region}.myqcloud.com/{key}` |

### 9.2 签名流程

```
1. 构造 SignTime: "start;end"
2. 计算 SignKey: HMAC-SHA1(SecretKey, SignTime)
3. 构造 HttpString: "{method}\n{path}\n{params}\n{headers}\n"
4. 计算 StringToSign: "sha1\n{SignTime}\n{SHA1(HttpString)}\n"
5. 计算 Signature: HMAC-SHA1(SignKey, StringToSign)
6. 组装 Authorization Header
```

### 9.3 状态

CosManager 已完成完整实现，但目前 UI 层尚未集成调用。可作为未来云存储功能的扩展入口。

---

## 10. 构建与部署

### 10.1 构建环境

| 组件 | 版本/路径 |
|------|----------|
| 编译器 | MSVC 2022 (Visual Studio 17) |
| CMake | ≥ 3.15 |
| Qt | 5.12.9 / msvc2017_64 (`C:/Qt/Qt5.12.9/5.12.9/msvc2017_64`) |
| Qt 模块 | Core, Gui, Widgets, Network, Sql |

### 10.2 构建命令

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### 10.3 CMakeLists.txt 关键配置

| 配置项 | 说明 |
|--------|------|
| `CMAKE_CXX_STANDARD 17` | C++17 标准 |
| `AUTOUIC/AUTOMOC/AUTORCC ON` | Qt 元对象/资源自动处理 |
| `WIN32` | Windows GUI 应用（无控制台） |
| `DEBUG_POSTFIX d` | Debug 构建添加 d 后缀 |
| `Release /Zi /DEBUG` | Release 同时输出 PDB 符号 |
| `/NODEFAULTLIB:MSVCRTD.lib / libcmtd.lib` | 解决 MSVC Debug CRT 冲突 |
| `/utf-8` | MSVC 源文件和执行字符集 UTF-8 |
| 输出目录 | 统一到 `bin/` |

### 10.4 运行时部署

`bin/` 目录包含完整可执行环境：
- `CLoudPlatform.exe` / `CLoudPlatformd.exe` — 可执行文件
- `plink.exe` / `pscp.exe` — PuTTY SSH 工具
- `Qt5*.dll` — Qt 运行时库
- `platforms/` — Windows 平台插件 (`qwindows.dll`)
- `sqldrivers/` — SQLite 驱动 (`qsqlite.dll`)
- `imageformats/` — 图像格式插件
- `styles/` — Qt 样式插件

### 10.5 SSH 辅助目标（CMake Custom Targets）

| Target | 说明 |
|--------|------|
| `ssh_auto_connect` | 自动连接服务器（密钥优先，失败回退密码） |
| `ssh_setup_key` | 自动生成 RSA 密钥并上传公钥 |
| `ssh_pass_connect` | 使用 sshpass 连接（需安装） |
| `ssh_auto_upload` | 自动上传 bin/ 到服务器 |
| `ssh_remote_cmd` | 执行远程命令 |

---

## 11. 数据格式

### 11.1 云端目录结构

```
/home/ubuntu/
  └── <clientName>/           # 客户根目录
        ├── <taskName1>/       # 任务 1 数据
        │     ├── 1234.jot
        │     ├── 1234.txt
        │     ├── seg1.sin
        │     └── ...
        └── <taskName2>/       # 任务 2 数据
              └── ...
```

路径层级：**两层**（`clientName/taskName/`），不含 project 层级。

### 11.2 本地缓存目录

```
%AppData%/CloudPlatform/
  └── cloudplatform_users.db    # SQLite 数据库

程序运行目录/files/             # 备份恢复临时目录（用完清理）
  └── <taskName>/
```

### 11.3 测量数据格式

| 扩展名 | 格式 | 说明 |
|--------|------|------|
| `.jot` | JOT | 测量数据记录 |
| `.tin` | TIN | 三角网数据（大文件） |
| `.sin` | SIN | 波形/测量数据 |
| `.ahr` | AHR | 测量辅助数据 |
| `.Weld` | Weld | 焊接检查数据 |
| `.prj` | PRJ | 项目配置 |
| `.txt` | 文本 | 测量文本/坐标数据 |

---

## 附录 A：关键代码清单

| 文件 | 行数（约） | 说明 |
|------|-----------|------|
| `main.cpp` | ~30 | 应用入口 |
| `mainwindow.h/.cpp` | ~500 | 主窗口框架 |
| `authmanager.h/.cpp` | ~1200 | 认证+数据库核心 |
| `sshmanager.h/.cpp` | ~900 | SSH 通信 |
| `sshmanager_append.cpp` | ~200 | SSH 管道上传 |
| `cosmanager.h/.cpp` | ~600 | COS 客户端 |
| `uploadpage.h/.cpp` | ~800 | 文件上传页 |
| `downloadpage.h/.cpp` | ~700 | 文件下载页 |
| `projectpage.h/.cpp` | ~800 | 项目管理页 |
| `localmanagerpage.h/.cpp` | ~1400 | 本地管理页（含备份恢复） |
| `logindialog.h/.cpp` | ~400 | 登录对话框 |
| `usermanagedialog.h/.cpp` | ~250 | 用户管理对话框 |
| `style.qss` | 541 | 全局 QSS 样式表 |

## 附录 B：开发笔记

### B.1 中文编码注意事项
- MSVC 编译使用 `/utf-8` 确保源文件 UTF-8
- SSH 通信中文件传输优先使用管道方式避免 pscp 中文乱码
- `QString::fromLocal8Bit()` / `QString::fromUtf8()` 按场景选择

### B.2 pscp 行为注意事项
- `pscp -r dir host:target` 会创建 `target/dir/` 子目录
- 若 target 目录已存在，pscp 会在内部再嵌套一层
- 解决方案：始终上传到父目录，让 pscp 自然创建

### B.3 PowerShell ZIP 操作
- 压缩：`Compress-Archive -Path source\* -DestinationPath out.zip`
- 解压：`Expand-Archive -Path in.zip -DestinationPath dest -Force`
- 注意：参数中路径含空格时需用双引号包裹

### B.4 故障排查指南
| 现象 | 可能原因 | 检查点 |
|------|---------|--------|
| 路径多层嵌套 | 下载时预创建目录 | `downloadProjectFilesFromServer` 中的 `mkpath` |
| 恢复后文件丢失 | robocopy /MOVE 边界问题 | 改用 `safeMergeDirs` |
| 中文文件名乱码 | pscp 编码 | 用 plink+cat 管道替代 |
| SSH 连接失败 | 工具未找到 | 检查 PATH / PuTTY 安装 / 程序目录 |
