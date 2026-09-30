#include "ui/BScanView.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

BScanView::BScanView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(320, 260);
    setCursor(Qt::CrossCursor);
}

void BScanView::setFrame(const voctv::BScanFrame &frame) {
    if (image_.width() != frame.width || image_.height() != frame.height)
        image_ = QImage(frame.width, frame.height, QImage::Format_Grayscale8);
    for (int y = 0; y < frame.height; ++y) {
        uchar *line = image_.scanLine(y);
        for (int x = 0; x < frame.width; ++x)
            line[x] = uchar(std::clamp(std::sqrt(frame.at(x, y)) * 255.0f, 0.0f, 255.0f));  // sqrt = display gamma
    }
    update();
}

void BScanView::setSelectedPixel(QPoint pixel) {
    selected_ = pixel;
    update();
}

QRect BScanView::imageRect() const {
    if (image_.isNull()) return rect();
    QSize s = image_.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void BScanView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 11, 13));
    if (image_.isNull()) {
        p.setPen(QColor(120, 124, 130));
        p.drawText(rect(), Qt::AlignCenter, tr("Connect and start an acquisition"));
        return;
    }
    const QRect r = imageRect();
    p.drawImage(r, image_);
    if (selected_.x() >= 0 && selected_.x() < image_.width() && selected_.y() >= 0 && selected_.y() < image_.height()) {
        const double sx = double(r.width()) / image_.width(), sy = double(r.height()) / image_.height();
        const QPointF c(r.left() + (selected_.x() + 0.5) * sx, r.top() + (selected_.y() + 0.5) * sy);
        p.setPen(QPen(QColor(0, 220, 240, 170), 1, Qt::DashLine));
        p.drawLine(QPointF(c.x(), r.top()), QPointF(c.x(), r.bottom()));
        p.drawLine(QPointF(r.left(), c.y()), QPointF(r.right(), c.y()));
        p.setPen(QPen(QColor(255, 90, 90), 2));
        p.drawEllipse(c, 5, 5);
    }
    p.setPen(QColor(200, 204, 210));
    p.drawText(r.adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignLeft,
               tr("B-scan  %1 x %2").arg(image_.width()).arg(image_.height()));
}

void BScanView::mousePressEvent(QMouseEvent *event) {
    if (image_.isNull()) return;
    const QRect r = imageRect();
    if (!r.contains(event->position().toPoint())) return;
    const int x = std::clamp(int((event->position().x() - r.left()) * image_.width() / r.width()), 0, image_.width() - 1);
    const int y = std::clamp(int((event->position().y() - r.top()) * image_.height() / r.height()), 0, image_.height() - 1);
    setSelectedPixel({x, y});
    emit pixelSelected({x, y});
}
