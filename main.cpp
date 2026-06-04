//
// Created by zichengpeng on 2026/5/26
//

#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 设置应用程序信息
    app.setApplicationName("CloudPlatform");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("CloudPlatform");

    qDebug() << "[Main] CloudPlatform v1.0.0 启动";

    // 加载全局样式
    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(styleFile.readAll());
        styleFile.close();
    }

    // 直接启动主窗口
    MainWindow window;
    window.show();

    return app.exec();
}
