cmake_minimum_required(VERSION 3.15)
project(CLoudPlatform)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(Qt5_DIR $ENV{QT_GCC_DIR})
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    # 直接使用 Qt5.12.9 的 msvc 路径，不依赖环境变量
    set(Qt5_DIR "C:/Qt/Qt5.12.9/5.12.9/msvc2017_64/lib/cmake/Qt5")
    # MSVC: use UTF-8 as source and execution character set
    add_compile_options(/utf-8)
endif()

find_package(Qt5 COMPONENTS Core Gui Widgets Network Sql REQUIRED)
SET(QT_LIBS Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Network)
list(APPEND LINK_LIBS ${QT_LIBS})

message(STATUS "qtPath:${Qt5_DIR}")


# 包含头文件目录
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/..
)

# 收集源文件
set(SOURCES
    main.cpp
    src/authmanager.cpp
    src/cosmanager.cpp
    src/logindialog.cpp
    src/mainwindow.cpp
    src/sshmanager.cpp
    src/uploadpage.cpp
    src/downloadpage.cpp
    src/usermanagedialog.cpp
    src/projectpage.cpp
    src/localmanagerpage.cpp
)

set(HEADERS
    src/authmanager.h
    src/cosmanager.h
    src/logindialog.h
    src/mainwindow.h
    src/sshmanager.h
    src/uploadpage.h
    src/downloadpage.h
    src/usermanagedialog.h
    src/projectpage.h
    src/localmanagerpage.h
)

# 设置输出目录（所有配置统一输出到 cmake/bin）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
# 多配置生成器（如 VS）需要分别设置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 添加Qt资源文件
set(RESOURCES
        "resources.qrc"
)

# Windows应用程序图标
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

# 生成可执行文件
add_executable(CLoudPlatform WIN32
        ${SOURCES}
        ${HEADERS}
        ${RESOURCES}
        ${APP_ICON_RESOURCE_WINDOWS}
)

# 设置调试版本后缀
set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX d)

