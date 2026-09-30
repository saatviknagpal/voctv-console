// src/ui/VolumeView.cpp
#include "ui/VolumeView.h"
#include <QLabel>
#include <QLinearGradient>
#include <QVBoxLayout>
#include <QtDataVisualization/Q3DCamera>
#include <QtDataVisualization/Q3DScene>
#include <QtDataVisualization/Q3DSurface>
#include <QtDataVisualization/Q3DTheme>
#include <QtDataVisualization/QSurface3DSeries>
#include <QtDataVisualization/QSurfaceDataProxy>
#include <QtDataVisualization/QValue3DAxis>

VolumeView::VolumeView(QWidget *parent) : QWidget(parent) {
    graph_ = new Q3DSurface;
    QWidget *container = QWidget::createWindowContainer(graph_, this);
    container->setMinimumSize(300, 200);
    hint_ = new QLabel(tr("Run a Volume acquisition to build the 3D basilar-membrane surface."), this);
    hint_->setStyleSheet("color: #8a8f98; padding: 4px;");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(hint_);
    layout->addWidget(container, 1);

    graph_->activeTheme()->setType(Q3DTheme::ThemeEbony);
    graph_->scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetIsometricRight);
    graph_->scene()->activeCamera()->setZoomLevel(125.0f);
    graph_->axisX()->setTitle(tr("Lateral (A-scan)"));
    graph_->axisY()->setTitle(tr("Depth (px)"));
    graph_->axisZ()->setTitle(tr("Slice"));
    graph_->axisX()->setTitleVisible(true);
    graph_->axisY()->setTitleVisible(true);
    graph_->axisZ()->setTitleVisible(true);
    graph_->axisY()->setReversed(true);

    proxy_ = new QSurfaceDataProxy;
    series_ = new QSurface3DSeries(proxy_);
    series_->setDrawMode(QSurface3DSeries::DrawSurface);
    QLinearGradient g;
    g.setColorAt(0.0, QColor(20, 40, 160));
    g.setColorAt(0.5, QColor(0, 190, 200));
    g.setColorAt(1.0, QColor(250, 230, 120));
    series_->setBaseGradient(g);
    series_->setColorStyle(Q3DTheme::ColorStyleRangeGradient);
    graph_->addSeries(series_);
}

void VolumeView::setSurface(const QVector<QVector<float>> &depthRows, int depthPixels) {
    if (depthRows.size() < 2) return;
    float lo = float(depthPixels), hi = 0.0f;
    auto *array = new QSurfaceDataArray;
    array->reserve(depthRows.size());
    for (int z = 0; z < depthRows.size(); ++z) {
        auto *row = new QSurfaceDataRow(depthRows[z].size());
        for (int x = 0; x < depthRows[z].size(); ++x) {
            const float d = depthRows[z][x];
            lo = qMin(lo, d);
            hi = qMax(hi, d);
            (*row)[x].setPosition(QVector3D(x, d, z));
        }
        array->append(row);
    }
    proxy_->resetArray(array);
    // Fit the depth axis to the surface so its curvature is visible, not flattened.
    const float pad = qMax(2.0f, 0.15f * (hi - lo));
    graph_->axisY()->setRange(qMax(0.0f, lo - pad), qMin(float(depthPixels), hi + pad));
    hint_->setText(tr("Basilar-membrane surface across %1 slices (drag to rotate, scroll to zoom)").arg(depthRows.size()));
}
