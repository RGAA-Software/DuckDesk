//
// Created by RGAA on 2024-04-20.
//

#include "stat_chart.h"
#include "px_qt_widget/no_margin_layout.h"

namespace px
{

    StatChart::StatChart(const std::shared_ptr<PxContext>&,
                         const QString& title,
                         const std::vector<QString>& line_names,
                         QWidget* parent) : QWidget(parent) {
        setStyleSheet("background-color:#ffffff; border: 1px solid #eeeeee;");
        auto layout = new NoMarginVLayout();
        chart_ = new QChart();
        chart_->setTitle(title);
        QFont font;
        font.setPixelSize(16);
        font.setBold(true);
        chart_->setTitleFont(font);
        for (auto& n : line_names) {
            auto s = new QLineSeries();
            s->setName(n);
            series_.insert({n, s});
            chart_->addSeries(s);
        }

        chart_view_ = new QChartView(this);
        chart_view_->setRenderHint(QPainter::Antialiasing);
        chart_view_->setChart(chart_);
        layout->addWidget(chart_view_);

        x_axis_ = new QValueAxis();
        x_axis_->setTickCount(20);
        x_axis_->setRange(0, 180);
        x_axis_->setLabelFormat("%d");
        chart_->addAxis(x_axis_, Qt::AlignBottom);
        for (auto& [n, s] : series_) {
            s->attachAxis(x_axis_);
        }

        y_axis_ = new QValueAxis();
        y_axis_->setRange(0, 120);
        y_axis_->setLabelFormat("%d ms");
        chart_->addAxis(y_axis_, Qt::AlignLeft);
        for (auto& [n, s] : series_) {
            s->attachAxis(y_axis_);
        }

        setLayout(layout);
    }

    void StatChart::UpdateTitle(const QString& title) {
        chart_->setTitle(title);
    }

    void StatChart::UpdateLines(const std::map<QString, std::vector<int32_t>>& value) {
        for (const auto& [input_name, input_values] : value) {
            for (const auto& [name, series] : series_) {
                if (input_name == name && series) {
                    QList<QPointF> points;
                    for (std::size_t index = 0;
                         index < input_values.size(); ++index) {
                        points.push_back(QPointF(
                            static_cast<qreal>(index),
                            input_values.at(index)));
                    }
                    series->replace(points);
                    break;
                }
            }
        }
        if (chart_view_) {
            chart_view_->update();
        }
    }

}
