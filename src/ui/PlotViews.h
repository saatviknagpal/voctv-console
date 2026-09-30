// src/ui/PlotViews.h
#pragma once
#include <QChartView>
#include "core/Types.h"

class QLineSeries;
class QValueAxis;
class QLogValueAxis;

class AScanPlot : public QChartView {
public:
    explicit AScanPlot(QWidget *parent = nullptr);
    void setProfile(const QVector<float> &profile);
private:
    QLineSeries *series_;
    QValueAxis *x_, *y_;
};

class VibrationPlot : public QChartView {
public:
    explicit VibrationPlot(QWidget *parent = nullptr);
    void setSamples(const QVector<float> &samples, double sampleRateHz);
private:
    QLineSeries *series_;
    QValueAxis *x_, *y_;
};

class TuningCurvePlot : public QChartView {
public:
    explicit TuningCurvePlot(QWidget *parent = nullptr);
    void setPoints(const QVector<voctv::TuningPoint> &points);
private:
    QLineSeries *series_;
    QLogValueAxis *x_;
    QValueAxis *y_;
};
