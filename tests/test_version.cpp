// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// Guards the CalVer plumbing: the VERSION file is the only place a version
// number lives, and it has to reach the generated header intact.

#include <gtest/gtest.h>

#include <string>

#include "vphilox/version.hpp"

TEST(Version, StringIsCalVer) {
    const std::string v = vphilox::version_string;

    // YYYY.0M.MICRO
    ASSERT_GE(v.size(), 9u) << "version string too short: " << v;
    EXPECT_EQ(v[4], '.');
    EXPECT_EQ(v[7], '.') << "month must be zero-padded to two digits: " << v;

    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i == 4 || i == 7) continue;
        EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(v[i])))
            << "non-digit at " << i << " in " << v;
    }
}

TEST(Version, MacrosAgreeWithString) {
    const std::string v = vphilox::version_string;
    EXPECT_EQ(std::stoi(v.substr(0, 4)), VPHILOX_VERSION_YEAR);
    EXPECT_EQ(std::stoi(v.substr(5, 2)), VPHILOX_VERSION_MONTH);
    EXPECT_EQ(std::stoi(v.substr(8)), VPHILOX_VERSION_MICRO);
}

TEST(Version, MonthIsARealMonth) {
    EXPECT_GE(VPHILOX_VERSION_MONTH, 1);
    EXPECT_LE(VPHILOX_VERSION_MONTH, 12);
}

TEST(Version, NumberIsComparable) {
    EXPECT_EQ(vphilox::version_number, VPHILOX_VERSION);
    EXPECT_GE(VPHILOX_VERSION, VPHILOX_VERSION_NUMBER(2026, 8, 0));
    EXPECT_LT(VPHILOX_VERSION_NUMBER(2026, 8, 0), VPHILOX_VERSION_NUMBER(2026, 9, 0));
    EXPECT_LT(VPHILOX_VERSION_NUMBER(2026, 12, 9), VPHILOX_VERSION_NUMBER(2027, 1, 0));
}
