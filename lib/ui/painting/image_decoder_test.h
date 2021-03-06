// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_LIB_UI_PAINTING_IMAGE_DECODER_TEST_H_
#define FLUTTER_LIB_UI_PAINTING_IMAGE_DECODER_TEST_H_

#include <memory>

#include "flutter/common/settings.h"
#include "flutter/runtime/dart_vm.h"
#include "flutter/testing/elf_loader.h"
#include "flutter/testing/test_dart_native_resolver.h"
#include "flutter/testing/testing.h"
#include "flutter/testing/thread_test.h"

namespace flutter {
namespace testing {

class ImageDecoderFixtureTest : public ThreadTest {
 public:
  ImageDecoderFixtureTest();

  Settings CreateSettingsForFixture();

 private:
  std::shared_ptr<TestDartNativeResolver> native_resolver_;
  fml::UniqueFD assets_dir_;
  ELFAOTSymbols aot_symbols_;

  void SetSnapshotsAndAssets(Settings& settings);

  FML_DISALLOW_COPY_AND_ASSIGN(ImageDecoderFixtureTest);
};

}  // namespace testing
}  // namespace flutter

#endif  // FLUTTER_LIB_UI_PAINTING_IMAGE_DECODER_TEST_H_
