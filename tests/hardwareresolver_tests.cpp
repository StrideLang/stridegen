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
#include <iostream>

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
    std::filesystem::create_directories(m_root / "platforms/P/cores");
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
  EXPECT_EQ(uno->recordString("types", "real"), "Float32");
  // From the device.
  EXPECT_EQ(uno->stringProperty("cpu"), "atmega328p");
  EXPECT_EQ(uno->intProperty("clock"), 16000000);
  EXPECT_EQ(uno->nameListProperty("ports"),
            (std::vector<std::string>{"PortB", "PortC", "PortD"}));
  EXPECT_EQ(uno->entryInt("ports", "PortB", "output"), 37);
  // From the board.
  EXPECT_EQ(uno->entryString("pins", "LED", "port"), "PortB");
  EXPECT_EQ(uno->entryInt("pins", "LED", "bit"), 5);
  EXPECT_EQ(uno->entryInt("pins", "A3", "adc"), 3);
  EXPECT_EQ(uno->entry("pins", "D20"), nullptr);
  EXPECT_EQ(uno->recordString("upload", "method"), "avrdude");
  EXPECT_EQ(uno->recordInt("upload", "baud"), 115200);
  EXPECT_EQ(uno->recordString("toolchain", "cc"), "AvrGcc");
  EXPECT_TRUE(contains(uno->nameListProperty("usb"), "2341:0043"));
  EXPECT_EQ(uno->nameListProperty("image"), std::vector<std::string>{"Hex"});

  // `memory` merges key by key: flash and ram from the device, bootloader
  // from the board.
  EXPECT_EQ(uno->recordInt("memory", "flash"), 32768);
  EXPECT_EQ(uno->recordInt("memory", "ram"), 2048);
  EXPECT_EQ(uno->recordInt("memory", "bootloader"), 512);

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
  EXPECT_FALSE(device->recordInt("memory", "bootloader").has_value());
}

TEST(HardwareResolver, PicoOverlayChainUnchanged) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  auto rp2350 = resolver.resolve("Pico", "RP2350");
  ASSERT_TRUE(rp2350.has_value());
  ASSERT_EQ(rp2350->chain().size(), 2u);
  EXPECT_EQ(rp2350->chain()[0].name, "CortexM33");
  EXPECT_EQ(rp2350->chain()[1].name, "RP2350");

  auto resources = rp2350->nameListProperty("resources");
  EXPECT_TRUE(contains(resources, "PicoHardwareInterpolator"));
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

TEST(HardwareResolver, PlatformCoreComesBeforeSharedCore) {
  ScratchRoot root;
  root.write("cores/C.stride", "core C { triple: \"shared\" pointerBits: 32 }\n");
  root.write("platforms/P/cores/C.stride",
             "core C { triple: \"platform\" pointerBits: 16 }\n");
  root.write("platforms/P/devices/D.stride",
             "device D { platform: P overlays: C }\n");
  HardwareResolver resolver(root.path());
  auto d = resolver.resolve("P", "D");
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->stringProperty("triple"), "platform");
}

TEST(HardwareResolver, SharedCoreIsTheFallback) {
  ScratchRoot root;
  root.write("cores/C.stride", "core C { triple: \"shared\" pointerBits: 32 }\n");
  root.write("platforms/P/devices/D.stride",
             "device D { platform: P overlays: C }\n");
  HardwareResolver resolver(root.path());
  auto d = resolver.resolve("P", "D");
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->stringProperty("triple"), "shared");
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

// GPIO -----------------------------------------------------------------------

