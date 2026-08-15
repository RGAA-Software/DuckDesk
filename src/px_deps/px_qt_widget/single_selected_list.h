//
// Created by RGAA on 26/08/2024.
//

#ifndef GAMMARAYPC_SINGLE_SELECTED_LIST_H
#define GAMMARAYPC_SINGLE_SELECTED_LIST_H

#include <QListWidget>
#include <QString>
#include <map>
#include "click_listener.h"

namespace tc
{

    //
    class SingleItem {
    public:
        QString name_;
        QString icon_path_;
        bool selected_{false};
    };
    using SingleItemPtr = std::shared_ptr<SingleItem>;

    //
    class SingleSelectedList :  public QWidget {
    public:
        explicit SingleSelectedList(QWidget* parent = nullptr);

        void UpdateItems(const std::vector<SingleItemPtr>& items);
        void Select(int idx) const;
        std::vector<SingleItemPtr> GetItems() const;
        void SetOnItemClickListener(OnItemClickListener&& listener);
        void SelectByName(const std::string& name);

    private:
        QListWidgetItem* AddItem(const SingleItemPtr& data, int index);

    public:
        QListWidget* list_ = nullptr;
        std::vector<SingleItemPtr> items_;
        std::map<int, QWidget*> select_widgets_;
        OnItemClickListener item_click_listener_;
    };

}

#endif //GAMMARAYPC_SINGLE_SELECTED_LIST_H
