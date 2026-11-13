/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "gtest/gtest.h"

#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/utils/astfunctions.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace strd;

namespace {

bool contains(const std::vector<std::string> &list, const std::string &item) {
  return std::find(list.begin(), list.end(), item) != list.end();
}

// A throwaway strideroot holding only hardware files, for the error cases.
class ScratchRoot {
public:
  ScratchRoot() {
    m_root = std::filesystem::temp_directory_path() /
             ("stride_hw_" + std::to_string(std::rand()));
    std::filesystem::create_directories(m_root / "cores");
    std::filesystem::create_directories(m_root / "platforms/P/devices");
    std::filesystem::create_directories(m_root / "platforms/P/boards");
  }
  ~ScratchRoot() {
    std::error_code ec;
    std::filesystem::remove_all(m_root, ec);
  }
  void write(const std::string &relative, const std::string &text) {
    std::ofstream(m_root / relative) << text;
  }
  std::string path() const { return m_root.generic_string(); }

private:
  std::filesystem::path m_root;
};

} // namespace

TEST(HardwareResolver, UnoResolvesBoardDeviceCore) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  std::string error;
  auto uno = resolver.resolve("AVR", "ArduinoUno", &error);
  ASSERT_TRUE(uno.has_value()) << error;

  ASSERT_EQ(uno->chain().size(), 3u);
  EXPECT_EQ(uno->chain()[0].kind, HardwareKind::Core);
  EXPECT_EQ(uno->chain()[1].kind, HardwareKind::Device);
  EXPECT_EQ(uno->chain()[2].kind, HardwareKind::Board);
  EXPECT_EQ(uno->name(), "ArduinoUno");
  EXPECT_EQ(uno->layerName(HardwareKind::Core), "AVR5");
  EXPECT_EQ(uno->layerName(HardwareKind::Device), "ATmega328P");

  // From the core.
  EXPECT_EQ(uno->stringProperty("triple"), "avr");
  EXPECT_EQ(uno->intProperty("pointerBits"), 16);
  EXPECT_EQ(uno->blockString("types", "real"), "Float32");
  // From the device.
  EXPECT_EQ(uno->stringProperty("cpu"), "atmega328p");
  EXPECT_EQ(uno->intProperty("clock"), 16000000);
  EXPECT_EQ(uno->nameListProperty("ports"),
            (std::vector<std::string>{"B", "C", "D"}));
  EXPECT_EQ(uno->entryInt("ports", "B", "output"), 37);
  // From the board.
  EXPECT_EQ(uno->entryString("pins", "LED", "port"), "B");
  EXPECT_EQ(uno->entryInt("pins", "LED", "bit"), 5);
  EXPECT_EQ(uno->entryInt("pins", "A3", "adc"), 3);
  EXPECT_EQ(uno->entry("pins", "D20"), nullptr);
  EXPECT_EQ(uno->blockString("upload", "method"), "avrdude");
  EXPECT_EQ(uno->blockInt("upload", "baud"), 115200);
  EXPECT_EQ(uno->blockString("toolchain", "cc"), "AvrGcc");
  EXPECT_TRUE(contains(uno->nameListProperty("usb"), "2341:0043"));
  EXPECT_EQ(uno->nameListProperty("image"), std::vector<std::string>{"Hex"});

  // `memory` merges key by key: flash and ram from the device, bootloader
  // from the board.
  EXPECT_EQ(uno->blockInt("memory", "flash"), 32768);
  EXPECT_EQ(uno->blockInt("memory", "ram"), 2048);
  EXPECT_EQ(uno->blockInt("memory", "bootloader"), 512);

  // `meta` is a plain value: the board's wins.
  EXPECT_NE(uno->stringProperty("meta").find("Arduino Uno"), std::string::npos);
  // `overlays` is not merged.
  EXPECT_FALSE(uno->hasProperty("overlays"));
}

TEST(HardwareResolver, DeviceAloneStopsAtCore) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  auto device = resolver.resolve("AVR", "ATmega328P");
  ASSERT_TRUE(device.has_value());
  EXPECT_EQ(device->chain().size(), 2u);
  EXPECT_EQ(device->layerName(HardwareKind::Board), "");
  EXPECT_FALSE(device->blockInt("memory", "bootloader").has_value());
}

