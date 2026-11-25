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

#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace strd {

const char *hardwareKindName(HardwareKind kind) {
  switch (kind) {
  case HardwareKind::Core:
    return "core";
  case HardwareKind::Device:
    return "device";
  case HardwareKind::Board:
    // Not "board": the game library already has a `board` type.
    return "hardwareBoard";
  }
  return "";
}

namespace {

ASTNode mergeValues(const ASTNode &base, const ASTNode &over);

ASTNode mergeLists(const ASTNode &base, const ASTNode &over) {
  auto merged = base->deepCopy();
  auto items = merged->getChildren();
  for (const auto &item : over->getChildren()) {
    auto text = ASTQuery::valueText(item);
    auto existing =
        text.empty()
            ? items.end()
            : std::find_if(items.begin(), items.end(),
                           [&](const ASTNode &candidate) {
                             return ASTQuery::valueText(candidate) == text;
                           });
    if (existing == items.end()) {
      items.push_back(item->deepCopy());
    } else if (item->getNodeType() == AST::Declaration) {
      *existing = item->deepCopy();
    }
  }
  merged->setChildren(items);
  return merged;
}

ASTNode mergeRecords(const std::shared_ptr<DeclarationNode> &base,
                     const std::shared_ptr<DeclarationNode> &over) {
  auto merged = std::static_pointer_cast<DeclarationNode>(base->deepCopy());
  for (const auto &prop : over->getProperties()) {
    auto existing = merged->getPropertyValue(prop->getName());
    if (existing) {
      merged->replacePropertyValue(prop->getName(),
                                   mergeValues(existing, prop->getValue()));
    } else {
      merged->addProperty(
          std::static_pointer_cast<PropertyNode>(prop->deepCopy()));
    }
  }
  return merged;
}

ASTNode mergeValues(const ASTNode &base, const ASTNode &over) {
  if (!base) {
    return over ? over->deepCopy() : nullptr;
  }
  if (!over) {
    return base;
  }
  if (base->getNodeType() == AST::List && over->getNodeType() == AST::List) {
    return mergeLists(base, over);
  }
  auto baseRec = ASTQuery::asAnonymousRecord(base);
  auto overRec = ASTQuery::asAnonymousRecord(over);
  if (baseRec && overRec &&
      baseRec->getEntityType() == overRec->getEntityType()) {
    return mergeRecords(baseRec, overRec);
  }
  return over->deepCopy();
}

std::optional<int64_t> intOf(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(node)->getIntValue();
  }
  return std::nullopt;
}

std::optional<bool> switchOf(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(node)->getSwitchValue();
  }
  return std::nullopt;
}

std::vector<ASTNode> listItems(const ASTNode &node) {
  if (node && node->getNodeType() == AST::List) {
    return node->getChildren();
  }
  return {};
}

std::shared_ptr<DeclarationNode> asDeclaration(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Declaration) {
    return std::static_pointer_cast<DeclarationNode>(node);
  }
  return nullptr;
}

std::string optionName(const ASTNode &node) {
  if (!node || node->getNodeType() == AST::None) {
    return {};
  }
  if (auto origin = node->getCompilerProperty("resolvedFrom")) {
    if (origin->getNodeType() == AST::Declaration) {
      return std::static_pointer_cast<DeclarationNode>(origin)->getName();
    }
  }
  if (node->getNodeType() == AST::Entity) {
    auto ns = node->getNamespaceList();
    if (!ns.empty()) {
      return std::static_pointer_cast<EntityNode>(node)->getName();
    }
  }
  return ASTQuery::valueText(node);
}

