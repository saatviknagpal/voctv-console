#pragma once
#include <QImage>
#include <QWidget>
#include "core/Types.h"

class BScanView : public QWidget {
    Q_OBJECT
public:
    explicit BScanView(QWidget *parent = nullptr);
    void setFrame(const voctv::BScanFrame &frame);
    void setSelectedPixel(QPoint pixel);
    QSize sizeHint() const override { return {640, 520}; }

signals:
    void pixelSelected(QPoint pixel);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect imageRect() const;
    QImage image_;
    QPoint selected_{-1, -1};
};
