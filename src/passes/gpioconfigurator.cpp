/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "gpioconfigurator.hpp"

#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astquery.h"

#include <filesystem>
#include <functional>
#include <map>
#include <set>

using namespace strd;

namespace {

bool isGpioModuleCall(const std::shared_ptr<FunctionNode> &func) {
  const auto &name = func->getName();
  if (name != "DigitalOut" && name != "DigitalIn" && name != "Led") {
    return false;
  }
  auto ns = func->getNamespaceList();
  return ns.empty() || ns.back() == "DigitalIO";
}

std::string fileStem(const std::string &path) {
  return std::filesystem::path(path).stem().string();
}

} // namespace

void GpioConfigurator::processGpio() {
  if (!m_ctx.system) {
    return;
  }

  std::vector<std::shared_ptr<DeclarationNode>> pinDecls;
  std::set<const void *> seen;
  auto collectPinDecl = [&](const ASTNode &node) {
    if (node && node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "gpioPin" &&
          seen.insert(decl.get()).second) {
        pinDecls.push_back(decl);
      }
    }
  };
  for (const auto &child : m_ctx.tree->getChildren()) {
    collectPinDecl(child);
  }
  for (const auto &objects : m_ctx.system->getImportTrees()) {
    for (const auto &node : objects.second) {
      collectPinDecl(node);
    }
  }

  std::vector<std::shared_ptr<StreamNode>> streams;
  std::function<void(const ASTNode &)> findStreams = [&](const ASTNode &node) {
    if (!node) {
      return;
    }
    if (node->getNodeType() == AST::Stream) {
      streams.push_back(std::static_pointer_cast<StreamNode>(node));
      return;
    }
    if (node->getNodeType() == AST::Declaration) {
      for (const auto &prop :
           std::static_pointer_cast<DeclarationNode>(node)->getProperties()) {
        if (prop && prop->getValue() &&
            prop->getValue()->getNodeType() == AST::List) {
          for (const auto &item : prop->getValue()->getChildren()) {
            if (item && item->getNodeType() == AST::Stream) {
              findStreams(item);
            }
          }
        }
      }
    }
  };
  for (const auto &child : m_ctx.tree->getChildren()) {
    findStreams(child);
  }

  bool usesGpioModules = false;
  std::function<void(const ASTNode &)> scan = [&](const ASTNode &node) {
    if (!node || usesGpioModules) {
      return;
    }
    if (node->getNodeType() == AST::Function &&
        isGpioModuleCall(std::static_pointer_cast<FunctionNode>(node))) {
      usesGpioModules = true;
      return;
    }
    if (node->getNodeType() == AST::Stream) {
      auto s = std::static_pointer_cast<StreamNode>(node);
      scan(s->getLeft());
      scan(s->getRight());
    }
  };
  for (const auto &s : streams) {
    scan(s);
  }
  if (pinDecls.empty() && !usesGpioModules) {
    return;
  }

  auto addError = [&](const std::string &file, int line,
                      const std::string &message) {
    m_ctx.addError(LangError::InvalidHardware, file, line, {message});
  };

  auto systems = ASTQuery::getSystemNodes(m_ctx.tree);
  if (systems.empty() || systems.front()->hwPlatforms().empty()) {
    auto where = pinDecls.empty() ? nullptr : pinDecls.front();
    addError(where ? where->getFilename() : m_ctx.tree->getFilename(),
             where ? where->getLine() : 0,
             "gpioPin and the DigitalIO modules need a board: use <Platform> "
             "version 1.0 on <Board>");
    return;
  }

  auto systemNode = systems.front();
  const auto &hardware = m_platformInit.ensureHardwareResolved();
  if (!hardware) {
    addError(systemNode->getFilename(), systemNode->getLine(),
             m_ctx.hardwareResolveError);
    return;
  }

  std::vector<GpioPinConfig> configs;
  configs.reserve(pinDecls.size());
  for (const auto &decl : pinDecls) {
    configs.push_back(GpioPinConfig::fromDeclaration(decl));
  }
  for (const auto &error :
       HardwareResolver::checkGpioPins(*hardware, configs)) {
    addError(error.file, error.line, error.message);
  }
  if (!m_ctx.errors.empty()) {
    return;
  }

  // Attach hardware ISR vectors to interrupt-enabled gpioPins and domains
  std::map<std::string, std::string> pinIsrVectors;
  for (size_t i = 0; i < pinDecls.size(); ++i) {
    if (configs[i].interrupt == "InterruptNone") {
      continue;
    }
    if (auto bp = hardware->boardPin(configs[i].label);
        bp && !bp->interruptVector.empty()) {
      pinDecls[i]->setCompilerProperty(
          "isrVector",
          std::make_shared<ValueNode>(bp->interruptVector,
                                      pinDecls[i]->getFilename().c_str(),
                                      pinDecls[i]->getLine()));
      pinIsrVectors[pinDecls[i]->getName()] = bp->interruptVector;
    }
  }
  if (!pinIsrVectors.empty()) {
    for (const auto &child : m_ctx.tree->getChildren()) {
      if (!child || child->getNodeType() != AST::Declaration) {
        continue;
      }
      auto domDecl = std::static_pointer_cast<DeclarationNode>(child);
      const std::string &et = domDecl->getEntityType();
      if (et != "_domainDefinition" && et != "gameDefinition") {
        continue;
      }
      if (auto trigProp = domDecl->getPropertyValue("trigger")) {
        std::string trigName = ASTQuery::getNodeName(trigProp);
        auto sep = trigName.rfind("::");
        if (sep != std::string::npos) {
          trigName = trigName.substr(sep + 2);
        }
        auto it = pinIsrVectors.find(trigName);
        if (it != pinIsrVectors.end()) {
          domDecl->setCompilerProperty(
              "isrVector",
              std::make_shared<ValueNode>(it->second,
                                          domDecl->getFilename().c_str(),
                                          domDecl->getLine()));
        }
      }
    }
  }

  auto writes = HardwareResolver::pinSetupWrites(*hardware, configs);
  if (!writes.empty()) {
    auto rootDomain =
        ASTQuery::findDeclarationByName("RootDomain", {}, m_ctx.tree, {});
    if (!rootDomain) {
      for (const auto &objects : m_ctx.system->getImportTrees()) {
        rootDomain = ASTQuery::findDeclarationByName(
            "RootDomain", {{nullptr, objects.second}}, nullptr, {});
        if (rootDomain) {
          m_ctx.tree->addChild(rootDomain);
          break;
        }
      }
    }
    if (!rootDomain) {
      addError(systemNode->getFilename(), systemNode->getLine(),
               "the framework has no RootDomain to set the pins up in");
      return;
    }
    auto onLoad = rootDomain->getPropertyValue("onLoad");
    auto list = std::make_shared<ListNode>(__FILE__, __LINE__);
    if (onLoad && onLoad->getNodeType() == AST::List) {
      for (const auto &item : onLoad->getChildren()) {
        list->addChild(item);
      }
    }
    const char *file = __FILE__;
    for (const auto &w : writes) {
      auto args = std::make_shared<ListNode>(file, __LINE__);
      args->addChild(
          std::make_shared<ValueNode>(int64_t(w.address), file, __LINE__));
      args->addChild(
          std::make_shared<ValueNode>(int64_t(w.clearMask), file, __LINE__));
      args->addChild(
          std::make_shared<ValueNode>(int64_t(w.setBits), file, __LINE__));
      args->addChild(
          std::make_shared<ValueNode>(w.readModify, file, __LINE__));
      auto call = std::make_shared<FunctionNode>("_GpioModify", nullptr, file,
                                                 __LINE__);
      list->addChild(std::make_shared<StreamNode>(args, call, file, __LINE__));
    }
    if (onLoad) {
      rootDomain->replacePropertyValue("onLoad", list);
    } else {
      rootDomain->addProperty(
          std::make_shared<PropertyNode>("onLoad", list, file, __LINE__));
    }
  }

  std::map<std::string, std::string> importAliases;
  for (const auto &import : ASTQuery::getImportNodes(m_ctx.tree)) {
    if (!import->importAlias().empty()) {
      auto path = ASTQuery::getImportPath(import);
      auto sep = path.rfind("::");
      importAliases[import->importAlias()] =
          sep == std::string::npos ? path : path.substr(sep + 2);
    }
  }
  auto findPin = [&](const ASTNode &ref) -> int {
    if (!ref || ref->getNodeType() != AST::Entity) {
      return -1;
    }
    auto name = ASTQuery::getNodeName(ref);
    auto ns = ref->getNamespaceList();
    std::string qualifier = ns.empty() ? std::string() : ns.back();
    auto alias = importAliases.find(qualifier);
    if (alias != importAliases.end()) {
      qualifier = alias->second;
    }
    for (size_t i = 0; i < pinDecls.size(); ++i) {
      if (pinDecls[i]->getName() != name) {
        continue;
      }
      if (qualifier.empty() ||
          fileStem(pinDecls[i]->getFilename()) == qualifier) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };

  // ExecutionInterrupt domains, and the WriteReadModify GPIO registers that
  // both an interrupt domain and a cooperative domain write. A cooperative
  // write to such a register gets clearAddress = 0 (!= setAddress), which
  // tells the JIT to guard its read-modify-write against the interrupt.
  std::set<std::string> interruptDomains;
  auto collectInterruptDomain = [&](const ASTNode &n) {
    if (!n || n->getNodeType() != AST::Declaration) {
      return;
    }
    auto d = std::static_pointer_cast<DeclarationNode>(n);
    if (d->getEntityType() != "_domainDefinition" &&
        d->getEntityType() != "gameDefinition") {
      return;
    }
    if (auto execProp = d->getPropertyValue("execution")) {
      bool isIsr = false;
      if (auto origin = execProp->getCompilerProperty("resolvedFrom")) {
        isIsr = origin->getNodeType() == AST::Declaration &&
                std::static_pointer_cast<DeclarationNode>(origin)->getName() ==
                    "ExecutionInterrupt";
      } else if (execProp->getNodeType() == AST::Int) {
        isIsr =
            std::static_pointer_cast<ValueNode>(execProp)->getIntValue() == 1;
      } else {
        isIsr = ASTQuery::getNodeName(execProp).find("ExecutionInterrupt") !=
                std::string::npos;
      }
      if (isIsr) {
        interruptDomains.insert(d->getName());
      }
    }
  };
  for (const auto &child : m_ctx.tree->getChildren()) {
    collectInterruptDomain(child);
  }
  for (const auto &objects : m_ctx.system->getImportTrees()) {
    for (const auto &node : objects.second) {
      collectInterruptDomain(node);
    }
  }

  auto streamIsInterrupt = [&](const std::shared_ptr<StreamNode> &s) {
    std::string domName;
    std::function<void(const ASTNode &)> findDom = [&](const ASTNode &n) {
      if (!n || !domName.empty()) {
        return;
      }
      if (n->getNodeType() == AST::Stream) {
        auto st = std::static_pointer_cast<StreamNode>(n);
        findDom(st->getRight());
        findDom(st->getLeft());
        return;
      }
      std::string d = CodeAnalysis::getNodeDomainName(n, {}, m_ctx.tree);
      size_t pos = d.rfind("::");
      if (pos != std::string::npos) {
        d = d.substr(pos + 2);
      }
      if (!d.empty()) {
        domName = d;
      }
    };
    findDom(s);
    return !domName.empty() && interruptDomains.count(domName) > 0;
  };

  std::set<int64_t> isrWrittenRmwRegs;
  std::set<int64_t> coopWrittenRmwRegs;
  for (const auto &s : streams) {
    const bool isIsr = streamIsInterrupt(s);
    std::function<void(const ASTNode &)> scanWrites = [&](const ASTNode &n) {
      if (!n) {
        return;
      }
      if (n->getNodeType() == AST::Stream) {
        auto st = std::static_pointer_cast<StreamNode>(n);
        scanWrites(st->getLeft());
        scanWrites(st->getRight());
        return;
      }
      if (n->getNodeType() == AST::Function) {
        auto fn = std::static_pointer_cast<FunctionNode>(n);
        if (isGpioModuleCall(fn) && fn->getName() != "DigitalIn") {
          int idx = findPin(fn->getPropertyValue("pin"));
          if (idx >= 0) {
            auto acc =
                HardwareResolver::resolvePinAccess(*hardware, configs[idx]);
            if (acc && (acc->openDrainEmulated ||
                        acc->writeStyle != "WriteSetClear")) {
              int64_t regAddr =
                  acc->openDrainEmulated ? acc->dirAddress : acc->setAddress;
              (isIsr ? isrWrittenRmwRegs : coopWrittenRmwRegs).insert(regAddr);
            }
          }
        }
      }
    };
    scanWrites(s);
  }
  auto needsIsrGuard = [&](int64_t reg, bool isInterruptDomain) {
    return !isInterruptDomain && isrWrittenRmwRegs.count(reg) > 0 &&
           coopWrittenRmwRegs.count(reg) > 0;
  };

  auto expandPinCall = [&](const std::shared_ptr<FunctionNode> &func,
                           const ASTNode &pinRef, bool isInterruptDomain) {
    int index = findPin(pinRef);
    if (index < 0) {
      std::string text = pinRef ? ASTQuery::getNodeName(pinRef) : "nothing";
      addError(func->getFilename(), func->getLine(),
               func->getName() + "'s pin: must name a gpioPin, not " + text);
      return;
    }
    const auto &config = configs[index];
    const bool reads = func->getName() == "DigitalIn";
    if (!reads && config.mode != "PinOutput") {
      addError(func->getFilename(), func->getLine(),
               func->getName() + " drives gpioPin " + config.name +
                   ", whose mode is " + config.mode +
                   ". Only a Gpio::PinOutput pin can be driven");
      return;
    }
    if (reads && config.mode != "PinInput" && config.mode != "PinOutput") {
      addError(func->getFilename(), func->getLine(),
               "DigitalIn reads gpioPin " + config.name + ", whose mode is " +
                   config.mode +
                   ". Only Gpio::PinInput and Gpio::PinOutput pins "
                   "can be read");
      return;
    }
    auto access = HardwareResolver::resolvePinAccess(*hardware, config);
    if (!access) {
      return;
    }
    std::vector<ASTNode> kept;
    for (const auto &child : func->getChildren()) {
      if (child && child->getNodeType() == AST::Property &&
          std::static_pointer_cast<PropertyNode>(child)->getName() == "pin") {
        continue;
      }
      kept.push_back(child);
    }
    func->setChildren(kept);
    const std::string fileName = func->getFilename();
    const char *file = fileName.c_str();
    int line = func->getLine();
    auto setInt = [&](const char *key, int64_t value) {
      func->setPropertyValue(key,
                             std::make_shared<ValueNode>(value, file, line));
    };
    auto setSwitch = [&](const char *key, bool value) {
      func->setPropertyValue(key,
                             std::make_shared<ValueNode>(value, file, line));
    };
    if (reads) {
      setInt("inAddress", access->inAddress);
      setInt("inMask", access->inMask);
      setSwitch("invert", access->invert);
      return;
    }
    if (access->openDrainEmulated) {
      // Released (high) clears the direction bit, low sets it: the level is
      // written inverted to the direction register.
      bool guard = needsIsrGuard(access->dirAddress, isInterruptDomain);
      setInt("setAddress", access->dirAddress);
      setInt("clearAddress", guard ? 0 : access->dirAddress);
      setInt("setMask", access->dirMask);
      setInt("clearMask", access->dirMask);
      setSwitch("readModify", true);
      setSwitch("invert", !access->invert);
    } else {
      bool rmw = access->writeStyle != "WriteSetClear";
      bool guard = rmw && needsIsrGuard(access->setAddress, isInterruptDomain);
      setInt("setAddress", access->setAddress);
      setInt("clearAddress", guard ? 0 : access->clearAddress);
      setInt("setMask", access->setMask);
      setInt("clearMask", access->clearMask);
      setSwitch("readModify", rmw);
      setSwitch("invert", access->invert);
    }
  };

  std::function<void(const ASTNode &, bool)> rewrite =
      [&](const ASTNode &node, bool isInterruptDomain) {
    if (!node || node->getNodeType() != AST::Stream) {
      return;
    }
    auto stream = std::static_pointer_cast<StreamNode>(node);
    auto left = stream->getLeft();
    auto right = stream->getRight();
    ASTNode next = right && right->getNodeType() == AST::Stream
                       ? std::static_pointer_cast<StreamNode>(right)->getLeft()
                       : right;
    if (left && left->getNodeType() == AST::Entity && findPin(left) >= 0 &&
        next && next->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(next);
      if (isGpioModuleCall(func) && func->getName() == "DigitalIn" &&
          !func->getPropertyValue("pin") &&
          right->getNodeType() == AST::Stream) {
        expandPinCall(func, left, isInterruptDomain);
        stream->setLeft(func);
        stream->setRight(
            std::static_pointer_cast<StreamNode>(right)->getRight());
        rewrite(stream, isInterruptDomain);
        return;
      }
    }
    for (const auto &part : {stream->getLeft(), stream->getRight()}) {
      if (part && part->getNodeType() == AST::Function) {
        auto func = std::static_pointer_cast<FunctionNode>(part);
        if (isGpioModuleCall(func)) {
          if (auto pinRef = func->getPropertyValue("pin")) {
            expandPinCall(func, pinRef, isInterruptDomain);
          } else if (!func->getPropertyValue("inAddress") &&
                     !func->getPropertyValue("setAddress")) {
            addError(func->getFilename(), func->getLine(),
                     func->getName() + " needs pin: with a gpioPin");
          }
        }
      }
    }
    rewrite(stream->getRight(), isInterruptDomain);
  };
  for (const auto &s : streams) {
    rewrite(s, streamIsInterrupt(s));
  }
}