std::vector<std::string> optionNameList(const ASTNode &node) {
  std::vector<std::string> names;
  for (const auto &item : listItems(node)) {
    auto name = optionName(item);
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return names;
}

PinRoute routeFrom(const std::shared_ptr<DeclarationNode> &decl) {
  PinRoute route;
  route.function = optionName(decl->getPropertyValue("function"));
  route.select = intOf(decl->getPropertyValue("select"));
  route.reg = intOf(decl->getPropertyValue("register"));
  return route;
}

const std::vector<std::pair<std::string, std::string>> &optionKeys() {
  static const std::vector<std::pair<std::string, std::string>> keys = {
      {"mode", "_PinModes"},       {"outputType", "_PinOutputTypes"},
      {"pull", "_PinPulls"},       {"drive", "_PinDrives"},
      {"slew", "_PinSlews"},       {"interrupt", "_PinInterrupts"},
      {"ioStandard", "_PinIoStandards"}};
  return keys;
}

bool isDefaultValue(const std::string &value) {
  static const std::set<std::string> defaults = {
      "OutputPushPull", "PullNone",      "DriveDefault",
      "SlewDefault",    "InterruptNone", "IoDefault"};
  return defaults.count(value) > 0;
}

std::string valueOf(const GpioPinConfig &pin, const std::string &key) {
  if (key == "mode") return pin.mode;
  if (key == "outputType") return pin.outputType;
  if (key == "pull") return pin.pull;
  if (key == "drive") return pin.drive;
  if (key == "slew") return pin.slew;
  if (key == "interrupt") return pin.interrupt;
  if (key == "ioStandard") return pin.ioStandard;
  return {};
}

std::string joined(const std::vector<std::string> &items) {
  std::string text;
  for (const auto &item : items) {
    text += (text.empty() ? "" : ", ") + item;
  }
  return text.empty() ? "none" : text;
}

bool contains(const std::vector<std::string> &items, const std::string &item) {
  return std::find(items.begin(), items.end(), item) != items.end();
}

std::pair<int64_t, int> fieldLocation(const GpioField &field, int bit,
                                      int width) {
  if (field.pinStride > 0) {
    return {field.reg + field.pinStride * bit, field.shift};
  }
  int offset = bit * field.bitsPerPin + field.shift;
  if (offset >= width && field.regHigh) {
    return {*field.regHigh, offset - width};
  }
  return {field.reg, offset};
}

uint64_t fieldMask(const GpioField &field) {
  if (field.bitsPerPin <= 0) {
    return 0;
  }
  return field.bitsPerPin >= 64 ? ~0ull : ((1ull << field.bitsPerPin) - 1);
}

bool isOutput(const GpioPinConfig &pin) { return pin.mode == "PinOutput"; }

class RegisterWritePlan {
public:
  enum Step { Enable, Level, Pad, Function, Mode, Interrupt, StepCount };

  RegisterWritePlan() : m_steps(StepCount) {}

  void modify(Step step, int64_t address, int width, uint64_t clear,
              uint64_t set, const std::string &reason) {
    for (auto &w : m_steps[step]) {
      if (w.address == address && w.readModify) {
        w.clearMask |= clear;
        w.setBits = (w.setBits & ~clear) | set;
        w.reason += ", " + reason;
        return;
      }
    }
    m_steps[step].push_back({address, width, true, clear, set, reason});
  }

  void store(Step step, int64_t address, int width, uint64_t value,
             const std::string &reason) {
    m_steps[step].push_back({address, width, false, 0, value, reason});
  }

  void writeField(Step step, const GpioField &field, int bit, int width,
                  int64_t code, const std::string &reason) {
    auto [address, offset] = fieldLocation(field, bit, width);
    if (offset < 0 || offset >= 64) {
      return;
    }
    uint64_t mask = fieldMask(field) << offset;
    modify(step, address, width, mask, (uint64_t(code) << offset) & mask,
           reason);
  }

  std::vector<RegisterWrite> flatten() const {
    std::vector<RegisterWrite> writes;
    for (const auto &step : m_steps) {
      writes.insert(writes.end(), step.begin(), step.end());
    }
    return writes;
  }

private:
  std::vector<std::vector<RegisterWrite>> m_steps;
};

struct GpioPinUse {
  const GpioPinConfig *pin;
  std::string chipPin;
};

void validateDeviceFields(const ResolvedHardware &hardware,
                          std::vector<GpioError> &errors) {
  for (const auto &port : hardware.gpioPorts()) {
    for (const auto &field : port.fields) {
      if (field.options.size() != field.codes.size()) {
        errors.push_back(
            {field.file, field.line,
             "gpioField " + field.name + " of " + port.name + " has " +
                 std::to_string(field.options.size()) + " options but " +
                 std::to_string(field.codes.size()) + " codes"});
      }
    }
  }
}

void validateSinglePin(const ResolvedHardware &hardware,
                       const std::string &device, const GpioPinConfig &pin,
                       std::vector<GpioError> &errors,
                       std::vector<GpioPinUse> &uses) {
  auto fail = [&](const std::string &message) {
    errors.push_back(
        {pin.file, pin.line, "gpioPin " + pin.name + ": " + message});
  };
  if (!pin.boardQualifier.empty() && !hardware.hasLayer(pin.boardQualifier)) {
    fail("'" + pin.boardQualifier + "::" + pin.label + "' names " +
         pin.boardQualifier + ", which is not in the overlay chain of " +
         hardware.name());
    return;
  }
  auto boardPin = hardware.boardPin(pin.label);
  if (!boardPin) {
    fail("board " + hardware.name() + " has no pin '" + pin.label + "'");
    return;
  }
  if (pin.mode.empty()) {
    fail("mode is required");
    return;
  }
  if (boardPin->port.empty()) {
    fail("pin '" + pin.label + "' is the package ball " +
         (boardPin->package.empty() ? std::string("(none)")
                                    : boardPin->package) +
         "; package pins need an FPGA backend, which is not supported yet");
    return;
  }
  auto port = hardware.gpioPort(boardPin->port);
  if (!port) {
    fail("pin '" + pin.label + "' names port " + boardPin->port + ", which " +
         device + " does not declare");
    return;
  }
  if (port->driverAccess) {
    fail("port " + port->name +
         " is reached through a driver (AccessDriver), which the GPIO modules "
         "don't support yet");
    return;
  }
  std::string chipPin =
      device + " pin " + port->name + "." + std::to_string(boardPin->bit);
  if (!boardPin->reservedBy.empty()) {
    fail("pin '" + pin.label + "' (" + chipPin + ") is reserved by " +
         boardPin->reservedBy + ", which the runtime uses");
    return;
  }

  for (const auto &[key, typeClass] : optionKeys()) {
    auto value = valueOf(pin, key);
    auto supported = hardware.supportedValues(*boardPin, typeClass);
    if (!contains(supported, value)) {
      fail(key + " " + value + " is not supported by " + chipPin +
           " (supported: " + joined(supported) + ")");
    }
  }
  if (pin.hysteresis) {
    bool hasField = std::any_of(
        port->fields.begin(), port->fields.end(),
        [](const GpioField &f) { return f.option == "hysteresis"; });
    if (!hasField) {
      fail("hysteresis can't be changed on " + chipPin);
    }
  }

  if (!isOutput(pin)) {
    if (pin.outputType != "OutputPushPull")
      fail("outputType is set, but mode is " + pin.mode);
    if (pin.drive != "DriveDefault")
      fail("drive is set, but mode is " + pin.mode);
    if (pin.slew != "SlewDefault")
      fail("slew is set, but mode is " + pin.mode);
    if (pin.initial)
      fail("initial is set, but mode is " + pin.mode);
  }
  if (pin.mode == "PinAnalog" || pin.mode == "PinDisabled") {
    if (pin.pull != "PullNone") {
      fail("pull is set, but mode is " + pin.mode);
    }
    if (pin.interrupt != "InterruptNone") {
      fail("interrupt is set, but mode is " + pin.mode);
    }
  }
  if (pin.mode == "PinFunction") {
    if (pin.function.empty()) {
      fail("mode PinFunction needs a function");
    } else {
      bool routed = std::any_of(
          boardPin->routes.begin(), boardPin->routes.end(),
          [&](const PinRoute &r) { return r.function == pin.function; });
      if (!routed) {
        std::vector<std::string> offered;
        for (const auto &r : boardPin->routes) {
          offered.push_back(r.function);
        }
        fail("function " + pin.function + " can't be routed to " + chipPin +
             " (it offers: " + joined(offered) + ")");
      }
    }
  } else if (!pin.function.empty()) {
    fail("function is set, but mode is " + pin.mode);
  }

  uses.push_back({&pin, chipPin});
}

void checkPinConflicts(const std::vector<GpioPinUse> &uses,
                       std::vector<GpioError> &errors) {
  for (size_t i = 0; i < uses.size(); ++i) {
    for (size_t j = i + 1; j < uses.size(); ++j) {
      if (uses[i].chipPin != uses[j].chipPin) {
        continue;
      }
      const auto &a = *uses[i].pin;
      const auto &b = *uses[j].pin;
      bool same = a.mode == b.mode && a.outputType == b.outputType &&
                  a.pull == b.pull && a.drive == b.drive && a.slew == b.slew &&
                  a.interrupt == b.interrupt && a.ioStandard == b.ioStandard &&
                  a.hysteresis == b.hysteresis && a.function == b.function &&
                  a.initial == b.initial && a.activeLow == b.activeLow;
      if (!same) {
        errors.push_back({b.file, b.line,
                          "gpioPin " + b.name + " uses " + uses[j].chipPin +
                              ", which gpioPin " + a.name +
                              " also uses with different settings"});
      }
    }
  }
}

void emitInitialLevel(RegisterWritePlan &plan, const GpioPinConfig &pin,
                      const GpioPort &port, const PinAccess &access,
                      uint64_t pinMask) {
  if (!isOutput(pin)) {
    return;
  }
  const int width = port.width;
  bool high = pin.initial != pin.activeLow;
  if (access.openDrainEmulated) {
    plan.modify(RegisterWritePlan::Level, port.output, width, pinMask, 0,
                pin.name + " output low");
  } else if (access.writeStyle == "WriteSetClear") {
    plan.store(RegisterWritePlan::Level,
               high ? access.setAddress : access.clearAddress, width,
               high ? access.setMask : access.clearMask,
               pin.name + (high ? " high" : " low"));
  } else {
    plan.modify(RegisterWritePlan::Level, port.output, width, pinMask,
                high ? pinMask : 0, pin.name + (high ? " high" : " low"));
  }
}

void emitPadFields(RegisterWritePlan &plan, const GpioPinConfig &pin,
                   const GpioPort &port, int bit) {
  for (const auto &field : port.fields) {
    std::string value;
    if (field.option == "_PinPulls")
      value = pin.pull;
    else if (field.option == "_PinDrives")
      value = pin.drive;
    else if (field.option == "_PinSlews")
      value = pin.slew;
    else if (field.option == "_PinOutputTypes")
      value = pin.outputType;
    else if (field.option == "_PinIoStandards")
      value = pin.ioStandard;
    else
      continue;

    if (isDefaultValue(value) &&
        !(field.option == "_PinOutputTypes" && isOutput(pin))) {
      continue;
    }
    if (auto code = field.codeFor(value)) {
      plan.writeField(RegisterWritePlan::Pad, field, bit, port.width, *code,
                      pin.name + " " + value);
    }
  }
}

void emitFunctionSelect(RegisterWritePlan &plan, const GpioPinConfig &pin,
                        const BoardPin &boardPin, const GpioPort &port,
                        int bit) {
  if (pin.mode == "PinFunction") {
    for (const auto &route : boardPin.routes) {
      if (route.function != pin.function) {
        continue;
      }
      for (const auto &field : port.fields) {
        if (field.option == "_PinFunctionType" && route.select) {
          plan.writeField(RegisterWritePlan::Function, field, bit, port.width,
                          *route.select, pin.name + " " + pin.function);
        }
      }
      if (route.reg) {
        plan.store(RegisterWritePlan::Function, *route.reg, 32,
                   uint64_t(port.index * port.width + bit),
                   pin.name + " " + pin.function + " route");
      }
      break;
    }
    return;
  }
  for (const auto &field : port.fields) {
    if (field.option == "_PinFunctions") {
      if (auto code = field.codeFor("PinGpio")) {
        plan.writeField(RegisterWritePlan::Function, field, bit, port.width,
                        *code, pin.name + " GPIO");
      }
    }
  }
}

void emitPinMode(RegisterWritePlan &plan, const GpioPinConfig &pin,
                 const GpioPort &port, const PinAccess &access, int bit,
                 uint64_t pinMask) {
  if (access.openDrainEmulated) {
    bool drivesLow = !(pin.initial != pin.activeLow);
    plan.modify(RegisterWritePlan::Mode, access.dirAddress, port.width, pinMask,
                drivesLow ? pinMask : 0,
                pin.name + (drivesLow ? " drives low" : " released"));
    return;
  }
  bool modeField = false;
  for (const auto &field : port.fields) {
    if (field.option == "_PinModes") {
      if (auto code = field.codeFor(pin.mode)) {
        plan.writeField(RegisterWritePlan::Mode, field, bit, port.width, *code,
                        pin.name + " " + pin.mode);
        modeField = true;
      }
    }
  }
  if (!modeField && pin.mode != "PinFunction") {
    bool output = isOutput(pin);
    if (port.direction) {
      plan.modify(RegisterWritePlan::Mode, *port.direction, port.width, pinMask,
                  output ? pinMask : 0,
                  pin.name + (output ? " output" : " input"));
    } else if (port.directionSet && port.directionClear) {
      plan.store(RegisterWritePlan::Mode,
                 output ? *port.directionSet : *port.directionClear, port.width,
                 pinMask, pin.name + (output ? " output" : " input"));
    }
  }
}

// Hardware interrupt sense and mask registers, after pin mode/pull setup.
void emitInterruptSetup(RegisterWritePlan &plan, const GpioPinConfig &pin,
                        const BoardPin &boardPin, const GpioPort &port,
                        uint64_t pinMask) {
  if (pin.interrupt == "InterruptNone" || !boardPin.interruptSource) {
    return;
  }
  const auto &isrc = *boardPin.interruptSource;
  if (isrc.senseField) {
    if (auto code = isrc.senseField->codeFor(pin.interrupt)) {
      plan.writeField(RegisterWritePlan::Interrupt, *isrc.senseField, 0,
                      port.width, *code, pin.name + " " + pin.interrupt);
    }
  }
  if (isrc.pinMaskRegister) {
    plan.modify(RegisterWritePlan::Interrupt, *isrc.pinMaskRegister, port.width,
                pinMask, pinMask, pin.name + " interrupt pin mask");
  }
  if (isrc.enableBit >= 0 && isrc.enableBit < 64) {
    const uint64_t enMask = uint64_t(1) << isrc.enableBit;
    plan.modify(RegisterWritePlan::Interrupt, isrc.enableRegister, port.width,
                enMask, enMask, pin.name + " interrupt enable");
  }
}

} // namespace