namespace {

GpioPinConfig gpioPin(const std::string &name, const std::string &label,
                      const std::string &mode) {
  GpioPinConfig pin;
  pin.name = name;
  pin.label = label;
  pin.mode = mode;
  pin.file = "Rig.stride";
  pin.line = 1;
  return pin;
}

std::vector<std::string> messages(const std::vector<GpioError> &errors) {
  std::vector<std::string> texts;
  for (const auto &e : errors) {
    texts.push_back(e.message);
  }
  return texts;
}

bool anyContains(const std::vector<GpioError> &errors, const std::string &a,
                 const std::string &b = "") {
  for (const auto &e : errors) {
    if (e.message.find(a) != std::string::npos &&
        (b.empty() || e.message.find(b) != std::string::npos)) {
      return true;
    }
  }
  return false;
}

// An STM32-style device: 32-bit ports with BSRR set/clear, a clock gate, 2-bit
// packed mode and pull fields, and a 4-bit alternate-function field that
// spills into AFRH.
const char *kStm32Style = R"(
device Stm32Like {
    platform: P
    ports: [
        gpioPort PortA {
            width: 32
            output: 1207959572 input: 1207959568
            outputSet: 1207959576 outputClear: 1207959576 setShift: 0 clearShift: 16
            enable: { register: 1073876020 bit: 17 }
            fields: [
                gpioField Mode { option: _PinModes register: 1207959552 bitsPerPin: 2
                                 options: [PinInput, PinOutput, PinFunction, PinAnalog] codes: [0, 1, 2, 3] },
                gpioField Pull { option: _PinPulls register: 1207959564 bitsPerPin: 2
                                 options: [PullNone, PullUp, PullDown] codes: [0, 1, 2] },
                gpioField OutType { option: _PinOutputTypes register: 1207959556 bitsPerPin: 1
                                    options: [OutputPushPull, OutputOpenDrain] codes: [0, 1] },
                gpioField AltFn { option: _PinFunctionType register: 1207959584 registerHigh: 1207959588 bitsPerPin: 4 }
            ]
        }
    ]
    pinFeatures: [
        pinFeatures PA2 { port: PortA bit: 2 routes: [pinRoute { function: Usart2Tx select: 7 }] },
        pinFeatures PA9 { port: PortA bit: 9 routes: [pinRoute { function: Usart1Tx select: 7 }] }
    ]
}
)";

const char *kStm32Board = R"(
hardwareBoard Stm32Board {
    platform: P
    overlays: Stm32Like
    pins: [
        pin LED { port: PortA bit: 5 },
        pin TX2 { port: PortA bit: 2 },
        pin TX1 { port: PortA bit: 9 },
        pin D6 { port: PortA bit: 6 },
        pin Ball { packagePin: "H17" }
    ]
}
)";

} // namespace

TEST(HardwareResolver, GpioUnoPinsAndCapabilities) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  std::string error;
  auto uno = resolver.resolve("AVR", "ArduinoUno", &error);
  ASSERT_TRUE(uno.has_value()) << error;

  auto ports = uno->gpioPorts();
  ASSERT_EQ(ports.size(), 3u);
  EXPECT_EQ(ports[0].name, "PortB");
  EXPECT_EQ(ports[0].width, 8);
  EXPECT_EQ(ports[0].direction, 36);
  ASSERT_EQ(ports[0].fields.size(), 2u);
  EXPECT_EQ(ports[0].fields[1].option, "_PinPulls");
  EXPECT_EQ(ports[0].fields[1].codeFor("PullUp"), 1);

  auto d2 = uno->boardPin("D2");
  ASSERT_TRUE(d2.has_value());
  EXPECT_EQ(d2->port, "PortD");
  EXPECT_EQ(d2->bit, 2);
  EXPECT_EQ(uno->supportedValues(*d2, "_PinPulls"),
            (std::vector<std::string>{"PullNone", "PullUp"}));
  EXPECT_TRUE(contains(uno->supportedValues(*d2, "_PinOutputTypes"),
                       "OutputOpenDrain"));
  EXPECT_TRUE(contains(uno->supportedValues(*d2, "_PinInterrupts"),
                       "InterruptRising"));
  auto d4 = uno->boardPin("D4");
  ASSERT_TRUE(d4.has_value());
  EXPECT_FALSE(contains(uno->supportedValues(*d4, "_PinInterrupts"),
                        "InterruptRising"));
  EXPECT_TRUE(contains(uno->supportedValues(*d4, "_PinInterrupts"),
                       "InterruptBothEdges"));
  auto a0 = uno->boardPin("A0");
  ASSERT_TRUE(a0.has_value());
  EXPECT_TRUE(contains(uno->supportedValues(*a0, "_PinModes"), "PinAnalog"));
  EXPECT_FALSE(contains(uno->supportedValues(*d4, "_PinModes"), "PinAnalog"));
  EXPECT_EQ(uno->boardPin("D1")->reservedBy, "Usart0");
  auto led = uno->boardPin("LED");
  ASSERT_TRUE(led.has_value());
  ASSERT_FALSE(led->routes.empty());
  EXPECT_EQ(led->routes[0].function, "Spi0Sck");
}

