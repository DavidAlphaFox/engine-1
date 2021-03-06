// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "flutter/flow/layers/container_layer.h"
#include "flutter/flow/layers/layer.h"
#include "flutter/flow/layers/physical_shape_layer.h"
#include "flutter/flow/layers/picture_layer.h"
#include "flutter/fml/command_line.h"
#include "flutter/fml/file.h"
#include "flutter/fml/log_settings.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/common/persistent_cache.h"
#include "flutter/shell/common/shell_test.h"
#include "flutter/shell/common/switches.h"
#include "flutter/testing/testing.h"
#include "include/core/SkPicture.h"

namespace flutter {
namespace testing {

static void WaitForIO(Shell* shell) {
  std::promise<bool> io_task_finished;
  shell->GetTaskRunners().GetIOTaskRunner()->PostTask(
      [&io_task_finished]() { io_task_finished.set_value(true); });
  io_task_finished.get_future().wait();
}

TEST_F(ShellTest, CacheSkSLWorks) {
  // Create a temp dir to store the persistent cache
  fml::ScopedTemporaryDirectory dir;
  PersistentCache::SetCacheDirectoryPath(dir.path());
  PersistentCache::ResetCacheForProcess();

  auto settings = CreateSettingsForFixture();
  settings.cache_sksl = true;
  settings.dump_skp_on_shader_compilation = true;

  fml::AutoResetWaitableEvent firstFrameLatch;
  settings.frame_rasterized_callback =
      [&firstFrameLatch](const FrameTiming& t) { firstFrameLatch.Signal(); };

  auto sksl_config = RunConfiguration::InferFromSettings(settings);
  sksl_config.SetEntrypoint("emptyMain");
  std::unique_ptr<Shell> shell = CreateShell(settings);
  PlatformViewNotifyCreated(shell.get());
  RunEngine(shell.get(), std::move(sksl_config));

  // Initially, we should have no SkSL cache
  auto cache = PersistentCache::GetCacheForProcess()->LoadSkSLs();
  ASSERT_EQ(cache.size(), 0u);

  // Draw something to trigger shader compilations.
  LayerTreeBuilder builder = [](std::shared_ptr<ContainerLayer> root) {
    SkPath path;
    path.addCircle(50, 50, 20);
    auto physical_shape_layer = std::make_shared<PhysicalShapeLayer>(
        SK_ColorRED, SK_ColorBLUE, 1.0f, path, Clip::antiAlias);
    root->Add(physical_shape_layer);
  };
  PumpOneFrame(shell.get(), 100, 100, builder);
  firstFrameLatch.Wait();
  WaitForIO(shell.get());

  // Some skp should be dumped due to shader compilations.
  int skp_count = 0;
  fml::FileVisitor skp_visitor = [&skp_count](const fml::UniqueFD& directory,
                                              const std::string& filename) {
    if (filename.size() >= 4 &&
        filename.substr(filename.size() - 4, 4) == ".skp") {
      skp_count += 1;
    }
    return true;
  };
  fml::VisitFilesRecursively(dir.fd(), skp_visitor);
  ASSERT_GT(skp_count, 0);

  // SkSL cache should be generated by the last run.
  cache = PersistentCache::GetCacheForProcess()->LoadSkSLs();
  ASSERT_GT(cache.size(), 0u);

  // Run the engine again with cache_sksl = false and check that the previously
  // generated SkSL cache is used for precompile.
  PersistentCache::ResetCacheForProcess();
  settings.cache_sksl = false;
  settings.dump_skp_on_shader_compilation = true;
  auto normal_config = RunConfiguration::InferFromSettings(settings);
  normal_config.SetEntrypoint("emptyMain");
  DestroyShell(std::move(shell));
  shell = CreateShell(settings);
  PlatformViewNotifyCreated(shell.get());
  RunEngine(shell.get(), std::move(normal_config));
  firstFrameLatch.Reset();
  PumpOneFrame(shell.get(), 100, 100, builder);
  firstFrameLatch.Wait();
  WaitForIO(shell.get());

  // To check that all shaders are precompiled, verify that no new skp is dumped
  // due to shader compilations.
  int old_skp_count = skp_count;
  skp_count = 0;
  fml::VisitFilesRecursively(dir.fd(), skp_visitor);
  ASSERT_EQ(skp_count, old_skp_count);

  // Remove all files generated
  fml::FileVisitor remove_visitor = [&remove_visitor](
                                        const fml::UniqueFD& directory,
                                        const std::string& filename) {
    if (fml::IsDirectory(directory, filename.c_str())) {
      {  // To trigger fml::~UniqueFD before fml::UnlinkDirectory
        fml::UniqueFD sub_dir =
            fml::OpenDirectoryReadOnly(directory, filename.c_str());
        fml::VisitFiles(sub_dir, remove_visitor);
      }
      fml::UnlinkDirectory(directory, filename.c_str());
    } else {
      fml::UnlinkFile(directory, filename.c_str());
    }
    return true;
  };
  fml::VisitFiles(dir.fd(), remove_visitor);
  DestroyShell(std::move(shell));
}

static void CheckTextSkData(sk_sp<SkData> data, const std::string& expected) {
  std::string data_string(reinterpret_cast<const char*>(data->bytes()),
                          data->size());
  ASSERT_EQ(data_string, expected);
}

void ResetAssetPath() {
  PersistentCache::UpdateAssetPath("some_path_that_does_not_exist");
  ASSERT_EQ(PersistentCache::GetCacheForProcess()->LoadSkSLs().size(), 0u);
}

void CheckTwoSkSLsAreLoaded() {
  auto shaders = PersistentCache::GetCacheForProcess()->LoadSkSLs();
  ASSERT_EQ(shaders.size(), 2u);
}

TEST_F(ShellTest, CanLoadSkSLsFromAsset) {
  // Avoid polluting unit tests output by hiding INFO level logging.
  fml::LogSettings warning_only = {fml::LOG_WARNING};
  fml::ScopedSetLogSettings scoped_set_log_settings(warning_only);

  // Create an empty shell to test its service protocol handlers.
  auto empty_settings = CreateSettingsForFixture();
  auto empty_config = RunConfiguration::InferFromSettings(empty_settings);
  std::unique_ptr<Shell> empty_shell = CreateShell(empty_settings);

  // The SkSL key is Base32 encoded. "IE" is the encoding of "A" and "II" is the
  // encoding of "B".
  //
  // The SkSL data is Base64 encoded. "eA==" is the encoding of "x" and "eQ=="
  // is the encoding of "y".
  const std::string kTestJson =
      "{\n"
      "  \"data\": {\n"
      "    \"IE\": \"eA==\",\n"
      "    \"II\": \"eQ==\"\n"
      "  }\n"
      "}\n";

  // Temp dir for the asset.
  fml::ScopedTemporaryDirectory asset_dir;
  fml::UniqueFD sksl_asset_dir =
      fml::OpenDirectory(asset_dir.fd(), PersistentCache::kSkSLSubdirName, true,
                         fml::FilePermission::kReadWrite);

  auto data = std::make_unique<fml::DataMapping>(
      std::vector<uint8_t>{kTestJson.begin(), kTestJson.end()});
  fml::WriteAtomically(sksl_asset_dir, PersistentCache::kAssetFileName, *data);

  // 1st, test that RunConfiguration::InferFromSettings sets the path.
  ResetAssetPath();
  auto settings = CreateSettingsForFixture();
  settings.assets_path = asset_dir.path();
  RunConfiguration::InferFromSettings(settings);
  CheckTwoSkSLsAreLoaded();

  // 2nd, test that Shell::OnServiceProtocolSetAssetBundlePath sets the path.
  ResetAssetPath();
  ServiceProtocol::Handler::ServiceProtocolMap params;
  rapidjson::Document document;
  params["assetDirectory"] = asset_dir.path();
  OnServiceProtocol(
      empty_shell.get(), ShellTest::ServiceProtocolEnum::kSetAssetBundlePath,
      empty_shell->GetTaskRunners().GetUITaskRunner(), params, document);
  CheckTwoSkSLsAreLoaded();

  // 3rd, test that Shell::OnServiceProtocolRunInView sets the path.
  ResetAssetPath();
  params["assetDirectory"] = asset_dir.path();
  params["mainScript"] = "no_such_script.dart";
  OnServiceProtocol(
      empty_shell.get(), ShellTest::ServiceProtocolEnum::kSetAssetBundlePath,
      empty_shell->GetTaskRunners().GetUITaskRunner(), params, document);
  CheckTwoSkSLsAreLoaded();

  // 4th, test the content of the SkSLs in the asset.
  {
    auto shaders = PersistentCache::GetCacheForProcess()->LoadSkSLs();
    ASSERT_EQ(shaders.size(), 2u);

    // Make sure that the 2 shaders are sorted by their keys. Their keys should
    // be "A" and "B" (decoded from "II" and "IE").
    if (shaders[0].first->bytes()[0] == 'B') {
      std::swap(shaders[0], shaders[1]);
    }

    CheckTextSkData(shaders[0].first, "A");
    CheckTextSkData(shaders[1].first, "B");
    CheckTextSkData(shaders[0].second, "x");
    CheckTextSkData(shaders[1].second, "y");
  }

  // Cleanup.
  DestroyShell(std::move(empty_shell));
  fml::UnlinkFile(sksl_asset_dir, PersistentCache::kAssetFileName);
  fml::UnlinkDirectory(asset_dir.fd(), PersistentCache::kSkSLSubdirName);
}

}  // namespace testing
}  // namespace flutter