// ResolvedHardware -----------------------------------------------------------

const std::string &ResolvedHardware::name() const {
  static const std::string empty;
  return m_chain.empty() ? empty : m_chain.back().name;
}

std::string ResolvedHardware::layerName(HardwareKind kind) const {
  for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it) {
    if (it->kind == kind) {
      return it->name;
    }
  }
  return {};
}

ASTNode ResolvedHardware::property(const std::string &name) const {
  auto it = m_properties.find(name);
  return it == m_properties.end() ? nullptr : it->second;
}

bool ResolvedHardware::hasProperty(const std::string &name) const {
  auto value = property(name);
  return value && value->getNodeType() != AST::None;
}

std::string
ResolvedHardware::stringProperty(const std::string &name,
                                 const std::string &defaultValue) const {
  auto text = ASTQuery::valueText(property(name));
  return text.empty() ? defaultValue : text;
}

std::optional<int64_t>
ResolvedHardware::intProperty(const std::string &name) const {
  return intOf(property(name));
}

std::vector<std::string>
ResolvedHardware::nameListProperty(const std::string &name) const {
  std::vector<std::string> names;
  auto value = property(name);
  if (!value || value->getNodeType() == AST::None) {
    return names;
  }
  if (value->getNodeType() == AST::List) {
    for (const auto &item : value->getChildren()) {
      auto text = ASTQuery::valueText(item);
      if (!text.empty()) {
        names.push_back(text);
      }
    }
  } else {
    auto text = ASTQuery::valueText(value);
    if (!text.empty()) {
      names.push_back(text);
    }
  }
  return names;
}