TEST(HardwareResolver, GpioChecksOneErrorPerFailingKey) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  auto uno = resolver.resolve("AVR", "ArduinoUno");
  ASSERT_TRUE(uno.has_value());

  auto check = [&](GpioPinConfig pin) {
    return HardwareResolver::checkGpioPins(*uno, {pin});
  };

  // Valid uses
  auto button = gpioPin("Button", "D2", "PinInput");
  button.pull = "PullUp";
  button.activeLow = true;
  button.boardQualifier = "ArduinoUno";
  EXPECT_TRUE(check(button).empty());
  auto relay = gpioPin("Relay", "D7", "PinOutput");
  relay.outputType = "OutputOpenDrain";
  EXPECT_TRUE(check(relay).empty());
  auto knob = gpioPin("Knob", "A0", "PinAnalog");
  EXPECT_TRUE(check(knob).empty());
  auto sck = gpioPin("Sck", "D13", "PinFunction");
  sck.function = "Spi0Sck";
  EXPECT_TRUE(check(sck).empty());

  // A value the device lacks, with the supported set
  auto pullDown = gpioPin("Button", "D2", "PinInput");
  pullDown.pull = "PullDown";
  auto errors = check(pullDown);
  ASSERT_EQ(errors.size(), 1u) << messages(errors).front();
  EXPECT_EQ(errors[0].message,
            "gpioPin Button: pull PullDown is not supported by ATmega328P pin "
            "PortD.2 (supported: PullNone, PullUp)");
  EXPECT_EQ(errors[0].file, "Rig.stride");

  auto drive = gpioPin("Led", "D13", "PinOutput");
  drive.drive = "DriveHigh";
  EXPECT_TRUE(anyContains(check(drive), "drive DriveHigh is not supported"));

  EXPECT_TRUE(anyContains(check(gpioPin("Analog", "D2", "PinAnalog")),
                          "mode PinAnalog is not supported"));
  auto rising = gpioPin("Edge", "D4", "PinInput");
  rising.interrupt = "InterruptRising";
  EXPECT_TRUE(anyContains(check(rising), "interrupt InterruptRising"));

  // A function the pin doesn't offer
  auto pio = gpioPin("Strip", "D9", "PinFunction");
  pio.function = "Pio0";
  EXPECT_TRUE(anyContains(check(pio), "function Pio0 can't be routed",
                          "Pwm1A"));

  // Keys that don't fit the mode
  auto outType = gpioPin("In", "D4", "PinInput");
  outType.outputType = "OutputOpenDrain";
  EXPECT_TRUE(anyContains(check(outType), "outputType is set, but mode is PinInput"));
  auto initial = gpioPin("In", "D4", "PinInput");
  initial.initial = true;
  EXPECT_TRUE(anyContains(check(initial), "initial is set"));
  auto analogPull = gpioPin("Knob", "A0", "PinAnalog");
  analogPull.pull = "PullUp";
  EXPECT_TRUE(anyContains(check(analogPull), "pull is set, but mode is PinAnalog"));
  auto stray = gpioPin("Out", "D4", "PinOutput");
  stray.function = "Spi0Sck";
  EXPECT_TRUE(anyContains(check(stray), "function is set, but mode is PinOutput"));
  EXPECT_TRUE(anyContains(check(gpioPin("Fn", "D13", "PinFunction")),
                          "mode PinFunction needs a function"));

  // Reserved, unknown and out-of-chain pins
  EXPECT_TRUE(anyContains(check(gpioPin("Rx", "D0", "PinInput")),
                          "reserved by Usart0"));
  EXPECT_TRUE(anyContains(check(gpioPin("X", "D20", "PinInput")),
                          "has no pin 'D20'"));
  auto other = gpioPin("X", "D2", "PinInput");
  other.boardQualifier = "OtherBoard";
  EXPECT_TRUE(anyContains(check(other), "not in the overlay chain"));

  // LED and D13 are one chip pin: they conflict only with different settings
  auto status = gpioPin("Status", "LED", "PinOutput");
  auto same = gpioPin("Same", "D13", "PinOutput");
  EXPECT_TRUE(HardwareResolver::checkGpioPins(*uno, {status, same}).empty());
  auto input = gpioPin("Input", "D13", "PinInput");
  EXPECT_TRUE(anyContains(HardwareResolver::checkGpioPins(*uno, {status, input}),
                          "also uses with different settings"));
}

