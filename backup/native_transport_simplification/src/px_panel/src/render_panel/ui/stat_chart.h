//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_PANEL_STAT_CHART_H
#define PX_PANEL_STAT_CHART_H

#include <QWidget>
#include <QChart>
#include <QChartView>
#include <QValueAxis>
#include <QCategoryAxis>
#include <QSplineSeries>
#include <QLineSeries>
#include <QPointer>

#include <memory>
#include <map>

namespace px
{

    class PxContext;

    class StatChart : public QWidget {
    public:
        explicit StatChart(const std::shared_ptr<PxContext>& ctx,
                           const QString& title,
                           const std::vector<QString>& line_names,
                           QWidget* parent = nullptr);
        void UpdateTitle(const QString& title);
        void UpdateLines(const std::map<QString, std::vector<int32_t>>& value);

    private:
        QPointer<QChart> chart_;
        QPointer<QChartView> chart_view_;
        QPointer<QValueAxis> x_axis_;
        QPointer<QValueAxis> y_axis_;
        std::map<QString, QPointer<QLineSeries>> series_;

    };

}

#endif //PX_PANEL_STAT_CHART_H