ASTNode ResolvedHardware::recordValue(const std::string &record,
                                      const std::string &key) const {
  return ASTQuery::recordValue(property(record), key);
}

std::string
ResolvedHardware::recordString(const std::string &record, const std::string &key,
                               const std::string &defaultValue) const {
  auto text = ASTQuery::valueText(recordValue(record, key));
  return text.empty() ? defaultValue : text;
}

std::optional<int64_t>
ResolvedHardware::recordInt(const std::string &record,
                            const std::string &key) const {
  return intOf(recordValue(record, key));
}

std::vector<std::string>
ResolvedHardware::recordKeys(const std::string &record) const {
  std::vector<std::string> keys;
  if (auto decl = ASTQuery::asAnonymousRecord(property(record))) {
    for (const auto &prop : decl->getProperties()) {
      keys.push_back(prop->getName());
    }
  }
  return keys;
}

std::shared_ptr<DeclarationNode>
ResolvedHardware::entry(const std::string &list,
                        const std::string &name) const {
  auto value = property(list);
  if (!value || value->getNodeType() != AST::List) {
    return nullptr;
  }
  for (const auto &item : value->getChildren()) {
    if (item && item->getNodeType() == AST::Declaration &&
        std::static_pointer_cast<DeclarationNode>(item)->getName() == name) {
      return std::static_pointer_cast<DeclarationNode>(item);
    }
  }
  return nullptr;
}

