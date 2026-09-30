// src/ui/PlotViews.cpp
#include "ui/PlotViews.h"
#include <QLineSeries>
#include <QLogValueAxis>
#include <QValueAxis>
#include <algorithm>
#include <cmath>

static QChart *makeChart(const QString &title) {
    auto *chart = new QChart;
    chart->setTheme(QChart::ChartThemeDark);
    chart->setBackgroundBrush(QColor(22, 24, 27));
    chart->legend()->hide();
    chart->setTitle(title);
    chart->setMargins(QMargins(4, 4, 4, 4));
    return chart;
}

AScanPlot::AScanPlot(QWidget *parent) : QChartView(parent) {
    QChart *chart = makeChart(tr("A-scan at crosshair"));
    series_ = new QLineSeries;
    series_->setPen(QPen(QColor(0, 200, 220), 1.5));
    chart->addSeries(series_);
    x_ = new QValueAxis; x_->setTitleText(tr("Depth (px)")); x_->setLabelFormat("%d");
    y_ = new QValueAxis; y_->setTitleText(tr("Reflectivity")); y_->setRange(0, 1);
    chart->addAxis(x_, Qt::AlignBottom); chart->addAxis(y_, Qt::AlignLeft);
    series_->attachAxis(x_); series_->attachAxis(y_);
    setChart(chart);
    setRenderHint(QPainter::Antialiasing);
}

void AScanPlot::setProfile(const QVector<float> &profile) {
    QList<QPointF> pts;
    pts.reserve(profile.size());
    for (int i = 0; i < profile.size(); ++i) pts.append(QPointF(i, profile[i]));
    series_->replace(pts);
    x_->setRange(0, qMax(1, int(profile.size()) - 1));
}

VibrationPlot::VibrationPlot(QWidget *parent) : QChartView(parent) {
    QChart *chart = makeChart(tr("Vibration at selected point"));
    series_ = new QLineSeries;
    series_->setPen(QPen(QColor(255, 170, 60), 1.2));
    chart->addSeries(series_);
    x_ = new QValueAxis; x_->setTitleText(tr("Time (ms)"));
    y_ = new QValueAxis; y_->setTitleText(tr("Displacement (nm)"));
    chart->addAxis(x_, Qt::AlignBottom); chart->addAxis(y_, Qt::AlignLeft);
    series_->attachAxis(x_); series_->attachAxis(y_);
    setChart(chart);
    setRenderHint(QPainter::Antialiasing);
}

void VibrationPlot::setSamples(const QVector<float> &samples, double sampleRateHz) {
    constexpr int kShow = 2000;
    const int start = qMax(0, int(samples.size()) - kShow);
    QList<QPointF> pts;
    pts.reserve(samples.size() - start);
    float peak = 0.01f;
    for (int i = start; i < samples.size(); ++i) {
        pts.append(QPointF((i - start) * 1000.0 / qMax(1.0, sampleRateHz), samples[i]));
        peak = std::max(peak, std::abs(samples[i]));
    }
    series_->replace(pts);
    x_->setRange(0, qMax(0.001, (samples.size() - start) * 1000.0 / qMax(1.0, sampleRateHz)));
    y_->setRange(-peak * 1.2, peak * 1.2);
}

TuningCurvePlot::TuningCurvePlot(QWidget *parent) : QChartView(parent) {
    QChart *chart = makeChart(tr("Frequency tuning curve"));
    series_ = new QLineSeries;
    series_->setPen(QPen(QColor(120, 220, 120), 2));
    series_->setPointsVisible(true);
    chart->addSeries(series_);
    x_ = new QLogValueAxis; x_->setTitleText(tr("Frequency (Hz)")); x_->setBase(10); x_->setLabelFormat("%g");
    x_->setRange(100, 80000);
    y_ = new QValueAxis; y_->setTitleText(tr("Displacement (nm)"));
    chart->addAxis(x_, Qt::AlignBottom); chart->addAxis(y_, Qt::AlignLeft);
    series_->attachAxis(x_); series_->attachAxis(y_);
    setChart(chart);
    setRenderHint(QPainter::Antialiasing);
}

void TuningCurvePlot::setPoints(const QVector<voctv::TuningPoint> &points) {
    QList<QPointF> pts;
    double lo = 1e9, hi = 0, peak = 0.01;
    for (const auto &p : points) {
        pts.append(QPointF(p.frequencyHz, p.magnitudeNm));
        lo = std::min(lo, p.frequencyHz); hi = std::max(hi, p.frequencyHz); peak = std::max(peak, p.magnitudeNm);
    }
    series_->replace(pts);
    if (!points.isEmpty()) x_->setRange(lo * 0.9, hi * 1.1);
    y_->setRange(0, peak * 1.15);
}
