#include "client_file_browser_model.h"

#include <gtest/gtest.h>

namespace px::client::imgui {
namespace {

std::vector<ClientFileListItem> Items() {
    return {{.path = "a", .name = "Alpha", .size = 20U, .modifiedTime = 30U},
            {.path = "b", .name = "beta", .size = 10U, .modifiedTime = 20U},
            {.path = "c", .name = "Folder", .modifiedTime = 10U, .directory = true}};
}

TEST(ClientFileSelectionTest, SupportsControlAndShiftSelection) {
    const auto items = Items();
    ClientFileSelection selection{};
    selection.Select(0U, "a", false, false, items);
    selection.Select(2U, "c", true, false, items);
    EXPECT_TRUE(selection.Contains("a"));
    EXPECT_TRUE(selection.Contains("c"));
    selection.Select(1U, "b", false, true, items);
    EXPECT_FALSE(selection.Contains("a"));
    EXPECT_TRUE(selection.Contains("b"));
    EXPECT_TRUE(selection.Contains("c"));
}

TEST(ClientFileSelectionTest, SelectAllAndClearAreDeterministic) {
    const auto items = Items();
    ClientFileSelection selection{};
    selection.SelectAll(items);
    EXPECT_EQ(selection.Paths().size(), 3U);
    selection.Clear();
    EXPECT_TRUE(selection.Empty());
}

TEST(ClientFileSortTest, KeepsDirectoriesFirstAndSortsFiles) {
    auto items = Items();
    ClientFileSort sort{.column = ClientFileSortColumn::Size, .ascending = true};
    sort.Apply(items);
    ASSERT_EQ(items.size(), 3U);
    EXPECT_EQ(items[0].path, "c");
    EXPECT_EQ(items[1].path, "b");
    EXPECT_EQ(items[2].path, "a");
    sort.Toggle(ClientFileSortColumn::Size);
    sort.Apply(items);
    EXPECT_EQ(items[0].path, "c");
    EXPECT_EQ(items[1].path, "a");
}

} // namespace
} // namespace px::client::imgui