std::string ResolvedHardware::entryString(const std::string &list,
                                          const std::string &name,
                                          const std::string &key) const {
  auto decl = entry(list, name);
  return decl ? ASTQuery::valueText(decl->getPropertyValue(key))
              : std::string();
}

std::optional<int64_t> ResolvedHardware::entryInt(const std::string &list,
                                                  const std::string &name,
                                                  const std::string &key) const {
  auto decl = entry(list, name);
  return decl ? intOf(decl->getPropertyValue(key)) : std::nullopt;
}

bool ResolvedHardware::hasLayer(const std::string &name) const {
  return std::any_of(m_chain.begin(), m_chain.end(),
                     [&](const HardwareLayer &layer) {
                       return layer.name == name;
                     });
}

void ResolvedHardware::ensureGpioCache() const {
  if (m_gpioCacheBuilt) {
    return;
  }
  m_gpioCacheBuilt = true;

  int portIndex = 0;
  for (const auto &item : listItems(property("ports"))) {
    auto decl = asDeclaration(item);
    if (!decl) {
      continue;
    }
    GpioPort port;
    port.name = decl->getName();
    port.index = portIndex++;
    port.width =
        static_cast<int>(intOf(decl->getPropertyValue("width")).value_or(8));
    port.output = intOf(decl->getPropertyValue("output")).value_or(0);
    port.input = intOf(decl->getPropertyValue("input")).value_or(0);
    port.direction = intOf(decl->getPropertyValue("direction"));
    port.outputSet = intOf(decl->getPropertyValue("outputSet"));
    port.outputClear = intOf(decl->getPropertyValue("outputClear"));
    port.outputToggle = intOf(decl->getPropertyValue("outputToggle"));
    port.directionSet = intOf(decl->getPropertyValue("directionSet"));
    port.directionClear = intOf(decl->getPropertyValue("directionClear"));
    port.setShift =
        static_cast<int>(intOf(decl->getPropertyValue("setShift")).value_or(0));
    port.clearShift = static_cast<int>(
        intOf(decl->getPropertyValue("clearShift")).value_or(0));
    if (auto enable = decl->getPropertyValue("enable")) {
      port.enableRegister = intOf(ASTQuery::recordValue(enable, "register"));
      port.enableBit =
          static_cast<int>(intOf(ASTQuery::recordValue(enable, "bit"))
                               .value_or(0));
    }
    port.driverAccess =
        optionName(decl->getPropertyValue("access")) == "AccessDriver";
    for (const auto &fieldItem : listItems(decl->getPropertyValue("fields"))) {
      auto fieldDecl = asDeclaration(fieldItem);
      if (!fieldDecl) {
        continue;
      }
      GpioField field;
      field.name = fieldDecl->getName();
      field.file = fieldDecl->getFilename();
      field.line = fieldDecl->getLine();
      field.option = optionName(fieldDecl->getPropertyValue("option"));
      field.reg = intOf(fieldDecl->getPropertyValue("register")).value_or(0);
      field.regHigh = intOf(fieldDecl->getPropertyValue("registerHigh"));
      field.bitsPerPin = static_cast<int>(
          intOf(fieldDecl->getPropertyValue("bitsPerPin")).value_or(1));
      field.shift = static_cast<int>(
          intOf(fieldDecl->getPropertyValue("shift")).value_or(0));
      field.pinStride =
          intOf(fieldDecl->getPropertyValue("pinStride")).value_or(0);
      field.options = optionNameList(fieldDecl->getPropertyValue("options"));
      for (const auto &code : listItems(fieldDecl->getPropertyValue("codes"))) {
        field.codes.push_back(intOf(code).value_or(0));
      }
      port.fields.push_back(std::move(field));
    }
    m_cachedPorts.push_back(std::move(port));
  }

  std::map<std::pair<std::string, int64_t>, std::shared_ptr<DeclarationNode>>
      features;
  for (const auto &item : listItems(property("pinFeatures"))) {
    if (auto decl = asDeclaration(item)) {
      auto port = ASTQuery::valueText(decl->getPropertyValue("port"));
      auto bit = intOf(decl->getPropertyValue("bit"));
      if (!port.empty() && bit) {
        features[{port, *bit}] = decl;
      }
    }
  }

  std::vector<PinRoute> anyRoutes;
  for (const auto &routeItem :
       listItems(ASTQuery::recordValue(property("gpio"), "anyPinRoutes"))) {
    if (auto routeDecl = asDeclaration(routeItem)) {
      anyRoutes.push_back(routeFrom(routeDecl));
    }
  }

  for (const auto &item : listItems(property("pins"))) {
    auto decl = asDeclaration(item);
    if (!decl) {
      continue;
    }
    BoardPin pin;
    pin.label = decl->getName();
    pin.port = ASTQuery::valueText(decl->getPropertyValue("port"));
    if (pin.port == "none") {
      pin.port.clear();
    }
    pin.bit =
        static_cast<int>(intOf(decl->getPropertyValue("bit")).value_or(-1));
    auto package = decl->getPropertyValue("packagePin");
    if (package && package->getNodeType() == AST::String) {
      pin.package = ASTQuery::valueText(package);
    }
    pin.analog = intOf(decl->getPropertyValue("adc"));
    auto feature = features.find({pin.port, pin.bit});
    if (feature != features.end()) {
      auto f = feature->second;
      pin.modes = optionNameList(f->getPropertyValue("modes"));
      pin.interrupts = optionNameList(f->getPropertyValue("interrupts"));
      if (auto isrc = asDeclaration(f->getPropertyValue("interruptSource"))) {
        auto vecProp = isrc->getPropertyValue("vector");
        if (vecProp && vecProp->getNodeType() == AST::String) {
          pin.interruptVector = ASTQuery::valueText(vecProp);
        }
      }
      if (auto analog = intOf(f->getPropertyValue("analog"))) {
        pin.analog = analog;
      }
      for (const auto &routeItem : listItems(f->getPropertyValue("routes"))) {
        if (auto routeDecl = asDeclaration(routeItem)) {
          pin.routes.push_back(routeFrom(routeDecl));
        }
      }
      auto reserved = f->getPropertyValue("reservedBy");
      if (reserved && reserved->getNodeType() == AST::String) {
        pin.reservedBy = ASTQuery::valueText(reserved);
      }
      if (auto isrcNode = f->getPropertyValue("interruptSource")) {
        if (auto isrcDecl = asDeclaration(isrcNode)) {
          GpioInterruptSource isrc;
          isrc.vector = ASTQuery::valueText(isrcDecl->getPropertyValue("vector"));
          isrc.enableRegister = intOf(isrcDecl->getPropertyValue("enableRegister")).value_or(0);
          isrc.enableBit = static_cast<int>(intOf(isrcDecl->getPropertyValue("enableBit")).value_or(0));
          isrc.pinMaskRegister = intOf(isrcDecl->getPropertyValue("pinMaskRegister"));
          if (auto sfNode = isrcDecl->getPropertyValue("senseField")) {
            if (auto sfDecl = asDeclaration(sfNode)) {
              GpioField field;
              field.name = sfDecl->getName();
              field.file = sfDecl->getFilename();
              field.line = sfDecl->getLine();
              field.option = optionName(sfDecl->getPropertyValue("option"));
              field.reg = intOf(sfDecl->getPropertyValue("register")).value_or(0);
              field.regHigh = intOf(sfDecl->getPropertyValue("registerHigh"));
              field.bitsPerPin = static_cast<int>(intOf(sfDecl->getPropertyValue("bitsPerPin")).value_or(1));
              field.shift = static_cast<int>(intOf(sfDecl->getPropertyValue("shift")).value_or(0));
              field.pinStride = intOf(sfDecl->getPropertyValue("pinStride")).value_or(0);
              field.options = optionNameList(sfDecl->getPropertyValue("options"));
              for (const auto &code : listItems(sfDecl->getPropertyValue("codes"))) {
                field.codes.push_back(intOf(code).value_or(0));
              }
              isrc.senseField = field;
            }
          }
          if (!isrc.vector.empty() && isrc.enableRegister != 0) {
            pin.interruptSource = isrc;
          }
        }
      }
    }
    pin.routes.insert(pin.routes.end(), anyRoutes.begin(), anyRoutes.end());
    m_cachedPins.push_back(std::move(pin));
  }
}

