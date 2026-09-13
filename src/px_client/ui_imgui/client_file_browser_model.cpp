#include "client_file_browser_model.h"

#include <algorithm>
#include <cctype>

namespace px::client::imgui {
namespace {

bool CaseInsensitiveLess(const std::string_view left, const std::string_view right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                        [](const unsigned char lhs, const unsigned char rhs) { return std::tolower(lhs) < std::tolower(rhs); });
}

} // namespace

void ClientFileSelection::Select(const std::size_t index, std::string path, const bool control, const bool shift,
                                 const std::vector<ClientFileListItem>& visibleItems) {
    if (shift && anchor_ && !visibleItems.empty()) {
        const std::size_t first{std::min(*anchor_, index)};
        const std::size_t last{std::min(std::max(*anchor_, index), visibleItems.size() - 1U)};
        if (!control)
            paths_.clear();
        for (std::size_t itemIndex{first}; itemIndex <= last; ++itemIndex) {
            const auto& itemPath = visibleItems[itemIndex].path;
            if (!Contains(itemPath))
                paths_.push_back(itemPath);
        }
        return;
    }
    if (control) {
        const auto found = std::ranges::find(paths_, path);
        if (found == paths_.end())
            paths_.push_back(std::move(path));
        else
            paths_.erase(found);
        anchor_ = index;
        return;
    }
    SelectOnly(index, std::move(path));
}

void ClientFileSelection::SelectOnly(const std::size_t index, std::string path) {
    paths_.assign(1U, std::move(path));
    anchor_ = index;
}

void ClientFileSelection::SelectAll(const std::vector<ClientFileListItem>& visibleItems) {
    paths_.clear();
    paths_.reserve(visibleItems.size());
    for (const auto& item : visibleItems)
        paths_.push_back(item.path);
    anchor_ = visibleItems.empty() ? std::optional<std::size_t>{} : std::optional<std::size_t>{0U};
}

void ClientFileSelection::Clear() noexcept {
    paths_.clear();
    anchor_.reset();
}

bool ClientFileSelection::Contains(const std::string_view path) const {
    return std::ranges::find(paths_, path) != paths_.end();
}

bool ClientFileSelection::Empty() const noexcept {
    return paths_.empty();
}

const std::vector<std::string>& ClientFileSelection::Paths() const noexcept {
    return paths_;
}

void ClientFileSort::Toggle(const ClientFileSortColumn value) noexcept {
    if (column == value)
        ascending = !ascending;
    else {
        column = value;
        ascending = true;
    }
}

void ClientFileSort::Apply(std::vector<ClientFileListItem>& items) const {
    const ClientFileSortColumn selectedColumn{column};
    const bool sortAscending{ascending};
    std::ranges::stable_sort(items, [selectedColumn, sortAscending](const ClientFileListItem& left, const ClientFileListItem& right) {
        if (left.directory != right.directory)
            return left.directory > right.directory;
        bool less{};
        bool equal{};
        switch (selectedColumn) {
        case ClientFileSortColumn::Name:
            less = CaseInsensitiveLess(left.name, right.name);
            equal = !less && !CaseInsensitiveLess(right.name, left.name);
            break;
        case ClientFileSortColumn::Modified:
            less = left.modifiedTime < right.modifiedTime;
            equal = left.modifiedTime == right.modifiedTime;
            break;
        case ClientFileSortColumn::Size:
            less = left.size < right.size;
            equal = left.size == right.size;
            break;
        }
        if (equal)
            less = CaseInsensitiveLess(left.name, right.name);
        return sortAscending ? less : (!equal && !less);
    });
}

} // namespace px::client::imgui
