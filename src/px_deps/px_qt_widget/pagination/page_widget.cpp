#include "page_widget.h"
#include "page_widget_ui.h"

#include <QtGlobal>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDebug>
#include <QIntValidator>
#include <QLabel>
#include <QEvent>
#include <QLineEdit>

namespace tc
{

    PageWidget::PageWidget(int blockSize, QWidget *parent) : QWidget(parent),
                                                             ui(new Ui::PageWidget) {
        ui->setupUi(this);
        setBlockSize(blockSize);
        initialize();

        maxPage = 0;
        setMaxPage(1);

        QString qss = QString(".QLabel[page=\"true\"] { padding: 1px; margin-left:5px; margin-right:5px; color:#555555; background-color:#eeeeee;}")
                      +
                      QString(".QLabel[currentPage=\"true\"] { color: white; border-radius: 4px; background-color:#2979ff;}")
                      +
                      QString(".QLabel[page=\"true\"]:hover { color: white; border-radius: 4px; background-color:#75a7ff;}");
        this->setStyleSheet(qss);

        //ui->label->setFont(sk::SysConfig::Instance()->sys_font_11_);
        //ui->pageLineEdit->setFont(sk::SysConfig::Instance()->sys_font_11_);
        //ui->goto_page->setFont(sk::SysConfig::Instance()->sys_font_11_);

        connect(ui->goto_page, &QPushButton::clicked, this, [=, this]() {
            //setMaxPage(maxPage);
            setCurrentPage(ui->pageLineEdit->text().toInt());
        });

        connect(ui->pageLineEdit, &QLineEdit::returnPressed, this, [=, this]() {
            setCurrentPage(ui->pageLineEdit->text().toInt());
        });
    }

    PageWidget::~PageWidget() {
        delete ui;
        delete pageLabels;
    }

    bool PageWidget::eventFilter(QObject *watched, QEvent *e) {
        if (e->type() == QEvent::MouseButtonRelease) {
            int page = -1;
            if (watched == ui->previousPageLabel) { page = getCurrentPage() - 1; }

            if (watched == ui->nextPageLabel) { page = getCurrentPage() + 1; }

            for (int i = 0; i < pageLabels->count(); ++i) {
                if (watched == pageLabels->at(i)) {
                    page = pageLabels->at(i)->text().toInt();
                    break;
                }
            }

            if (-1 != page) {
                setCurrentPage(page, true);
                return true;
            }
        }

        if (watched == ui->pageLineEdit && e->type() == QEvent::KeyRelease) {
            auto ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Return) {
                setCurrentPage(ui->pageLineEdit->text().toInt(), true);
                //setMaxPage(ui->pageLineEdit->text().toInt());
                return true;
            }
        }

        return QWidget::eventFilter(watched, e);
    }

    int PageWidget::getBlockSize() const {
        return blockSize;
    }

    int PageWidget::getMaxPage() const {
        return maxPage;
    }

    int PageWidget::getCurrentPage() const {
        return currentPage;
    }

    void PageWidget::setMaxPage(int page) {
        page = qMax(page, 1);

        if (maxPage != page) {
            this->maxPage = page;
            this->currentPage = 1;
            updatePageLabels();
        }
    }

    void PageWidget::setCurrentPage(int page, bool signalEmitted) {
        page = qMax(page, 1);
        page = qMin(page, maxPage);

        if (page != this->currentPage) {
            this->currentPage = page;
            updatePageLabels();

            if (selected_callback) {
                selected_callback(page);
            }

            if (signalEmitted) {
                emit currentPageChanged(page);
            }
        }
    }

    void PageWidget::setSelectedCallback(OnPageSelectedCallback cbk) {
        selected_callback = cbk;
    }

    void PageWidget::setBlockSize(int blockSize) {
        blockSize = qMax(blockSize, 3);
        if (blockSize % 2 == 0) {
            ++blockSize;
        }
        this->blockSize = blockSize;
    }


    void PageWidget::initialize() {
        ui->pageLineEdit->installEventFilter(this);
        ui->pageLineEdit->setValidator(new QIntValidator(1, 10000000, this));
        ui->pageLineEdit->setFixedSize(QSize(50, 26));
        ui->goto_page->setFixedSize(QSize(50, 26));
        ui->goto_page->setText("Go");

        int item_size = 50;
        ui->nextPageLabel->setProperty("page", "true");
        ui->nextPageLabel->setFixedSize((QSize(item_size, 26)));
        ui->previousPageLabel->setProperty("page", "true");
        ui->previousPageLabel->setFixedSize((QSize(item_size, 26)));
        ui->nextPageLabel->installEventFilter(this);
        ui->previousPageLabel->installEventFilter(this);

        pageLabels = new QList<QLabel*>();

        auto leftLayout = new QHBoxLayout();
        auto centerLayout = new QHBoxLayout();
        auto rightLayout = new QHBoxLayout();
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(0);
        centerLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->setSpacing(0);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(0);

        for (int i = 0; i < blockSize * 3; ++i) {
            auto label = new QLabel(QString::number(i + 1));
            label->setAlignment(Qt::AlignCenter);
            label->setProperty("page", "true");
            label->installEventFilter(this);
            label->setFixedSize(item_size, 26);

            pageLabels->append(label);

            if (i < blockSize) {
                leftLayout->addWidget(label);
            } else if (i < blockSize * 2) {
                centerLayout->addWidget(label);
            } else {
                rightLayout->addWidget(label);
            }
        }

        ui->leftPagesWidget->setLayout(leftLayout);
        ui->centerPagesWidget->setLayout(centerLayout);
        ui->rightPagesWidget->setLayout(rightLayout);
    }

    void PageWidget::updatePageLabels() {
        ui->leftSeparateLabel->hide();
        ui->rightSeparateLabel->hide();

        if (maxPage <= blockSize * 3) {
            for (int i = 0; i < pageLabels->count(); i += 1) {
                auto label = pageLabels->at(i);

                if (i < maxPage) {
                    label->setText(QString::number(i + 1));
                    label->show();
                } else {
                    label->hide();
                }

                if (currentPage - 1 == i) {
                    label->setProperty("currentPage", "true");
                } else {
                    label->setProperty("currentPage", "false");
                }

                label->setStyleSheet("/**/");
            }
            return;
        }

        int c = currentPage;
        int n = blockSize;
        int m = maxPage;
        int centerStartPage = 0;

        if (c >= 1 && c <= n + n / 2 + 1) {
            centerStartPage = n + 1;
            ui->rightSeparateLabel->show();
        } else if (c >= m - n - n / 2 && c <= m) {
            centerStartPage = m - n - n + 1;
            ui->leftSeparateLabel->show();
        } else {
            centerStartPage = c - n / 2;
            ui->rightSeparateLabel->show();
            ui->leftSeparateLabel->show();
        }

        for (int i = 0; i < n; ++i) {
            pageLabels->at(i)->setText(QString::number(i + 1));
            pageLabels->at(n + i)->setText(QString::number(centerStartPage + i));
            pageLabels->at(3 * n - i - 1)->setText(QString::number(m - i));
        }

        for (int i = 0; i < pageLabels->count(); ++i) {
            auto label = pageLabels->at(i);
            int page = label->text().toInt();
            if (page == currentPage) {
                label->setProperty("currentPage", "true");
            } else {
                label->setProperty("currentPage", "false");
            }

            label->setStyleSheet("/**/");
            label->show();
        }
    }

}