TEST(HardwareResolver, GpioUnoAccessAndSetup) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  auto uno = resolver.resolve("AVR", "ArduinoUno");
  ASSERT_TRUE(uno.has_value());

  auto status = gpioPin("Status", "LED", "PinOutput");
  auto access = HardwareResolver::resolvePinAccess(*uno, status);
  ASSERT_TRUE(access.has_value());
  EXPECT_EQ(access->writeStyle, "WriteReadModify");
  EXPECT_EQ(access->width, 8);
  EXPECT_EQ(access->outAddress, 37);
  EXPECT_EQ(access->setAddress, 37);
  EXPECT_EQ(access->outMask, 0x20);
  EXPECT_EQ(access->inAddress, 35);
  EXPECT_FALSE(access->invert);
  EXPECT_FALSE(access->openDrainEmulated);

  auto button = gpioPin("Button", "D2", "PinInput");
  button.pull = "PullUp";
  button.activeLow = true;
  EXPECT_TRUE(HardwareResolver::resolvePinAccess(*uno, button)->invert);

  auto relay = gpioPin("Relay", "D7", "PinOutput");
  relay.outputType = "OutputOpenDrain";
  auto relayAccess = HardwareResolver::resolvePinAccess(*uno, relay);
  ASSERT_TRUE(relayAccess.has_value());
  EXPECT_TRUE(relayAccess->openDrainEmulated);
  EXPECT_EQ(relayAccess->dirAddress, 42);
  EXPECT_EQ(relayAccess->dirMask, 0x80);

  auto other = gpioPin("Other", "D12", "PinOutput");
  other.initial = true;
  auto writes = HardwareResolver::pinSetupWrites(*uno, {status, button, relay, other});
  // Levels: PORTB bits 5 (low) and 4 (high) merged, PORTD bit 7 low;
  // pull-up: PORTD bit 2; direction: DDRB bits 5 and 4 out, DDRD bit 2 in,
  // DDRD bit 7 an output: open drain with initial off drives the pin low.
  ASSERT_EQ(writes.size(), 5u);
  EXPECT_EQ(writes[0].address, 37);
  EXPECT_EQ(writes[0].clearMask, 0x30u);
  EXPECT_EQ(writes[0].setBits, 0x10u);
  EXPECT_EQ(writes[1].address, 43);
  EXPECT_EQ(writes[1].clearMask, 0x84u & 0x80u);
  EXPECT_EQ(writes[2].address, 43);
  EXPECT_EQ(writes[2].setBits, 0x04u);
  EXPECT_EQ(writes[3].address, 36);
  EXPECT_EQ(writes[3].setBits, 0x30u);
  EXPECT_EQ(writes[4].address, 42);
  EXPECT_EQ(writes[4].clearMask, 0x84u);
  EXPECT_EQ(writes[4].setBits, 0x80u);
  for (const auto &w : writes) {
    EXPECT_TRUE(w.readModify);
    EXPECT_EQ(w.width, 8);
  }
}