const std::vector<GpioPort> &ResolvedHardware::gpioPorts() const {
  ensureGpioCache();
  return m_cachedPorts;
}

std::optional<GpioPort>
ResolvedHardware::gpioPort(const std::string &name) const {
  for (const auto &port : gpioPorts()) {
    if (port.name == name) {
      return port;
    }
  }
  return std::nullopt;
}

const std::vector<BoardPin> &ResolvedHardware::boardPins() const {
  ensureGpioCache();
  return m_cachedPins;
}

std::optional<BoardPin>
ResolvedHardware::boardPin(const std::string &label) const {
  for (const auto &pin : boardPins()) {
    if (pin.label == label) {
      return pin;
    }
  }
  return std::nullopt;
}

std::vector<std::string>
ResolvedHardware::supportedValues(const BoardPin &pin,
                                  const std::string &option) const {
  std::vector<std::string> values;
  auto add = [&](const std::string &value) {
    if (!value.empty() && !contains(values, value)) {
      values.push_back(value);
    }
  };
  static const std::map<std::string, std::string> defaults = {
      {"_PinOutputTypes", "OutputPushPull"}, {"_PinPulls", "PullNone"},
      {"_PinDrives", "DriveDefault"},        {"_PinSlews", "SlewDefault"},
      {"_PinInterrupts", "InterruptNone"},   {"_PinIoStandards", "IoDefault"}};
  if (auto def = defaults.find(option); def != defaults.end()) {
    add(def->second);
  }
  if (auto port = gpioPort(pin.port)) {
    if (option == "_PinModes" &&
        (port->direction || (port->directionSet && port->directionClear))) {
      add("PinInput");
      add("PinOutput");
    }
    for (const auto &field : port->fields) {
      if (field.option == option) {
        for (size_t i = 0; i < field.options.size() && i < field.codes.size();
             ++i) {
          add(field.options[i]);
        }
      }
    }
  }
  if (option == "_PinModes") {
    for (const auto &mode : pin.modes) {
      add(mode);
    }
    if (pin.analog) {
      add("PinAnalog");
    }
    if (!pin.routes.empty()) {
      add("PinFunction");
    }
  }
  if (option == "_PinInterrupts") {
    for (const auto &kind : optionNameList(
             ASTQuery::recordValue(property("gpio"), "interrupts"))) {
      add(kind);
    }
    for (const auto &kind : pin.interrupts) {
      add(kind);
    }
  }
  for (const auto &value :
       optionNameList(ASTQuery::recordValue(property("gpio"), "emulated"))) {
    static const std::map<std::string, std::string> owner = {
        {"OutputOpenDrain", "_PinOutputTypes"},
        {"OutputOpenSource", "_PinOutputTypes"},
        {"PullUp", "_PinPulls"},
        {"PullDown", "_PinPulls"}};
    auto it = owner.find(value);
    if (it != owner.end() && it->second == option) {
      add(value);
    }
  }
  return values;
}

