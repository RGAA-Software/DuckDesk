//
// Created by RGAA on 2025.
//

#include <gtest/gtest.h>
#include "../string_util.h"

using namespace px;

TEST(StringUtilTest, TrimNormal) {
    EXPECT_EQ(StringUtil::Trim("  hello  "), "hello");
    EXPECT_EQ(StringUtil::Trim("\thello\t"), "hello");
    EXPECT_EQ(StringUtil::Trim("\n\rhello\n\r"), "hello");
}

TEST(StringUtilTest, TrimEmpty) {
    EXPECT_EQ(StringUtil::Trim(""), "");
}

TEST(StringUtilTest, TrimAllWhitespace) {
    EXPECT_EQ(StringUtil::Trim("   \t\n\r  "), "");
}

TEST(StringUtilTest, TrimNoWhitespace) {
    EXPECT_EQ(StringUtil::Trim("hello"), "hello");
}

TEST(StringUtilTest, TrimMixedWhitespace) {
    EXPECT_EQ(StringUtil::Trim(" \t hello \n \r "), "hello");
}

TEST(StringUtilTest, ToLowerCpy) {
    EXPECT_EQ(StringUtil::ToLowerCpy("HELLO"), "hello");
    EXPECT_EQ(StringUtil::ToLowerCpy("Hello World"), "hello world");
}

TEST(StringUtilTest, ToUpperCpy) {
    EXPECT_EQ(StringUtil::ToUpperCpy("hello"), "HELLO");
    EXPECT_EQ(StringUtil::ToUpperCpy("Hello World"), "HELLO WORLD");
}

TEST(StringUtilTest, StartWith) {
    EXPECT_TRUE(StringUtil::StartWith("hello world", "hello"));
    EXPECT_FALSE(StringUtil::StartWith("hello world", "world"));
    EXPECT_TRUE(StringUtil::StartWith("", ""));
}

TEST(StringUtilTest, Replace) {
    std::string s = "hello world world";
    StringUtil::Replace(s, "world", "universe");
    EXPECT_EQ(s, "hello universe universe");
}

TEST(StringUtilTest, EmptyReplacementPatternIsIgnored) {
    std::string value = "unchanged";
    StringUtil::Replace(value, "", "x");
    EXPECT_EQ(value, "unchanged");
}

TEST(StringUtilTest, SplitByChar) {
    std::vector<std::string> parts;
    StringUtil::Split("a,b,c", parts, ',');
    EXPECT_EQ(parts.size(), 3);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");
}

TEST(StringUtilTest, SplitByString) {
    std::vector<std::string> parts;
    StringUtil::Split("a::b::c", parts, "::");
    EXPECT_EQ(parts.size(), 3);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");
}

TEST(StringUtilTest, EmptyStringDelimiterReturnsTheInput) {
    std::vector<std::string> parts{"stale"};
    StringUtil::Split("a::b", parts, "");
    ASSERT_EQ(parts.size(), 1);
    EXPECT_EQ(parts.front(), "a::b");
}

TEST(StringUtilTest, IsValidInteger) {
    EXPECT_TRUE(StringUtil::IsValidInteger("123"));
    EXPECT_FALSE(StringUtil::IsValidInteger("12a3"));
    EXPECT_FALSE(StringUtil::IsValidInteger(""));
}

TEST(StringUtilTest, ToWStringToUTF8RoundTrip) {
    std::string original = "hello world";
    auto wstr = StringUtil::ToWString(original);
    auto back = StringUtil::ToUTF8(wstr);
    EXPECT_EQ(back, original);
}

TEST(StringUtilTest, UnicodeToWStringToUTF8RoundTrip) {
    const std::string original = "GammaRay 中文 \xF0\x9F\x9A\x80";
    const auto wstr = StringUtil::ToWString(original);
    ASSERT_FALSE(wstr.empty());
    EXPECT_EQ(StringUtil::ToUTF8(wstr), original);
}

TEST(StringUtilTest, InvalidUtf8IsRejected) {
    const std::string truncated = "\xE4\xB8";
    const std::string overlong = "\xC0\xAF";
    EXPECT_TRUE(StringUtil::ToWString(truncated).empty());
    EXPECT_TRUE(StringUtil::ToWString(overlong).empty());
}