# release调试生成pdb
if (CMAKE_BUILD_TYPE MATCHES "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()


# 链接库
target_link_libraries(CLoudPlatform PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
        Qt5::Sql
)

# 解决 MSVC Debug 模式下 CRT 库冲突
# 忽略冲突的库，让链接器自动选择
if (MSVC)
    set_property(TARGET CLoudPlatform APPEND_STRING PROPERTY LINK_FLAGS_DEBUG "/NODEFAULTLIB:MSVCRTD.lib")
    set_property(TARGET CLoudPlatform APPEND_STRING PROPERTY LINK_FLAGS_DEBUG "/NODEFAULTLIB:libcmtd.lib")
endif()

# 打印调试信息
message(STATUS "源文件: ${SOURCES}")
message(STATUS "头文件: ${HEADERS}")
message(STATUS "链接库: ${LINK_LIBS}")

# SSH连接配置（通过环境变量或命令行参数传入）
# 设置默认值，可以通过 cmake -DSSH_IP=xxx 等方式覆盖
set(SSH_IP "$ENV{SSH_IP}" CACHE STRING "SSH服务器IP地址")
set(SSH_USER "$ENV{SSH_USER}" CACHE STRING "SSH用户名")
set(SSH_PASS "$ENV{SSH_PASS}" CACHE STRING "SSH密码")
set(SSH_KEY "$ENV{SSH_KEY}" CACHE STRING "SSH私钥路径")

# 如果没有设置环境变量，使用默认值
if(NOT SSH_IP)
    set(SSH_IP "1.15.120.246")
endif()
if(NOT SSH_USER)
    set(SSH_USER "ubuntu")
endif()
if(NOT SSH_PASS)
    set(SSH_PASS "Supernavi2015!")
endif()
if(NOT SSH_KEY)
    set(SSH_KEY "$ENV{HOME}/.ssh/id_rsa")
endif()

message(STATUS "SSH配置 - IP: ${SSH_IP}, 用户: ${SSH_USER}")
message(STATUS "SSH密钥: ${SSH_KEY}")

# 检测可用的SSH工具
find_program(SSH_EXECUTABLE ssh)
find_program(SCP_EXECUTABLE scp)
find_program(PLINK_EXECUTABLE plink)
find_program(PSCP_EXECUTABLE pscp)
find_program(SSHPASS_EXECUTABLE sshpass)

# 自定义目标：自动连接服务器（使用SSH密钥，如果失败则使用密码）
if(SSH_EXECUTABLE)
    # 使用ssh命令连接
    add_custom_target(ssh_auto_connect
        COMMAND ${CMAKE_COMMAND} -E echo "正在自动连接服务器 ${SSH_IP}..."
        COMMAND ${SSH_EXECUTABLE} -o StrictHostKeyChecking=no -o ConnectTimeout=10 ${SSH_USER}@${SSH_IP} || (
            ${CMAKE_COMMAND} -E echo "密钥认证失败，尝试使用密码..."
            powershell -Command "Start-Process cmd -ArgumentList '/c ssh ${SSH_USER}@${SSH_IP}' -NoNewWindow -Wait"
        )
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "自动连接远程服务器"
        VERBATIM
    )
else()
    # 如果没有ssh，使用plink
    if(PLINK_EXECUTABLE)
        add_custom_target(ssh_auto_connect
            COMMAND ${CMAKE_COMMAND} -E echo "使用PuTTY plink连接服务器..."
            COMMAND ${PLINK_EXECUTABLE} -batch -pw ${SSH_PASS} ${SSH_USER}@${SSH_IP}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "使用PuTTY自动连接远程服务器"
            VERBATIM
        )
    else()
        add_custom_target(ssh_auto_connect
            COMMAND ${CMAKE_COMMAND} -E echo "错误: 未找到SSH客户端"
            COMMAND ${CMAKE_COMMAND} -E echo "请安装OpenSSH或PuTTY"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "SSH客户端未找到"
            VERBATIM
        )
    endif()
endif()

# 自定义目标：配置SSH密钥认证（自动生成并上传公钥）
add_custom_target(ssh_setup_key
    COMMAND ${CMAKE_COMMAND} -E echo "正在配置SSH密钥认证..."
    COMMAND ${CMAKE_COMMAND} -E echo "1. 检查SSH密钥是否存在..."
    COMMAND powershell -Command "if (-not (Test-Path '$ENV{HOME}\\.ssh\\id_rsa')) { ssh-keygen -t rsa -b 4096 -f '$ENV{HOME}\\.ssh\\id_rsa' -N '\"' }"
    COMMAND ${CMAKE_COMMAND} -E echo "2. 复制公钥到服务器..."
    COMMAND powershell -Command "type '$ENV{HOME}\\.ssh\\id_rsa.pub' | ssh ${SSH_USER}@${SSH_IP} 'cat >> ~/.ssh/authorized_keys'"
    COMMAND ${CMAKE_COMMAND} -E echo "3. 测试免密登录..."
    COMMAND ssh ${SSH_USER}@${SSH_IP} "echo 'SSH密钥认证配置成功！'"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "配置SSH密钥认证"
    VERBATIM
)

# 自定义目标：使用sshpass自动连接（需要安装sshpass）
if(SSHPASS_EXECUTABLE)
    add_custom_target(ssh_pass_connect
        COMMAND ${SSHPASS_EXECUTABLE} -p ${SSH_PASS} ssh -o StrictHostKeyChecking=no ${SSH_USER}@${SSH_IP}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "使用sshpass自动连接服务器"
        VERBATIM
    )
endif()

# 自定义目标：自动上传文件（使用SCP和SSH密钥）
if(SCP_EXECUTABLE)
    add_custom_target(ssh_auto_upload
        COMMAND ${CMAKE_COMMAND} -E echo "正在自动上传文件到服务器..."
        COMMAND ${SCP_EXECUTABLE} -o StrictHostKeyChecking=no -r ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/* ${SSH_USER}@${SSH_IP}:~/CloudPlatform/ || (
            ${CMAKE_COMMAND} -E echo "SCP上传失败，尝试使用PSCP..."
            ${PSCP_EXECUTABLE} -r -pw ${SSH_PASS} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/* ${SSH_USER}@${SSH_IP}:~/CloudPlatform/
        )
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "自动上传文件到服务器"
        VERBATIM
    )
endif()

# 自定义目标：自动执行远程命令
add_custom_target(ssh_remote_cmd
    COMMAND ${CMAKE_COMMAND} -E echo "请输入要执行的远程命令:"
    COMMAND ${CMAKE_COMMAND} -E echo "示例: cmake --build . --target ssh_remote_cmd -- REMOTE_CMD='ls -la'"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "执行远程命令"
    VERBATIM
)

# 创建自动连接脚本（Windows批处理）
if(WIN32)
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/ssh_auto_connect.bat"
        "@echo off\n"
        "echo 正在自动连接服务器 ${SSH_IP}...\n"
        "echo 使用用户名: ${SSH_USER}\n"
        "\n"
        "REM 检查是否存在SSH密钥\n"
        "if exist \"%USERPROFILE%\\.ssh\\id_rsa\" (\n"
        "    echo 使用SSH密钥认证连接...\n"
        "    ssh -o StrictHostKeyChecking=no ${SSH_USER}@${SSH_IP}\n"
        ") else (\n"
        "    echo 未找到SSH密钥，使用密码认证...\n"
        "    echo 密码: ${SSH_PASS}\n"
        "    ssh ${SSH_USER}@${SSH_IP}\n"
        ")\n"
        "\n"
        "pause\n"
    )
    
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/ssh_upload.bat"
        "@echo off\n"
        "echo 正在上传文件到服务器...\n"
        "echo 源目录: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\n"
        "echo 目标: ${SSH_USER}@${SSH_IP}:~/CloudPlatform/\n"
        "\n"
        "scp -r ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\\* ${SSH_USER}@${SSH_IP}:~/CloudPlatform/\n"
        "\n"
        "echo 上传完成！\n"
        "pause\n"
    )
    
    message(STATUS "已生成Windows批处理脚本:")
    message(STATUS "  ssh_auto_connect.bat - 自动连接服务器")
    message(STATUS "  ssh_upload.bat      - 自动上传文件")
endif()

# 创建自动连接脚本（Linux/Mac Shell）
if(UNIX)
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/ssh_auto_connect.sh"
        "#!/bin/bash\n"
        "echo \"正在自动连接服务器 ${SSH_IP}...\"\n"
        "echo \"使用用户名: ${SSH_USER}\"\n"
        "\n"
        "# 检查是否存在SSH密钥\n"
        "if [ -f \"$HOME/.ssh/id_rsa\" ]; then\n"
        "    echo \"使用SSH密钥认证连接...\"\n"
        "    ssh -o StrictHostKeyChecking=no ${SSH_USER}@${SSH_IP}\n"
        "else\n"
        "    echo \"未找到SSH密钥，使用密码认证...\"\n"
        "    ssh ${SSH_USER}@${SSH_IP}\n"
        "fi\n"
    )
    
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/ssh_upload.sh"
        "#!/bin/bash\n"
        "echo \"正在上传文件到服务器...\"\n"
        "echo \"源目录: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\"\n"
        "echo \"目标: ${SSH_USER}@${SSH_IP}:~/CloudPlatform/\"\n"
        "\n"
        "scp -r ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/* ${SSH_USER}@${SSH_IP}:~/CloudPlatform/\n"
        "\n"
        "echo \"上传完成！\"\n"
    )
    
    # 添加执行权限
    execute_process(COMMAND chmod +x "${CMAKE_CURRENT_SOURCE_DIR}/ssh_auto_connect.sh")
    execute_process(COMMAND chmod +x "${CMAKE_CURRENT_SOURCE_DIR}/ssh_upload.sh")
    
    message(STATUS "已生成Shell脚本:")
    message(STATUS "  ssh_auto_connect.sh - 自动连接服务器")
    message(STATUS "  ssh_upload.sh      - 自动上传文件")
endif()

# 生成SSH配置文件（用于项目文档）
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ssh_config_template.txt.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/ssh_config.txt"
    @ONLY
)

message(STATUS "=== SSH目标已添加 ===")
message(STATUS "可用目标:")
message(STATUS "  make ssh_auto_connect  - 自动连接服务器（推荐）")
message(STATUS "  make ssh_setup_key    - 配置SSH密钥认证")
if(SSHPASS_EXECUTABLE)
    message(STATUS "  make ssh_pass_connect  - 使用sshpass自动连接")
endif()
message(STATUS "  make ssh_auto_upload  - 自动上传文件到服务器")
message(STATUS "  make ssh_cmd          - 显示SSH连接信息")
if(WIN32)
    message(STATUS "  make ssh_putty         - 使用PuTTY连接服务器")
endif()
message(STATUS "\n脚本文件:")
if(WIN32)
    message(STATUS "  ssh_auto_connect.bat - 双击运行自动连接")
    message(STATUS "  ssh_upload.bat      - 双击运行自动上传")
else()
    message(STATUS "  ssh_auto_connect.sh - 运行自动连接")
    message(STATUS "  ssh_upload.sh      - 运行自动上传")
endif()
