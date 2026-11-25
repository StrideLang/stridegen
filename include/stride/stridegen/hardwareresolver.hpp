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

#ifndef STRIDE_HARDWARERESOLVER_HPP
#define STRIDE_HARDWARERESOLVER_HPP

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace strd {

// The three layers of a hardware description. Each layer names the one
// below it with `overlays:`. Boards are declared as `hardwareBoard`, since
// `board` is a game library type:
//   board  -> board or device   (platforms/<P>/boards/<Name>.stride)
//   device -> device or core    (platforms/<P>/devices/<Name>.stride)
//   core   -> core              (platforms/<P>/cores/<Name>.stride, then
//                                 cores/<Name>.stride for cores that several
//                                 platforms share)
enum class HardwareKind { Core, Device, Board };

const char *hardwareKindName(HardwareKind kind);

struct HardwareLayer {
  HardwareKind kind;
  std::string name;
  std::string path;
  std::shared_ptr<DeclarationNode> declaration;
};

// GPIO description, read from the merged layers -------------------------------

struct GpioField {
  std::string name;
  std::string file;
  int line = 0;
  std::string option; // its typeClass, such as "_PinPulls"
  int64_t reg = 0;
  std::optional<int64_t> regHigh;
  int bitsPerPin = 1;
  int shift = 0;
  int64_t pinStride = 0;
  std::vector<std::string> options;
  std::vector<int64_t> codes;
  std::optional<int64_t> codeFor(const std::string &value) const;
};

struct GpioPort {
  std::string name;
  int index = 0;
  // Base address, from `gpioPort GPIOA @ 0x40020000 { }`. A port with a base
  // takes any register it leaves out from the device's `gpioLayout`, whose
  // addresses are offsets from the base.
  std::optional<int64_t> base;
  int width = 8;
  int64_t output = 0;
  int64_t input = 0;
  std::optional<int64_t> direction;
  std::optional<int64_t> outputSet;
  std::optional<int64_t> outputClear;
  std::optional<int64_t> outputToggle;
  std::optional<int64_t> directionSet;
  std::optional<int64_t> directionClear;
  int setShift = 0;
  int clearShift = 0;
  std::optional<int64_t> enableRegister;
  int enableBit = 0;
  bool driverAccess = false;
  std::vector<GpioField> fields;
};

struct PinRoute {
  std::string function;
  std::optional<int64_t> select;
  std::optional<int64_t> reg;
};

// How a pin's interrupt is enabled in hardware and which ISR vector it triggers.
struct GpioInterruptSource {
  std::string vector;
  int64_t enableRegister = 0;
  int enableBit = 0;
  std::optional<int64_t> pinMaskRegister;
  std::optional<GpioField> senseField;
};

struct BoardPin {
  std::string label;
  std::string port; // empty for an FPGA package pin
  int bit = -1;
  std::string package;
  std::optional<int64_t> analog;
  std::vector<std::string> modes;      // added by pinFeatures
  std::vector<std::string> interrupts; // added by pinFeatures
  std::string interruptVector;         // added by pinFeatures (interruptSource.vector)
  std::optional<GpioInterruptSource> interruptSource;
  std::vector<PinRoute> routes;
  std::string reservedBy;
};

struct GpioPinConfig {
  std::string name;
  std::string file;
  int line = 0;
  std::string boardQualifier; // ArduinoUno in ArduinoUno::D2
  std::string label;
  std::string mode;
  std::string outputType = "OutputPushPull";
  std::string pull = "PullNone";
  std::string drive = "DriveDefault";
  std::string slew = "SlewDefault";
  std::string interrupt = "InterruptNone";
  std::string ioStandard = "IoDefault";
  std::string function;
  std::optional<bool> hysteresis;
  bool activeLow = false;
  bool initial = false;
  static GpioPinConfig
  fromDeclaration(const std::shared_ptr<DeclarationNode> &decl);
};

struct GpioError {
  std::string file;
  int line = 0;
  std::string message;
};

struct PinAccess {
  std::string writeStyle; // WriteSetClear, WriteReadModify
  int width = 8;
  int64_t outAddress = 0;
  int64_t setAddress = 0;
  int64_t clearAddress = 0;
  uint64_t setMask = 0;
  uint64_t clearMask = 0;
  uint64_t outMask = 0;
  int64_t inAddress = 0;
  uint64_t inMask = 0;
  int64_t dirAddress = 0;
  uint64_t dirMask = 0;
  bool invert = false;
  bool openDrainEmulated = false;
};

// A peripheral instance of a device, e.g. `usart USART2 @ 0x40004400 { ... }`
// in its `peripherals` list. Drivers and runtimes are written per `ip` (the
// register-level design and version), never per chip.
struct Peripheral {
  std::string name;  // USART2
  std::string kind;  // the entity type: usart, spi, i2c, ...
  std::optional<int64_t> base;
  std::string ip;    // e.g. "stm32.usart.v1"
  std::string bus;   // clock feeding it, a name in the device's clocks: "apb1"
  std::optional<int64_t> enableRegister;
  int enableBit = 0;
  std::shared_ptr<DeclarationNode> declaration;
};

struct RegisterWrite {
  int64_t address = 0;
  int width = 8;
  bool readModify = true;
  uint64_t clearMask = 0;
  uint64_t setBits = 0;
  std::string reason;
};

// One merged view of a board, device or core and everything it overlays.
class ResolvedHardware {
public:
  const std::vector<HardwareLayer> &chain() const { return m_chain; }
  const std::string &name() const;
  std::string layerName(HardwareKind kind) const;

  ASTNode property(const std::string &name) const;
  bool hasProperty(const std::string &name) const;
  std::string stringProperty(const std::string &name,
                             const std::string &defaultValue = "") const;
  std::optional<int64_t> intProperty(const std::string &name) const;
  std::vector<std::string> nameListProperty(const std::string &name) const;

  // A value inside a `{ key: value }` or typed anonymous record property,
  // e.g. recordValue("upload", "baud").
  ASTNode recordValue(const std::string &record, const std::string &key) const;
  std::string recordString(const std::string &record, const std::string &key,
                           const std::string &defaultValue = "") const;
  std::optional<int64_t> recordInt(const std::string &record,
                                   const std::string &key) const;
  std::vector<std::string> recordKeys(const std::string &record) const;

  std::shared_ptr<DeclarationNode> entry(const std::string &list,
                                         const std::string &name) const;
  std::string entryString(const std::string &list, const std::string &name,
                          const std::string &key) const;
  std::optional<int64_t> entryInt(const std::string &list,
                                  const std::string &name,
                                  const std::string &key) const;

  const std::map<std::string, ASTNode> &properties() const {
    return m_properties;
  }

  const std::vector<GpioPort> &gpioPorts() const;
  std::optional<GpioPort> gpioPort(const std::string &name) const;
  const std::vector<BoardPin> &boardPins() const;
  std::optional<BoardPin> boardPin(const std::string &label) const;
  std::vector<std::string> supportedValues(const BoardPin &pin,
                                           const std::string &option) const;
  bool hasLayer(const std::string &name) const;

  std::vector<Peripheral> peripherals() const;
  std::optional<Peripheral> peripheral(const std::string &name) const;

private:
  friend class HardwareResolver;
  void ensureGpioCache() const;

  std::vector<HardwareLayer> m_chain;
  std::map<std::string, ASTNode> m_properties;
  mutable bool m_gpioCacheBuilt = false;
  mutable std::vector<GpioPort> m_cachedPorts;
  mutable std::vector<BoardPin> m_cachedPins;
};

class HardwareResolver {
public:
  explicit HardwareResolver(std::string strideroot);

  std::optional<ResolvedHardware> resolve(const std::string &platformName,
                                          const std::string &name,
                                          std::string *error = nullptr) const;

  std::optional<HardwareLayer> findLayer(const std::string &platformName,
                                         const std::string &name) const;

  const std::string &strideroot() const { return m_strideroot; }

  void addSearchDirectory(const std::string &dir);
  const std::vector<std::string> &searchDirectories() const {
    return m_searchDirectories;
  }

  // GPIO planning & validation ---------------------------------------------
  static std::vector<GpioError>
  checkGpioPins(const ResolvedHardware &hardware,
                const std::vector<GpioPinConfig> &pins);
  static std::optional<PinAccess>
  resolvePinAccess(const ResolvedHardware &hardware, const GpioPinConfig &pin);
  static std::vector<RegisterWrite>
  pinSetupWrites(const ResolvedHardware &hardware,
                 const std::vector<GpioPinConfig> &pins);

private:
  std::string m_strideroot;
  std::vector<std::string> m_searchDirectories;
};

} // namespace strd

#endif // STRIDE_HARDWARERESOLVER_HPP
