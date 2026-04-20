#include "dbus_environment.hpp"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    auto* env = new DbusEnvironment;

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(env);
    return RUN_ALL_TESTS();
}