TEST(HardwareResolver, GpioStm32StyleDevice) {
  ScratchRoot root;
  root.write("platforms/P/devices/Stm32Like.stride", kStm32Style);
  root.write("platforms/P/boards/Stm32Board.stride", kStm32Board);
  HardwareResolver resolver(root.path());
  std::string error;
  auto board = resolver.resolve("P", "Stm32Board", &error);
  ASSERT_TRUE(board.has_value()) << error;

  auto led = gpioPin("Status", "LED", "PinOutput");
  auto tx = gpioPin("Tx", "TX1", "PinFunction");
  tx.function = "Usart1Tx";
  auto button = gpioPin("Button", "D6", "PinInput");
  button.pull = "PullDown";
  EXPECT_TRUE(HardwareResolver::checkGpioPins(*board, {led, tx, button}).empty());

  auto busKeeper = gpioPin("Keep", "D6", "PinInput");
  busKeeper.pull = "PullBusKeeper";
  EXPECT_TRUE(anyContains(HardwareResolver::checkGpioPins(*board, {busKeeper}),
                          "pull PullBusKeeper is not supported"));
  EXPECT_TRUE(anyContains(
      HardwareResolver::checkGpioPins(*board, {gpioPin("B", "Ball", "PinInput")}),
      "package ball H17"));

  auto access = HardwareResolver::resolvePinAccess(*board, led);
  ASSERT_TRUE(access.has_value());
  EXPECT_EQ(access->writeStyle, "WriteSetClear");
  EXPECT_EQ(access->width, 32);
  EXPECT_EQ(access->setAddress, 1207959576);
  EXPECT_EQ(access->clearAddress, 1207959576);
  EXPECT_EQ(access->setMask, 1 << 5);
  EXPECT_EQ(access->clearMask, 1 << 21);

  auto writes = HardwareResolver::pinSetupWrites(*board, {led, tx, button});
  ASSERT_GE(writes.size(), 5u);
  // Clock first
  EXPECT_EQ(writes[0].address, 1073876020);
  EXPECT_EQ(writes[0].setBits, 1u << 17);
  // Initial level: one store to BSRR's clear half, no read
  EXPECT_FALSE(writes[1].readModify);
  EXPECT_EQ(writes[1].address, 1207959576);
  EXPECT_EQ(writes[1].setBits, 1u << 21);
  auto at = [&](int64_t address) {
    return std::find_if(writes.begin(), writes.end(),
                        [&](const RegisterWrite &w) { return w.address == address; });
  };
  // Pull down on PA6
  auto pull = at(1207959564);
  ASSERT_NE(pull, writes.end());
  EXPECT_EQ(pull->clearMask, 3u << 12);
  EXPECT_EQ(pull->setBits, 2u << 12);
  // Output type of the push-pull output is written too
  auto outType = at(1207959556);
  ASSERT_NE(outType, writes.end());
  EXPECT_EQ(outType->clearMask, 1u << 5);
  EXPECT_EQ(outType->setBits, 0u);
  // AF7 for PA9 lands in AFRH bits 4..7
  auto af = std::find_if(writes.begin(), writes.end(),
                         [](const RegisterWrite &w) { return w.address == 1207959588; });
  ASSERT_NE(af, writes.end());
  EXPECT_EQ(af->clearMask, 0xFu << 4);
  EXPECT_EQ(af->setBits, 7u << 4);
  // Mode last, three pins merged into one MODER write
  const auto &mode = writes.back();
  EXPECT_EQ(mode.address, 1207959552);
  EXPECT_EQ(mode.clearMask, (3u << 10) | (3u << 18) | (3u << 12));
  EXPECT_EQ(mode.setBits, (1u << 10) | (2u << 18));
}

