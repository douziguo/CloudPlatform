# CloudPlatform

测绘行业测量数据管理平台，基于 Qt 5.12.9 / C++17 / CMake / MSVC 2022 构建。

## 功能模块

| 模块 | 文件 | 说明 |
|------|------|------|
| 登录认证 | `logindialog.cpp` | 用户登录、角色权限控制 |
| 项目管理 | `projectpage.cpp` | 项目的 CRUD、数据隔离（按 client_name） |
| 文件上传 | `uploadpage.cpp` | SSH（pscp）上传文件到服务器 |
| 文件下载 | `downloadpage.cpp` | SSH 下载、文件浏览 |
| 本地管理 | `localmanagerpage.cpp` | 备份恢复、历史记录 |
| 用户管理 | `usermanagedialog.cpp` | 管理员用户管理 |
| COS 集成 | `cosmanager.cpp` | 腾讯云 COS 存储集成 |
| SSH 管理 | `sshmanager.cpp` | pscp/plink 连接管理 |

## 云端目录结构

```
/home/ubuntu/<clientName>/
  ├── <taskName1>/     # 任务文件
  └── <taskName2>/     # 任务文件
```

## 备份恢复

详见 `doc/backup-restore-design.md`。

## 构建

```
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

输出至 `bin/` 目录。

## 文档

- `doc/software-design.md` — 软件整体设计文档（架构、模块、数据库、权限）
- `doc/backup-restore-design.md` — 备份恢复功能设计文档