TEST(HardwareResolver, PicoOverlayChainUnchanged) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  auto rp2350 = resolver.resolve("Pico", "RP2350");
  ASSERT_TRUE(rp2350.has_value());
  ASSERT_EQ(rp2350->chain().size(), 2u);
  EXPECT_EQ(rp2350->chain()[0].name, "RP2040");

  // Resources accumulate; compilerFlags is overridden.
  auto resources = rp2350->nameListProperty("resources");
  EXPECT_TRUE(contains(resources, "PicoBootromFloat"));
  EXPECT_TRUE(contains(resources, "CortexM33HardwareFpu"));
  EXPECT_NE(rp2350->stringProperty("compilerFlags").find("cortex-m33"),
            std::string::npos);
}

TEST(HardwareResolver, ListsDoNotDuplicateItems) {
  ScratchRoot root;
  root.write("cores/C.stride", "core C { triple: \"x\" pointerBits: 32 "
                               "frameworks: [\"A\", \"B\"] }\n");
  root.write("platforms/P/devices/D.stride",
             "device D { platform: P overlays: C frameworks: [\"B\", \"Z\"] }\n");
  HardwareResolver resolver(root.path());
  auto d = resolver.resolve("P", "D");
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->nameListProperty("frameworks"),
            (std::vector<std::string>{"A", "B", "Z"}));
}

TEST(HardwareResolver, NamedEntryReplacesEntryBelow) {
  ScratchRoot root;
  root.write("platforms/P/boards/Base.stride",
             "hardwareBoard Base { platform: P overlays: none pins: [\n"
             "  pin D1 { port: \"B\" bit: 1 },\n"
             "  pin LED { port: \"B\" bit: 5 }\n"
             "] }\n");
  root.write("platforms/P/boards/Clone.stride",
             "hardwareBoard Clone { platform: P overlays: Base pins: [\n"
             "  pin LED { port: \"D\" bit: 2 },\n"
             "  pin D9 { port: \"C\" bit: 0 }\n"
             "] }\n");
  HardwareResolver resolver(root.path());
  std::string error;
  auto clone = resolver.resolve("P", "Clone", &error);
  ASSERT_TRUE(clone.has_value()) << error;
  // Same order as the base, the replaced entry in its place, new ones last.
  EXPECT_EQ(clone->nameListProperty("pins"),
            (std::vector<std::string>{"D1", "LED", "D9"}));
  EXPECT_EQ(clone->entryString("pins", "LED", "port"), "D");
  EXPECT_EQ(clone->entryInt("pins", "LED", "bit"), 2);
}

TEST(HardwareResolver, UnknownLayerIsAnError) {
  ScratchRoot root;
  root.write("platforms/P/boards/B.stride",
             "hardwareBoard B { platform: P overlays: Missing }\n");
  HardwareResolver resolver(root.path());
  std::string error;
  EXPECT_FALSE(resolver.resolve("P", "B", &error).has_value());
  EXPECT_NE(error.find("Missing"), std::string::npos);
  EXPECT_NE(error.find("hardwareBoard 'B'"), std::string::npos);
}

TEST(HardwareResolver, CycleIsAnError) {
  ScratchRoot root;
  root.write("platforms/P/devices/D1.stride",
             "device D1 { platform: P overlays: D2 }\n");
  root.write("platforms/P/devices/D2.stride",
             "device D2 { platform: P overlays: D1 }\n");
  HardwareResolver resolver(root.path());
  std::string error;
  EXPECT_FALSE(resolver.resolve("P", "D1", &error).has_value());
  EXPECT_NE(error.find("cycle"), std::string::npos);
}

TEST(HardwareResolver, LowerLayerCannotOverlayHigherLayer) {
  ScratchRoot root;
  root.write("platforms/P/boards/B.stride",
             "hardwareBoard B { platform: P overlays: none }\n");
  root.write("platforms/P/devices/D.stride",
             "device D { platform: P overlays: B }\n");
  HardwareResolver resolver(root.path());
  std::string error;
  EXPECT_FALSE(resolver.resolve("P", "D", &error).has_value());
  EXPECT_NE(error.find("cannot overlay"), std::string::npos);
}
