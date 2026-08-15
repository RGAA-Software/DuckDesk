#ifndef PAGEWIDGET_H
#define PAGEWIDGET_H

#include <QWidget>
#include <QList>
#include <functional>

class QLabel;
class QEvent;

namespace Ui {
    class PageWidget;
}

typedef std::function<void(int page)> OnPageSelectedCallback;

namespace tc
{

    class PageWidget : public QWidget {
    Q_OBJECT
    public:
        explicit PageWidget(int blockSize = 3, QWidget *parent = 0);
        ~PageWidget() override;

        [[nodiscard]] int getBlockSize() const;
        [[nodiscard]] int getMaxPage() const;
        [[nodiscard]] int getCurrentPage() const;

        void setMaxPage(int maxPage);
        void setCurrentPage(int currentPage, bool signalEmitted = false);
        void setSelectedCallback(OnPageSelectedCallback callback);

    private:
        void setBlockSize(int blockSize);
        void updatePageLabels();
        void initialize();

    protected:
        bool eventFilter(QObject *watched, QEvent *e) override;

    signals:
        void currentPageChanged(int page);

    private:
        Ui::PageWidget *ui;
        int blockSize;
        int maxPage;
        int currentPage;
        QList<QLabel *>* pageLabels;
        OnPageSelectedCallback selected_callback = nullptr;
    };

}
#endif // PAGEWIDGET_H