// HardwareResolver -----------------------------------------------------------

HardwareResolver::HardwareResolver(std::string strideroot)
    : m_strideroot(std::move(strideroot)) {}

std::optional<HardwareLayer>
HardwareResolver::findLayer(const std::string &platformName,
                            const std::string &name) const {
  if (name.empty()) {
    return std::nullopt;
  }

  auto tryFile = [&](const std::string &path,
                     const std::vector<HardwareKind> &kinds)
      -> std::optional<HardwareLayer> {
    if (!std::filesystem::exists(path)) {
      return std::nullopt;
    }
    auto tree = AST::parseFile(path.c_str(), nullptr);
    if (!tree) {
      return std::nullopt;
    }
    for (HardwareKind kind : kinds) {
      const char *entityType = hardwareKindName(kind);
      for (const auto &child : tree->getChildren()) {
        if (!child || child->getNodeType() != AST::Declaration) {
          continue;
        }
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        if (decl->getEntityType() == entityType && decl->getName() == name) {
          return HardwareLayer{kind, name, path, decl};
        }
      }
    }
    return std::nullopt;
  };

  for (const auto &dir : m_searchDirectories) {
    if (auto layer = tryFile(dir + "/" + name + ".stride",
                             {HardwareKind::Board, HardwareKind::Device,
                              HardwareKind::Core})) {
      return layer;
    }
  }

  if (!platformName.empty()) {
    const std::string platformDir = m_strideroot + "/platforms/" + platformName;
    if (auto layer = tryFile(platformDir + "/boards/" + name + ".stride",
                             {HardwareKind::Board})) {
      return layer;
    }
    if (auto layer = tryFile(platformDir + "/devices/" + name + ".stride",
                             {HardwareKind::Device})) {
      return layer;
    }
    if (auto layer = tryFile(platformDir + "/cores/" + name + ".stride",
                             {HardwareKind::Core})) {
      return layer;
    }
  }
  return tryFile(m_strideroot + "/cores/" + name + ".stride",
                 {HardwareKind::Core});
}

std::optional<ResolvedHardware>
HardwareResolver::resolve(const std::string &platformName,
                          const std::string &name, std::string *error) const {
  std::vector<HardwareLayer> reverseChain;
  std::set<std::string> visited;
  std::string currentName = name;

  while (!currentName.empty()) {
    if (!visited.insert(currentName).second) {
      if (error) {
        *error = "hardware overlay cycle involving '" + currentName + "'";
      }
      return std::nullopt;
    }
    auto layer = findLayer(platformName, currentName);
    if (!layer) {
      std::string message =
          "unknown board, device or core '" + currentName + "'";
      if (!reverseChain.empty()) {
        message += " (overlaid by " +
                   std::string(hardwareKindName(reverseChain.back().kind)) +
                   " '" + reverseChain.back().name + "')";
      }
      if (error) {
        *error = message;
      }
      return std::nullopt;
    }
    if (!reverseChain.empty() &&
        static_cast<int>(layer->kind) >
            static_cast<int>(reverseChain.back().kind)) {
      if (error) {
        *error = std::string(hardwareKindName(reverseChain.back().kind)) +
                 " '" + reverseChain.back().name + "' cannot overlay " +
                 hardwareKindName(layer->kind) + " '" + layer->name + "'";
      }
      return std::nullopt;
    }
    reverseChain.push_back(*layer);
    currentName = ASTQuery::valueText(
        layer->declaration->getPropertyValue("overlays"));
  }

  ResolvedHardware resolved;
  resolved.m_chain.assign(reverseChain.rbegin(), reverseChain.rend());
  for (const auto &layer : resolved.m_chain) {
    for (const auto &prop : layer.declaration->getProperties()) {
      const std::string &propName = prop->getName();
      if (propName == "overlays") {
        continue;
      }
      auto it = resolved.m_properties.find(propName);
      if (it == resolved.m_properties.end()) {
        resolved.m_properties[propName] = prop->getValue()->deepCopy();
      } else {
        it->second = mergeValues(it->second, prop->getValue());
      }
    }
  }
  return resolved;
}

void HardwareResolver::addSearchDirectory(const std::string &dir) {
  if (!dir.empty() && !contains(m_searchDirectories, dir)) {
    m_searchDirectories.push_back(dir);
  }
}

