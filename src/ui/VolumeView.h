#pragma once
#include <QWidget>

class Q3DSurface;
class QSurfaceDataProxy;
class QSurface3DSeries;
class QLabel;

// 3D view of the basilar-membrane depth across the captured volume.
class VolumeView : public QWidget {
public:
    explicit VolumeView(QWidget *parent = nullptr);
    void setSurface(const QVector<QVector<float>> &depthRows, int depthPixels);
private:
    Q3DSurface *graph_;
    QSurfaceDataProxy *proxy_;
    QSurface3DSeries *series_;
    QLabel *hint_;
};