TEST(HardwareResolver, Stm32FamilyLayoutPeripheralsAndConsole) {
  // STM32F4Discovery -> STM32F407VG -> STM32F4xx -> CortexM4. The family
  // gives each port a base (@) and one gpioLayout of offsets; the chip adds
  // only its memory.
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  std::string error;
  auto disco = resolver.resolve("STM32F4", "STM32F4Discovery", &error);
  ASSERT_TRUE(disco.has_value()) << error;
  EXPECT_TRUE(disco->hasLayer("STM32F4xx"));
  EXPECT_EQ(disco->stringProperty("fpu"), "fpv4-sp-d16");
  EXPECT_EQ(disco->recordInt("memory", "ram").value_or(0), 131072);
  EXPECT_EQ(disco->recordInt("clocks", "hse").value_or(0), 8000000);
  EXPECT_EQ(disco->recordInt("clocks", "pllN").value_or(0), 336);

  auto gpiod = disco->gpioPort("GPIOD");
  ASSERT_TRUE(gpiod.has_value());
  EXPECT_EQ(gpiod->base.value_or(0), 0x40020C00);
  EXPECT_EQ(gpiod->width, 32);
  EXPECT_EQ(gpiod->input, 0x40020C10);
  EXPECT_EQ(gpiod->output, 0x40020C14);
  EXPECT_EQ(gpiod->outputSet.value_or(0), 0x40020C18);
  EXPECT_EQ(gpiod->clearShift, 16);
  EXPECT_EQ(gpiod->enableRegister.value_or(0), 0x40023830);
  EXPECT_EQ(gpiod->enableBit, 3);
  ASSERT_EQ(gpiod->fields.size(), 3u);
  EXPECT_EQ(gpiod->fields[0].reg, 0x40020C00); // MODER
  EXPECT_EQ(gpiod->fields[2].reg, 0x40020C0C); // PUPDR

  auto usart2 = disco->peripheral("USART2");
  ASSERT_TRUE(usart2.has_value());
  EXPECT_EQ(usart2->kind, "uart");
  EXPECT_EQ(usart2->base.value_or(0), 0x40004400);
  EXPECT_EQ(usart2->ip, "stm32.usart.v1");
  EXPECT_EQ(usart2->bus, "apb1");
  EXPECT_EQ(usart2->enableRegister.value_or(0), 0x40023840);
  EXPECT_EQ(usart2->enableBit, 17);
  EXPECT_FALSE(disco->peripheral("USART9").has_value());

  // The console's pins are the runtime's, by any of their names.
  auto led = gpioPin("Status", "LED", "PinOutput");
  EXPECT_TRUE(HardwareResolver::checkGpioPins(*disco, {led}).empty());
  EXPECT_TRUE(anyContains(
      HardwareResolver::checkGpioPins(*disco,
                                      {gpioPin("Out", "PA2", "PinOutput")}),
      "reserves as the console's tx (USART2)"));
  EXPECT_TRUE(anyContains(
      HardwareResolver::checkGpioPins(*disco,
                                      {gpioPin("In", "VCP_RX", "PinInput")}),
      "console's rx"));
}

TEST(HardwareResolver, GpioProjectLayersInSearchDirectory) {
  ScratchRoot project;
  // An FPGA soft core whose AXI GPIO marks inputs with 1 in TRI, as a
  // project-local device, and the project's board over it.
  project.write("SoftCore.stride", R"(
device SoftCore {
    platform: AVR
    ports: [
        gpioPort Gpio0 {
            width: 32 output: 1073741824 input: 1073741824
            fields: [
                gpioField Tri { option: _PinModes register: 1073741828 bitsPerPin: 1
                                options: [PinInput, PinOutput] codes: [1, 0] }
            ]
        }
    ]
}
)");
  project.write("Rig.stride", R"(
hardwareBoard Rig {
    overlays: SoftCore
    pins: [ pin Led0 { port: Gpio0 bit: 0 }, pin Sw0 { port: Gpio0 bit: 8 } ]
}
)");
  project.write("BlinkRig.stride", R"(
hardwareBoard BlinkRig {
    overlays: ArduinoUno
}
)");
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  resolver.addSearchDirectory(project.path());

  std::string error;
  auto blinkRig = resolver.resolve("AVR", "BlinkRig", &error);
  ASSERT_TRUE(blinkRig.has_value()) << error;
  EXPECT_EQ(blinkRig->chain().size(), 4u);
  EXPECT_TRUE(blinkRig->hasLayer("ArduinoUno"));
  EXPECT_EQ(blinkRig->stringProperty("platform"), "AVR");
  EXPECT_TRUE(blinkRig->boardPin("D2").has_value());

  auto rig = resolver.resolve("AVR", "Rig", &error);
  ASSERT_TRUE(rig.has_value()) << error;
  auto sw = gpioPin("Switch", "Sw0", "PinInput");
  EXPECT_TRUE(HardwareResolver::checkGpioPins(*rig, {sw}).empty());
  auto writes = HardwareResolver::pinSetupWrites(*rig, {sw});
  ASSERT_EQ(writes.size(), 1u);
  EXPECT_EQ(writes[0].address, 1073741828);
  EXPECT_EQ(writes[0].setBits, 1u << 8);
  EXPECT_EQ(writes[0].width, 32);
}

