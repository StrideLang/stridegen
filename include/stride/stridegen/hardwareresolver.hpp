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

// One register field that sets an option (gpioField).
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

// A function a pin can carry (pinRoute).
struct PinRoute {
  std::string function;
  std::optional<int64_t> select;
  std::optional<int64_t> reg;
};

// A board label with what its chip pin can do (pin plus pinFeatures).
struct BoardPin {
  std::string label;
  std::string port; // empty for an FPGA package pin
  int bit = -1;
  std::string package;
  std::optional<int64_t> analog;
  std::vector<std::string> modes;      // added by pinFeatures
  std::vector<std::string> interrupts; // added by pinFeatures
  std::vector<PinRoute> routes;
  std::string reservedBy;
};

// A project's gpioPin, with the resolver's defaults applied.
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

// How the GPIO modules reach a pin.
struct PinAccess {
  std::string writeStyle; // WriteSetClear, WriteReadModify
  int width = 8;
  int64_t outAddress = 0;
  int64_t setAddress = 0;
  int64_t clearAddress = 0;
  int64_t setMask = 0;
  int64_t clearMask = 0;
  int64_t outMask = 0;
  int64_t inAddress = 0;
  int64_t inMask = 0;
  int64_t dirAddress = 0;
  int64_t dirMask = 0;
  bool invert = false;
  bool openDrainEmulated = false;
};

// One startup register write. readModify writes
// (register & ~clearMask) | setBits; otherwise setBits is stored as is.
struct RegisterWrite {
  int64_t address = 0;
  int width = 8;
  bool readModify = true;
  uint64_t clearMask = 0;
  uint64_t setBits = 0;
  std::string reason;
};

// One merged view of a board, device or core and everything it overlays.
//
// Merge rules, applied from the base layer up to the leaf:
//   - lists accumulate (an item already present is not added twice, and a
//     named entry such as `pin D13 { ... }` replaces the one below it);
//   - `{ key: value }` blocks merge key by key, recursively;
//   - any other value is replaced by the layer above.
// `overlays` itself is not merged.
class ResolvedHardware {
public:
  // Base layer first, the requested layer last.
  const std::vector<HardwareLayer> &chain() const { return m_chain; }
  const std::string &name() const;

  // The name of the top-most layer of a kind, or empty if the chain has
  // none: for an Uno, layerName(Device) is "ATmega328P".
  std::string layerName(HardwareKind kind) const;

  ASTNode property(const std::string &name) const;
  bool hasProperty(const std::string &name) const;
  std::string stringProperty(const std::string &name,
                             const std::string &defaultValue = "") const;
  std::optional<int64_t> intProperty(const std::string &name) const;
  // Names or strings of a list property. A single value reads as a list of
  // one.
  std::vector<std::string> nameListProperty(const std::string &name) const;

  // A value inside a `{ key: value }` block property, e.g.
  // blockValue("upload", "baud").
  ASTNode blockValue(const std::string &block, const std::string &key) const;
  std::string blockString(const std::string &block, const std::string &key,
                          const std::string &defaultValue = "") const;
  std::optional<int64_t> blockInt(const std::string &block,
                                  const std::string &key) const;
  // The keys of a block property, in declaration order.
  std::vector<std::string> blockKeys(const std::string &block) const;

  // A named entry of a list property, e.g. entry("pins", "D13") for
  // `pins: [ pin D13 { port: "B" bit: 5 } ]`, and values inside it.
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

  // GPIO: the device's ports, the board's labels joined with the device's
  // pinFeatures, and what a pin supports for one option (its typeClass):
  // the defaults, the codes of the port's fields, gpio.emulated and the
  // pin's features.
  std::vector<GpioPort> gpioPorts() const;
  std::optional<GpioPort> gpioPort(const std::string &name) const;
  std::vector<BoardPin> boardPins() const;
  std::optional<BoardPin> boardPin(const std::string &label) const;
  std::vector<std::string> supportedValues(const BoardPin &pin,
                                           const std::string &option) const;
  // Layer names of the chain, base first (AVR5, ATmega328P, ArduinoUno, ...).
  bool hasLayer(const std::string &name) const;

private:
  friend class HardwareResolver;
  std::vector<HardwareLayer> m_chain;
  std::map<std::string, ASTNode> m_properties;
};

class HardwareResolver {
public:
  explicit HardwareResolver(std::string strideroot);

  // Resolves `name` as a board, then a device of `platformName`, then a
  // shared core, and follows its overlay chain. Returns nothing and sets
  // `error` when a layer is missing, the chain has a cycle, or a layer
  // overlays a layer of a higher kind (a device overlaying a board).
  std::optional<ResolvedHardware> resolve(const std::string &platformName,
                                          const std::string &name,
                                          std::string *error = nullptr) const;

  // Finds the declaration of one layer without following its overlays.
  std::optional<HardwareLayer> findLayer(const std::string &platformName,
                                         const std::string &name) const;

  const std::string &strideroot() const { return m_strideroot; }

  // Directories searched before strideroot for a layer `<Name>.stride`, such
  // as a project's hardware file next to its program. A project file may
  // declare a board, or a device (an FPGA soft core's GPIO block).
  void addSearchDirectory(const std::string &dir);
  const std::vector<std::string> &searchDirectories() const {
    return m_searchDirectories;
  }

  // A string, entity name or literal as text. Empty for other nodes.
  static std::string valueText(const ASTNode &node);

  // GPIO -------------------------------------------------------------------
  // Checks a project's gpioPin declarations against the resolved hardware:
  // labels, values the device supports, keys that fit the mode, reserved
  // pins and conflicting uses of one chip pin. One error per failing check.
  static std::vector<GpioError>
  checkGpioPins(const ResolvedHardware &hardware,
                const std::vector<GpioPinConfig> &pins);
  // How the GPIO modules reach a checked pin: addresses and masks.
  static std::optional<PinAccess>
  resolvePinAccess(const ResolvedHardware &hardware, const GpioPinConfig &pin);
  // The register writes that set the pins up at startup, in order: port
  // clocks, initial levels, pulls/drive/slew, function select, then mode
  // and direction, so outputs never glitch. Writes to one register in one
  // step are merged.
  static std::vector<RegisterWrite>
  pinSetupWrites(const ResolvedHardware &hardware,
                 const std::vector<GpioPinConfig> &pins);

private:
  std::string m_strideroot;
  std::vector<std::string> m_searchDirectories;
};

} // namespace strd

#endif // STRIDE_HARDWARERESOLVER_HPP
