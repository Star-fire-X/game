#include <gtest/gtest.h>

#include "client/resource/resource_loader.h"

TEST(ResourceProviderTest, MissingArchiveReturnsFalse) {
    mir2::client::ResourceManager manager;
    mir2::client::IResourceProvider& provider = manager;

    EXPECT_FALSE(provider.is_archive_loaded("Missing"));
    EXPECT_FALSE(provider.load_archive("/path/does/not/exist.wil"));
    EXPECT_FALSE(provider.get_sprite("Missing", 1).has_value());
}