TEST(HardwareResolver, GpioFieldListsMustMatch) {
  ScratchRoot root;
  root.write("platforms/P/devices/Bad.stride", R"(
device Bad {
    platform: P
    ports: [
        gpioPort PortA { output: 1 input: 2 direction: 3
            fields: [ gpioField Pull { option: _PinPulls register: 1
                                       options: [PullNone, PullUp] codes: [0] } ] }
    ]
}
)");
  HardwareResolver resolver(root.path());
  auto bad = resolver.resolve("P", "Bad");
  ASSERT_TRUE(bad.has_value());
  auto errors = HardwareResolver::checkGpioPins(*bad, {});
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "gpioField Pull of PortA has 2 options but 1 codes");
}

TEST(HardwareResolver, GpioMega2560) {
  HardwareResolver resolver(ASTFunctions::getDefaultStrideRoot());
  std::string error;
  auto mega = resolver.resolve("AVR", "ArduinoMega2560", &error);
  ASSERT_TRUE(mega.has_value()) << error;
  EXPECT_EQ(mega->layerName(HardwareKind::Device), "ATmega2560");
  EXPECT_EQ(mega->layerName(HardwareKind::Core), "AVR6");
  EXPECT_EQ(mega->stringProperty("cpu"), "atmega2560");
  EXPECT_EQ(mega->gpioPorts().size(), 11u);

  // D7 is PH4, an extended I/O port; D2 is PE4 with INT4
  auto d7 = mega->boardPin("D7");
  ASSERT_TRUE(d7.has_value());
  EXPECT_EQ(d7->port, "PortH");
  EXPECT_EQ(d7->bit, 4);
  auto relay = gpioPin("Relay", "D7", "PinOutput");
  auto access = HardwareResolver::resolvePinAccess(*mega, relay);
  ASSERT_TRUE(access.has_value());
  EXPECT_EQ(access->outAddress, 0x102);
  EXPECT_EQ(access->outMask, 0x10);

  auto edge = gpioPin("Edge", "D2", "PinInput");
  edge.interrupt = "InterruptRising";
  EXPECT_TRUE(HardwareResolver::checkGpioPins(*mega, {edge}).empty());
  // D4 is PG5: no external or pin-change interrupt
  auto noEdge = gpioPin("NoEdge", "D4", "PinInput");
  noEdge.interrupt = "InterruptBothEdges";
  EXPECT_TRUE(anyContains(HardwareResolver::checkGpioPins(*mega, {noEdge}),
                          "interrupt InterruptBothEdges is not supported"));
  // D0 and D1 are the runtime's serial port; D13 is the LED
  EXPECT_TRUE(anyContains(
      HardwareResolver::checkGpioPins(*mega, {gpioPin("Rx", "D0", "PinInput")}),
      "reserved by Usart0"));
  EXPECT_EQ(mega->boardPin("LED")->port, "PortB");
  EXPECT_EQ(mega->boardPin("LED")->bit, 7);
  EXPECT_EQ(mega->boardPin("A15")->port, "PortK");
}
