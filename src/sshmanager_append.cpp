// ========================================================================
// 通过 SSH 管道上传文件（不依赖 pscp/scp）
// 使用 ssh 远程执行 "cat > remotePath"，并通过 stdin 管道写入文件内容
// ========================================================================
bool SshManager::uploadFileViaSsh(const QString &localPath, const QString &remotePath)
{
    if (!m_isConnected) {
        emit uploadFinished(localPath, false, "SSH未连接");
        return false;
    }

    // 先创建远程目录
    QString remoteDir = QFileInfo(remotePath).path();
    if (!remoteDir.isEmpty() && remoteDir != ".") {
        QString mkdirCmd = QString("mkdir -p \"%1\"").arg(remoteDir);
        qDebug() << "[SshManager] [ssh-upload] 创建远程目录:" << remoteDir;
        executeCommand(mkdirCmd, true);
    }

    QString sshPath = QStandardPaths::findExecutable("ssh");
    if (sshPath.isEmpty()) {
        emit uploadFinished(localPath, false, "未找到 ssh 工具，请安装 OpenSSH 或 PuTTY");
        return false;
    }

    qDebug() << "[SshManager] [ssh-upload] 开始上传:" << localPath << "->" << remotePath;

    QProcess *proc = new QProcess(this);
    proc->setProgram(sshPath);

    QStringList args;
    if (!m_plinkPath.isEmpty()) {
        // 使用 plink.exe -pw 方式（非交互，可自动输密码）
        args << "-batch" << "-pw" << m_password
             << "-P" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << QString("cat > '%1'").arg(remotePath);
    } else if (m_useKeyAuth) {
        // 使用密钥认证（无需密码）
        args << "-o" << "StrictHostKeyChecking=no"
             << "-p" << QString::number(m_port)
             << QString("%1@%2").arg(m_user, m_host)
             << QString("cat > '%1'").arg(remotePath);
    } else {
        // 需要密码认证，但无法非交互式输入
        // 使用 sshpass（如果可用）或提示用户
        QString sshpassPath = QStandardPaths::findExecutable("sshpass");
        if (!sshpassPath.isEmpty()) {
            proc->setProgram(sshpassPath);
            args << "-p" << m_password
                 << "ssh"
                 << "-o" << "StrictHostKeyChecking=no"
                 << "-p" << QString::number(m_port)
                 << QString("%1@%2").arg(m_user, m_host)
                 << QString("cat > '%1'").arg(remotePath);
        } else {
            delete proc;
            emit uploadFinished(localPath, false,
                "密码认证需要 plink.exe/pscp.exe 或 sshpass 工具。\n"
                "请下载 PuTTY 工具集：\n"
                "https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html");
            return false;
        }
    }

    proc->setArguments(args);

    // 获取文件大小用于进度显示
    qint64 fileSize = QFileInfo(localPath).size();
    emit uploadProgress(localPath, 0, fileSize);

    // 连接信号
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, localPath, proc, fileSize](int exitCode, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        bool success = (exitCode == 0);
        QString errMsg = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (!success && errMsg.isEmpty()) {
            errMsg = QString("进程退出码: %1").arg(exitCode);
        }
        emit uploadProgress(localPath, fileSize, fileSize);
        emit uploadFinished(localPath, success, errMsg);
        m_uploadProcesses.remove(localPath);
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred,
            this, [this, localPath, proc](QProcess::ProcessError error) {
        if (!m_uploadProcesses.contains(localPath)) return;
        QString errMsg;
        switch (error) {
            case QProcess::FailedToStart:
                errMsg = "ssh 工具启动失败，请检查 OpenSSH 或 PuTTY 是否已安装";
                break;
            case QProcess::Crashed:
                errMsg = "ssh 进程异常崩溃";
                break;
            default:
                errMsg = QString("ssh 进程错误 (代码: %1)").arg(error);
        }
        qDebug() << "[SshManager] [ssh-upload] 进程错误:" << error << errMsg;
        emit uploadFinished(localPath, false, errMsg);
        m_uploadProcesses.remove(localPath);
        proc->deleteLater();
    });

    // 启动进程
    m_uploadProcesses[localPath] = proc;
    proc->start();

    // 等待进程启动
    if (!proc->waitForStarted(3000)) {
        qDebug() << "[SshManager] [ssh-upload] 进程未能启动:" << proc->errorString();
        return false; // errorOccurred 信号会处理
    }

    // 写入文件内容到 stdin
    QFile file(localPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData = file.readAll();
        qint64 written = proc->write(fileData);
        file.close();
        qDebug() << "[SshManager] [ssh-upload] 已写入" << written << "字节";
    } else {
        qWarning() << "[SshManager] [ssh-upload] 无法打开本地文件:" << localPath;
    }

    // 关闭 stdin（告诉远程 cat 命令文件结束）
    proc->closeWriteChannel();

    return true; // 异步，结果通过信号通知
}