std::optional<int64_t> GpioField::codeFor(const std::string &value) const {
  for (size_t i = 0; i < options.size() && i < codes.size(); ++i) {
    if (options[i] == value) {
      return codes[i];
    }
  }
  return std::nullopt;
}

GpioPinConfig
GpioPinConfig::fromDeclaration(const std::shared_ptr<DeclarationNode> &decl) {
  GpioPinConfig pin;
  if (!decl) {
    return pin;
  }
  pin.name = decl->getName();
  pin.file = decl->getFilename();
  pin.line = decl->getLine();
  if (auto label = decl->getPropertyValue("pin")) {
    pin.label = ASTQuery::valueText(label);
    auto ns = label->getNamespaceList();
    if (!ns.empty()) {
      pin.boardQualifier = ns.back();
    }
  }
  auto text = [&](const char *key, std::string &target) {
    auto value = optionName(decl->getPropertyValue(key));
    if (!value.empty() && value != "none") {
      target = value;
    }
  };
  text("mode", pin.mode);
  text("outputType", pin.outputType);
  text("pull", pin.pull);
  text("drive", pin.drive);
  text("slew", pin.slew);
  text("interrupt", pin.interrupt);
  text("ioStandard", pin.ioStandard);
  text("function", pin.function);
  pin.hysteresis = switchOf(decl->getPropertyValue("hysteresis"));
  pin.activeLow = switchOf(decl->getPropertyValue("activeLow")).value_or(false);
  pin.initial = switchOf(decl->getPropertyValue("initial")).value_or(false);
  return pin;
}

std::vector<GpioError>
HardwareResolver::checkGpioPins(const ResolvedHardware &hardware,
                                const std::vector<GpioPinConfig> &pins) {
  std::vector<GpioError> errors;
  const std::string device = hardware.layerName(HardwareKind::Device);
  std::vector<GpioPinUse> uses;

  validateDeviceFields(hardware, errors);
  for (const auto &pin : pins) {
    validateSinglePin(hardware, device, pin, errors, uses);
  }
  checkPinConflicts(uses, errors);
  return errors;
}

std::optional<PinAccess>
HardwareResolver::resolvePinAccess(const ResolvedHardware &hardware,
                                   const GpioPinConfig &pin) {
  auto boardPin = hardware.boardPin(pin.label);
  if (!boardPin || boardPin->bit < 0 || boardPin->bit >= 64) {
    return std::nullopt;
  }
  auto port = hardware.gpioPort(boardPin->port);
  if (!port) {
    return std::nullopt;
  }
  PinAccess access;
  access.width = port->width;
  access.outAddress = port->output;
  access.outMask = uint64_t(1) << boardPin->bit;
  access.inAddress = port->input;
  access.inMask = access.outMask;
  if (port->outputSet && port->outputClear) {
    int setBit = boardPin->bit + port->setShift;
    int clearBit = boardPin->bit + port->clearShift;
    if (setBit < 0 || setBit >= 64 || clearBit < 0 || clearBit >= 64) {
      return std::nullopt;
    }
    access.writeStyle = "WriteSetClear";
    access.setAddress = *port->outputSet;
    access.clearAddress = *port->outputClear;
    access.setMask = uint64_t(1) << setBit;
    access.clearMask = uint64_t(1) << clearBit;
  } else {
    access.writeStyle = "WriteReadModify";
    access.setAddress = port->output;
    access.clearAddress = port->output;
    access.setMask = access.outMask;
    access.clearMask = access.outMask;
  }
  access.invert = pin.activeLow;
  if (pin.outputType == "OutputOpenDrain") {
    bool hasField = std::any_of(
        port->fields.begin(), port->fields.end(), [](const GpioField &field) {
          return field.option == "_PinOutputTypes" &&
                 field.codeFor("OutputOpenDrain").has_value();
        });
    if (!hasField && port->direction) {
      access.openDrainEmulated = true;
      access.dirAddress = *port->direction;
      access.dirMask = access.outMask;
    }
  }
  return access;
}

std::vector<RegisterWrite>
HardwareResolver::pinSetupWrites(const ResolvedHardware &hardware,
                                 const std::vector<GpioPinConfig> &pins) {
  RegisterWritePlan plan;
  std::set<std::string> enabledPorts;

  for (const auto &pin : pins) {
    auto boardPin = hardware.boardPin(pin.label);
    if (!boardPin || boardPin->bit < 0 || boardPin->bit >= 64) {
      continue;
    }
    auto port = hardware.gpioPort(boardPin->port);
    auto access = resolvePinAccess(hardware, pin);
    if (!port || !access) {
      continue;
    }

    const int bit = boardPin->bit;
    const uint64_t pinMask = uint64_t(1) << bit;
    if (port->enableRegister && enabledPorts.insert(port->name).second &&
        port->enableBit >= 0 && port->enableBit < 64) {
      const uint64_t enMask = uint64_t(1) << port->enableBit;
      plan.modify(RegisterWritePlan::Enable, *port->enableRegister, port->width,
                  enMask, enMask, port->name + " clock");
    }

    emitInitialLevel(plan, pin, *port, *access, pinMask);
    emitPadFields(plan, pin, *port, bit);
    emitFunctionSelect(plan, pin, *boardPin, *port, bit);
    emitPinMode(plan, pin, *port, *access, bit, pinMask);
    emitInterruptSetup(plan, pin, *boardPin, *port, pinMask);
  }

  return plan.flatten();
}

} // namespace strd
