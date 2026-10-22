/* Copyright 2025 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#ifndef WBE_FILE_LINUX_FILE_SYSTEM_TEST_HH
#define WBE_FILE_LINUX_FILE_SYSTEM_TEST_HH

#include "platform/file_system/file_system.hh"
#include "platform/file_system/path.hh"
#include <gtest/gtest.h>

namespace WBE = WhiteBirdEngine;

TEST(LinuxFileSystemTest, LexicalNormalization) {
    ASSERT_EQ(WBE::Path("../assets/./textures/../image.png").lexically_normal(), WBE::Path("../assets/image.png"));
    ASSERT_EQ(WBE::Path("assets/../image.png").lexically_normal(), WBE::Path("image.png"));
}

TEST(LinuxFileSystemTest, ParentAndFilename) {
    WBE::Path path("assets/textures/image.png");
    ASSERT_EQ(path.parent_path(), WBE::Path("assets/textures"));
    ASSERT_EQ(path.filename(), WBE::Path("image.png"));
    ASSERT_EQ(WBE::FileSystem::get_file_dir("assets/textures/image.png"), path.parent_path());
    ASSERT_EQ(WBE::FileSystem::get_file_name("assets/textures/image.png"), "image.png");
    ASSERT_EQ(WBE::FileSystem::get_file_dir("image.png"), WBE::Path());
}

TEST(LinuxFileSystemTest, ExtensionAndComposition) {
    ASSERT_EQ(WBE::Path("archive.tar.gz").extension(), WBE::Path(".gz"));
    ASSERT_EQ(WBE::FileSystem::get_ext(WBE::Path("image.png")), ".png");
    ASSERT_EQ(WBE::Path(".hidden").extension(), WBE::Path());
    ASSERT_EQ(WBE::Path("assets") / "image.png", WBE::Path("assets/image.png"));
    ASSERT_EQ(WBE::FileSystem::path_to_string(WBE::Path("assets/image.png")), WBE::Path("assets/image.png").string());
}

TEST(LinuxFileSystemTest, ExecutableDirectory) {
    WBE::Path directory = WBE::FileSystem::get_executable_dir();
    ASSERT_TRUE(directory.is_absolute());
    ASSERT_FALSE(directory.empty());
}

#endif
