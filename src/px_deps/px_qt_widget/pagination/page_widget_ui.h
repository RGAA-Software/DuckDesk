#ifndef UI_PAGEWIDGET_H
#define UI_PAGEWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QWidget>
#include <QLineEdit>

QT_BEGIN_NAMESPACE

class Ui_PageWidget
{
public:
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer_2;
    QLabel *previousPageLabel;
    QWidget *leftPagesWidget;
    QLabel *leftSeparateLabel;
    QWidget *centerPagesWidget;
    QLabel *rightSeparateLabel;
    QWidget *rightPagesWidget;
    QLabel *nextPageLabel;
    //QSpacerItem *horizontalSpacer_3;
    //QLabel *label;
    QSpacerItem *horizontalSpacer_5;
    QLineEdit *pageLineEdit;
    QSpacerItem *horizontalSpacer_4;
    QPushButton *goto_page;
    QSpacerItem *horizontalSpacer;

    void setupUi(QWidget *PageWidget)
    {
        if (PageWidget->objectName().isEmpty())
            PageWidget->setObjectName(QString::fromUtf8("PageWidget"));
        PageWidget->resize(556, 134);
        PageWidget->setStyleSheet(QString::fromUtf8(""));
        horizontalLayout = new QHBoxLayout(PageWidget);
        horizontalLayout->setSpacing(0);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_2);

        previousPageLabel = new QLabel(PageWidget);
        previousPageLabel->setObjectName(QString::fromUtf8("previousPageLabel"));
        previousPageLabel->setAlignment(Qt::AlignCenter);
//        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
//        sizePolicy.setHorizontalStretch(0);
//        sizePolicy.setVerticalStretch(0);
//        sizePolicy.setHeightForWidth(previousPageLabel->sizePolicy().hasHeightForWidth());
//        previousPageLabel->setSizePolicy(sizePolicy);
//        previousPageLabel->setMinimumSize(QSize(30, 30));
//        previousPageLabel->setMaximumSize(QSize(30, 30));

        horizontalLayout->addWidget(previousPageLabel);

        leftPagesWidget = new QWidget(PageWidget);
        leftPagesWidget->setObjectName(QString::fromUtf8("leftPagesWidget"));

        horizontalLayout->addWidget(leftPagesWidget);

        leftSeparateLabel = new QLabel(PageWidget);
        leftSeparateLabel->setObjectName(QString::fromUtf8("leftSeparateLabel"));

        horizontalLayout->addWidget(leftSeparateLabel);

        centerPagesWidget = new QWidget(PageWidget);
        centerPagesWidget->setObjectName(QString::fromUtf8("centerPagesWidget"));

        horizontalLayout->addWidget(centerPagesWidget);

        rightSeparateLabel = new QLabel(PageWidget);
        rightSeparateLabel->setObjectName(QString::fromUtf8("rightSeparateLabel"));

        horizontalLayout->addWidget(rightSeparateLabel);

        rightPagesWidget = new QWidget(PageWidget);
        rightPagesWidget->setObjectName(QString::fromUtf8("rightPagesWidget"));

        horizontalLayout->addWidget(rightPagesWidget);

        nextPageLabel = new QLabel(PageWidget);
        nextPageLabel->setObjectName(QString::fromUtf8("nextPageLabel"));
        nextPageLabel->setAlignment(Qt::AlignCenter);
//        sizePolicy.setHeightForWidth(nextPageLabel->sizePolicy().hasHeightForWidth());
//        nextPageLabel->setSizePolicy(sizePolicy);
//        nextPageLabel->setMinimumSize(QSize(30, 30));
//        nextPageLabel->setMaximumSize(QSize(30, 30));
//        nextPageLabel->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        horizontalLayout->addWidget(nextPageLabel);

        //horizontalSpacer_3 = new QSpacerItem(30, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);
        //horizontalLayout->addItem(horizontalSpacer_3);

        //label = new QLabel(PageWidget);
        //label->setObjectName(QString::fromUtf8("label"));
        //horizontalLayout->addWidget(label);

        horizontalSpacer_5 = new QSpacerItem(10, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_5);

        pageLineEdit = new QLineEdit(PageWidget);
        pageLineEdit->setObjectName(QString::fromUtf8("pageLineEdit"));
//        sizePolicy.setHeightForWidth(pageLineEdit->sizePolicy().hasHeightForWidth());
//        pageLineEdit->setSizePolicy(sizePolicy);
        pageLineEdit->setMinimumSize(QSize(50, 0));
        pageLineEdit->setMaximumSize(QSize(50, 16777215));
        pageLineEdit->setAlignment(Qt::AlignCenter);

        horizontalLayout->addWidget(pageLineEdit);

        horizontalSpacer_4 = new QSpacerItem(10, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_4);

        goto_page = new QPushButton(PageWidget);
        goto_page->setObjectName(QString::fromUtf8("goto_page"));
//        sizePolicy.setHeightForWidth(goto_page->sizePolicy().hasHeightForWidth());
//        goto_page->setSizePolicy(sizePolicy);
        goto_page->setMinimumSize(QSize(50, 0));
        goto_page->setMaximumSize(QSize(50, 16777215));

        horizontalLayout->addWidget(goto_page);

        horizontalSpacer = new QSpacerItem(45, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);


        retranslateUi(PageWidget);

        QMetaObject::connectSlotsByName(PageWidget);
    } // setupUi

    void retranslateUi(QWidget *PageWidget)
    {
        PageWidget->setWindowTitle(QCoreApplication::translate("PageWidget", "Form", nullptr));
#if QT_CONFIG(tooltip)
        previousPageLabel->setToolTip(QCoreApplication::translate("PageWidget", "\344\270\212\344\270\200\351\241\265", nullptr));
#endif // QT_CONFIG(tooltip)
        previousPageLabel->setText(QCoreApplication::translate("PageWidget", "❮", nullptr));
#if QT_CONFIG(tooltip)
        leftSeparateLabel->setToolTip(QCoreApplication::translate("PageWidget", "\344\270\213\344\270\200\351\241\265", nullptr));
#endif // QT_CONFIG(tooltip)
        leftSeparateLabel->setText(QCoreApplication::translate("PageWidget", "..", nullptr));
#if QT_CONFIG(tooltip)
        rightSeparateLabel->setToolTip(QCoreApplication::translate("PageWidget", "\344\270\213\344\270\200\351\241\265", nullptr));
#endif // QT_CONFIG(tooltip)
        rightSeparateLabel->setText(QCoreApplication::translate("PageWidget", "..", nullptr));
#if QT_CONFIG(tooltip)
        nextPageLabel->setToolTip(QCoreApplication::translate("PageWidget", "\344\270\213\344\270\200\351\241\265", nullptr));
#endif // QT_CONFIG(tooltip)
        nextPageLabel->setText(QCoreApplication::translate("PageWidget", "❯", nullptr));
        //label->setText(QCoreApplication::translate("PageWidget", "Page", nullptr));
        goto_page->setText(QCoreApplication::translate("PageWidget", "Go", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PageWidget: public Ui_PageWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PAGEWIDGET_H
