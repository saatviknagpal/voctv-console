// src/ui/Theme.cpp
#include "ui/Theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

void applyDarkTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(30, 32, 36));
    p.setColor(QPalette::WindowText, QColor(220, 222, 226));
    p.setColor(QPalette::Base, QColor(22, 24, 27));
    p.setColor(QPalette::AlternateBase, QColor(36, 38, 43));
    p.setColor(QPalette::Text, QColor(220, 222, 226));
    p.setColor(QPalette::Button, QColor(44, 47, 53));
    p.setColor(QPalette::ButtonText, QColor(220, 222, 226));
    p.setColor(QPalette::Highlight, QColor(0, 170, 190));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 112, 118));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 112, 118));
    app.setPalette(p);
    app.setStyleSheet(
        "QGroupBox { border: 1px solid #3a3d44; border-radius: 6px; margin-top: 14px; padding: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; color: #00b4c8; font-weight: bold; }"
        "QPushButton { padding: 6px 10px; border-radius: 4px; }"
        "QPushButton#start { background: #0b7a4b; } QPushButton#stop { background: #8a2c2c; }");
}
