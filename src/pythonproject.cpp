/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2022-2026 Andres Cabrera
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

    Based on BSD/MIT code by Andres Cabrera and Joseph Tilbian.

    Author: Andres Cabrera
*/

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include "codeanalysis.hpp"
#include "codevalidator.h"
#include "pythonproject.h"
#include "stridesystem.hpp"

PythonProject::PythonProject(std::string platformName, std::string platformPath,
                             std::string strideRoot, std::string projectDir,
                             std::string pythonExecutable)
    : Builder(projectDir, strideRoot, platformPath),
      m_platformName(platformName), m_runningProcess(this), m_buildProcess(this)

{
  if (pythonExecutable.size() == 0) {
    m_pythonExecutable = "python";
  } else {
    m_pythonExecutable = pythonExecutable;
  }

  m_jsonFilename = m_projectDir + "/tree-" + platformName + ".json";

  QObject::connect(&m_buildProcess, SIGNAL(readyReadStandardOutput()), this,
                   SLOT(consoleMessage()));
  QObject::connect(&m_buildProcess, SIGNAL(readyReadStandardError()), this,
                   SLOT(consoleMessage()));
  QObject::connect(&m_runningProcess, SIGNAL(readyReadStandardOutput()), this,
                   SLOT(consoleMessage()));
  QObject::connect(&m_runningProcess, SIGNAL(readyReadStandardError()), this,
                   SLOT(consoleMessage()));
}

PythonProject::~PythonProject() {
  m_building.storeRelaxed(0);
  m_buildProcess.kill();
  m_buildProcess.waitForFinished();

  m_running.storeRelaxed(0);
  m_runningProcess.kill();
  m_runningProcess.waitForFinished();
}

std::map<std::string, std::string> PythonProject::generateCode(ASTNode tree) {
  writeAST(tree); // Write configuration file to json
  //    QJsonDocument configJson = QJsonDocument::fromVariant(m_configuration);
  //    QFile configFile(m_projectDir + QDir::separator() + "config.json");
  //    if (configFile.open(QIODevice::WriteOnly)) {
  //        configFile.write(configJson.toJson());
  //        configFile.close();
  //    }
  return std::map<std::string, std::string>();
}

bool PythonProject::build(std::map<std::string, std::string> domainMap) {
  QStringList arguments;
  if (m_buildProcess.state() == QProcess::Running) {
    m_buildProcess.close();
    if (!m_buildProcess.waitForFinished(5000)) {
      qDebug() << "Could not stop build process. Not starting again.";
      return false;
    }
  }

  // Start Build
  m_stdErr.clear();
  m_stdOut.clear();
  m_buildProcess.setWorkingDirectory(QString::fromStdString(m_strideRoot));
  // FIXME un hard-code library version
  arguments << "library/1.0/python/build.py"
            << QString::fromStdString(m_jsonFilename)
            << QString::fromStdString(m_projectDir)
            << QString::fromStdString(m_strideRoot) << "build";
  m_buildProcess.start(QString::fromStdString(m_pythonExecutable), arguments);

  m_buildProcess.waitForStarted(15000);
  //    qDebug() << "pid:" << m_buildProcess.pid();
  m_building.storeRelaxed(1);
  while (m_building.loadRelaxed() == 1) {
    if (m_buildProcess.waitForFinished(50)) {
      m_building.storeRelaxed(0);
    }
    qApp->processEvents();
  }

  qApp->processEvents();
  if (m_buildProcess.exitStatus() == QProcess::ExitStatus::NormalExit &&
      m_buildProcess.exitCode() == 0) {
    emit outputText("Done building. Success.");
    return true;
  } else {
    emit outputText("Done building. Failed.");
    return false;
  }
}

bool PythonProject::run(bool pressed) {
  if (!pressed) {
    stopRunning();
    return false;
  }
  QStringList arguments;
  if (m_runningProcess.state() == QProcess::Running) {
    m_runningProcess.close();
    if (!m_runningProcess.waitForFinished(5000)) {
      qDebug() << "Could not stop run process. Not starting again.";
      return false;
    }
  }
  m_stdErr.clear();
  m_stdOut.clear();
  m_runningProcess.setWorkingDirectory(QString::fromStdString(m_strideRoot));
  // FIXME un hard-code library version
  arguments << "library/1.0/python/build.py"
            << QString::fromStdString(m_jsonFilename)
            << QString::fromStdString(m_projectDir)
            << QString::fromStdString(m_strideRoot) << "build";
  m_runningProcess.start(QString::fromStdString(m_pythonExecutable), arguments);

  qDebug() << arguments;

  m_runningProcess.waitForStarted(15000);
  //  qDebug() << "run pid:" << m_runningProcess.pid();
  m_running.storeRelaxed(1);
  while (m_running.loadRelaxed() == 1) {
    if (m_runningProcess.waitForFinished(50)) {
      m_running.storeRelaxed(0);
    }
    qApp->processEvents();
  }
  emit programStopped();
  qApp->processEvents();
  if (m_runningProcess.exitStatus() == QProcess::ExitStatus::NormalExit &&
      m_runningProcess.exitCode() == 0) {
    emit outputText("Done running.");
    return true;
  } else {
    emit outputText("Abnormal run exit.");
    return false;
  }
}

void PythonProject::stopRunning() {
  m_building.storeRelaxed(0);
  m_running.storeRelaxed(0);
  if (m_buildProcess.state() == QProcess::Running) {
    m_buildProcess.kill(); // Taking too long...
  }
  if (m_runningProcess.state() == QProcess::Running) {
    m_runningProcess.kill(); // Taking too long...
  }
  //    qDebug() << "Stopping.";
  // m_runningProcess.waitForFinished();
}

void PythonProject::writeAST(ASTNode tree) {
  QJsonArray treeObject;
  for (ASTNode node : tree->getChildren()) {
    QJsonObject nodeObject;
    if (node->getNodeType() == AST::Platform ||
        node->getNodeType() == AST::Stream ||
        node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      astToJson(node, nodeObject);
    }
    treeObject.append(nodeObject);
  }
  QFile saveFile(QString::fromStdString(m_jsonFilename));

  if (!saveFile.open(QIODevice::WriteOnly)) {
    qWarning("Couldn't open save file.");
    return;
  }
  QJsonDocument saveDoc(treeObject);
  saveFile.write(saveDoc.toJson());
}

void PythonProject::astToJson(ASTNode node, QJsonObject &obj) {
  if (node->getNodeType() == AST::Array) {
    QJsonObject newObj;
    newObj["type"] = QString("Bundle");
    newObj["name"] = QString::fromStdString(
        static_cast<ArrayNode *>(node.get())->getName());
    newObj["filename"] = QString::fromStdString(node->getFilename());
    newObj["line"] = node->getLine();

    ListNode *indexList = static_cast<ArrayNode *>(node.get())->index().get();
    Q_ASSERT(indexList->size() == 1);
    AST *indexNode = indexList->getChildren().at(0).get();
    if (indexNode->getNodeType() == AST::Int) {
      newObj["index"] = static_cast<long long>(
          static_cast<ValueNode *>(indexNode)->getIntValue());
    } else if (indexNode->getNodeType() == AST::Entity) {
      newObj["index"] = QString::fromStdString(
          static_cast<EntityNode *>(indexNode)->getName());
    } else if (indexNode->getNodeType() == AST::List) {
      // FIXME implement support for Lists
    } else if (indexNode->getNodeType() == AST::Range) {
      // FIXME implement support for Range
      // Are ranges and lists always unraveled by the compiler?
    }
    newObj["rate"] = CodeAnalysis::getNodeRate(node);
    obj["bundle"] = newObj;
  } else if (node->getNodeType() == AST::Entity) {
    QJsonObject newObj;
    newObj["name"] = QString::fromStdString(
        static_cast<EntityNode *>(node.get())->getName());
    newObj["filename"] = QString::fromStdString(node->getFilename());
    newObj["line"] = node->getLine();
    obj["name"] = newObj;
  } else if (node->getNodeType() == AST::Expression) {
    QJsonObject newObj;
    expressionToJson(std::static_pointer_cast<ExpressionNode>(node), newObj);
    newObj["filename"] = QString::fromStdString(node->getFilename());
    newObj["line"] = node->getLine();
    obj["expression"] = newObj;
  } else if (node->getNodeType() == AST::Function) {
    QJsonObject newObj;
    functionToJson(std::static_pointer_cast<FunctionNode>(node), newObj);
    newObj["filename"] = QString::fromStdString(node->getFilename());
    newObj["line"] = node->getLine();
    obj["function"] = newObj;
  } else if (node->getNodeType() == AST::Stream) {
    QJsonArray array;
    streamToJsonArray(std::static_pointer_cast<StreamNode>(node), array);
    obj["stream"] = array;
  } else if (node->getNodeType() == AST::Int) {
    QJsonObject newObj;
    obj["value"] = static_cast<long long>(
        static_cast<ValueNode *>(node.get())->getIntValue());
    QJsonObject domainObj;
    astToJson(static_cast<ValueNode *>(node.get())->getDomain(), domainObj);
    obj["domain"] = domainObj;
  } else if (node->getNodeType() == AST::Real) {
    obj["value"] = static_cast<ValueNode *>(node.get())->getRealValue();
    QJsonObject domainObj;
    astToJson(static_cast<ValueNode *>(node.get())->getDomain(), domainObj);
    obj["domain"] = domainObj;
  } else if (node->getNodeType() == AST::String) {
    obj["value"] = QString::fromStdString(
        static_cast<ValueNode *>(node.get())->getStringValue());
    QJsonObject domainObj;
    astToJson(static_cast<ValueNode *>(node.get())->getDomain(), domainObj);
    obj["domain"] = domainObj;
  } else if (node->getNodeType() == AST::Switch) {
    obj["value"] = static_cast<ValueNode *>(node.get())->getSwitchValue();
  } else if (node->getNodeType() == AST::Declaration) {
    DeclarationNode *decl = static_cast<DeclarationNode *>(node.get());
    QJsonObject newObject;
    newObject["name"] = QString::fromStdString(decl->getName());
    newObject["type"] = QString::fromStdString(decl->getEntityType());
    std::vector<std::string> nsList = decl->getNamespaceList();
    QString ns;
    for (std::string name : nsList) {
      ns += QString::fromStdString(name) + "::";
    }
    ns.chop(2);
    newObject["namespace"] = ns;
    std::vector<std::shared_ptr<PropertyNode>> props = decl->getProperties();
    QJsonObject propObject;
    for (auto prop : props) {
      ASTNode propValue = prop->getValue();
      QJsonObject valueObject;
      astToJson(propValue, valueObject);
      if (!valueObject.isEmpty()) {
        propObject[QString::fromStdString(prop->getName())] = valueObject;
      } else {
        propObject[QString::fromStdString(prop->getName())] = QJsonValue();
      }
      // TODO use astToJson here instead.
      if (propValue->getNodeType() == AST::Int) {
        propObject[QString::fromStdString(prop->getName())] =
            static_cast<long long>(
                static_cast<ValueNode *>(propValue.get())->getIntValue());
      } else if (propValue->getNodeType() == AST::Real) {
        propObject[QString::fromStdString(prop->getName())] =
            static_cast<ValueNode *>(propValue.get())->getRealValue();
      } else if (propValue->getNodeType() == AST::String) {
        propObject[QString::fromStdString(prop->getName())] =
            QString::fromStdString(
                static_cast<ValueNode *>(propValue.get())->getStringValue());
      } else if (propValue->getNodeType() == AST::Expression) {
        // TODO complete this
      } else if (propValue->getNodeType() == AST::Entity) {
        QJsonObject nameObject;
        astToJson(propValue, nameObject);
        propObject[QString::fromStdString(prop->getName())] = nameObject;
      } else if (propValue->getNodeType() == AST::List) {
        QJsonArray list;
        listToJsonArray(std::static_pointer_cast<ListNode>(propValue), list);
        propObject[QString::fromStdString(prop->getName())] = list;
      }
    }
    newObject["ports"] = propObject;
    newObject["filename"] = QString::fromStdString(node->getFilename());
    newObject["line"] = node->getLine();
    obj["entity"] = newObject;
  } else if (node->getNodeType() == AST::ArrayDeclaration) {
    DeclarationNode *decl = static_cast<DeclarationNode *>(node.get());
    QJsonObject newObject;
    std::shared_ptr<ArrayNode> arrayNode = decl->getArrayIndex();
    newObject["name"] = QString::fromStdString(arrayNode->getName());
    newObject["type"] = QString::fromStdString(decl->getEntityType());
    std::vector<std::string> nsList = decl->getNamespaceList();
    QString ns;
    for (std::string name : nsList) {
      ns += QString::fromStdString(name) + "::";
    }
    ns.chop(2);
    newObject["namespace"] = ns;
    ListNode *indexList = arrayNode->index().get();
    Q_ASSERT(indexList->size() == 1);
    AST *arrayIndex = indexList->getChildren().at(0).get();
    if (arrayIndex->getNodeType() == AST::Int ||
        arrayIndex->getNodeType() == AST::Real) {
      newObject["size"] = static_cast<long long>(
          static_cast<ValueNode *>(arrayIndex)->getIntValue());
    } else if (arrayIndex->getNodeType() == AST::Entity) {
      // FIXME we need to set the value from the name (it must be a constant)
      //            newObject["size"] =
      //            QString::fromStdString(static_cast<NameNode
      //            *>(arrayIndex)->getName());
      newObject["size"] = 8;
    } else {
      qDebug() << "Type for index not implemented.";
      // TODO Implement support for more index types
    }
    std::vector<std::shared_ptr<PropertyNode>> props = decl->getProperties();
    //        QJsonObject propertiesObj;
    QJsonObject propObject;
    for (auto prop : props) {
      ASTNode propValue = prop->getValue();
      QJsonObject valueObject;
      astToJson(propValue, valueObject);
      if (!valueObject.isEmpty()) {
        propObject[QString::fromStdString(prop->getName())] = valueObject;
      } else {
        propObject[QString::fromStdString(prop->getName())] = QJsonValue();
      }
      // TODO use astToJson here instead.
      if (propValue->getNodeType() == AST::Int) {
        propObject[QString::fromStdString(prop->getName())] =
            static_cast<long long>(
                static_cast<ValueNode *>(propValue.get())->getIntValue());
      } else if (propValue->getNodeType() == AST::Real) {
        propObject[QString::fromStdString(prop->getName())] =
            static_cast<ValueNode *>(propValue.get())->getRealValue();
      } else if (propValue->getNodeType() == AST::String) {
        propObject[QString::fromStdString(prop->getName())] =
            QString::fromStdString(
                static_cast<ValueNode *>(propValue.get())->getStringValue());
      } else if (propValue->getNodeType() == AST::Expression) {
        // TODO complete this
      } else if (propValue->getNodeType() == AST::Entity) {
        QJsonObject nameObject;
        astToJson(propValue, nameObject);
        propObject[QString::fromStdString(prop->getName())] = nameObject;
      } else if (propValue->getNodeType() == AST::List) {
        QJsonArray list;
        listToJsonArray(std::static_pointer_cast<ListNode>(propValue), list);
        propObject[QString::fromStdString(prop->getName())] = list;
      }
    }
    newObject["ports"] = propObject;
    newObject["filename"] = QString::fromStdString(node->getFilename());
    newObject["line"] = node->getLine();
    obj["entitybundle"] = newObject;
  } else if (node->getNodeType() == AST::List) {
    QJsonArray list;
    listToJsonArray(std::static_pointer_cast<ListNode>(node), list);
    obj["list"] = list;
  } else if (node->getNodeType() == AST::None) {
    obj = QJsonObject(); // Null value
  } else if (node->getNodeType() == AST::MemberAccess) {
    QJsonObject newObj;
    newObj["name"] = QString::fromStdString(
        static_cast<MemberAccessNode *>(node.get())->getEntity());
    newObj["portname"] = QString::fromStdString(
        static_cast<MemberAccessNode *>(node.get())->getPropertyName());
    obj["portproperty"] = newObj;
  } else if (node->getNodeType() == AST::Platform) {
    QJsonObject newObj;
    newObj["name"] = QString::fromStdString(
        static_cast<SystemNode *>(node.get())->platformName());
    newObj["majorVersion"] =
        static_cast<SystemNode *>(node.get())->majorVersion();
    newObj["minorVersion"] =
        static_cast<SystemNode *>(node.get())->minorVersion();
    QJsonObject platformInfo;
    platformInfo["path"] = QString::fromStdString(m_platformPath);
    platformInfo["name"] = QString::fromStdString(m_platformName);
    QJsonArray platformList;
    platformList.append(platformInfo);
    newObj["platforms"] = platformList;
    obj["system"] = newObj;
  } else {
    obj["type"] = QString("Unsupported");
  }
}

void PythonProject::listToJsonArray(std::shared_ptr<ListNode> listNode,
                                    QJsonArray &obj) {
  foreach (ASTNode element, listNode->getChildren()) {
    QJsonObject jsonElement;
    astToJson(element, jsonElement);
    obj.append(jsonElement);
  }
}

void PythonProject::streamToJsonArray(std::shared_ptr<StreamNode> node,
                                      QJsonArray &array) {
  Q_ASSERT(node->getNodeType() == AST::Stream);
  QJsonObject leftObject;
  astToJson(node->getLeft(), leftObject);
  array.append(leftObject);
  if (node->getRight()->getNodeType() == AST::Stream) {
    appendStreamToArray(node->getRight(), array);
  } else {
    QJsonObject rightObject;
    astToJson(node->getRight(), rightObject);
    array.append(rightObject);
  }
}

void PythonProject::functionToJson(std::shared_ptr<FunctionNode> node,
                                   QJsonObject &obj) {
  obj["name"] = QString::fromStdString(node->getName());
  obj["type"] = QString("Function");
  std::vector<std::shared_ptr<PropertyNode>> properties = node->getProperties();
  QJsonObject propObject;
  for (auto property : properties) {
    QJsonObject propValue;
    astToJson(property->getValue(), propValue);
    if (!propValue.isEmpty()) {
      propObject[QString::fromStdString(property->getName())] = propValue;
    } else {
      propObject[QString::fromStdString(property->getName())] = QJsonValue();
    }
  }
  obj["ports"] = propObject;
  obj["rate"] = CodeAnalysis::getNodeRate(node);
  auto inputBlock = node->getCompilerProperty("inputEntity");
  if (inputBlock) {
    if (inputBlock->getNodeType() == AST::Entity) {
      obj["inputEntity"] = QString::fromStdString(
          std::static_pointer_cast<EntityNode>(inputBlock)->getName());
    } else if (inputBlock->getNodeType() == AST::Array) {
      obj["inputEntity"] = QString::fromStdString(
          std::static_pointer_cast<ArrayNode>(inputBlock)->getName());
    }
  }
  auto outputBlock = node->getCompilerProperty("outputEntity");
  if (outputBlock) {
    if (outputBlock->getNodeType() == AST::Entity) {
      obj["outputEntity"] = QString::fromStdString(
          std::static_pointer_cast<EntityNode>(outputBlock)->getName());
    } else if (outputBlock->getNodeType() == AST::Array) {
      obj["outputEntity"] = QString::fromStdString(
          std::static_pointer_cast<ArrayNode>(outputBlock)->getName());
    }
  }
}

void PythonProject::expressionToJson(std::shared_ptr<ExpressionNode> node,
                                     QJsonObject &obj) {
  obj["type"] = QString::fromStdString(node->getExpressionTypeString());

  if (node->isUnary()) {
    QJsonObject value;
    astToJson(node->getValue(), value);
    if (!value.isEmpty()) {
      obj["value"] = value;
    }
  } else {
    QJsonObject left;
    astToJson(node->getLeft(), left);
    obj["left"] = left.isEmpty() ? QJsonValue() : left;
    QJsonObject right;
    astToJson(node->getRight(), right);
    obj["right"] = right.isEmpty() ? QJsonValue() : right;
  }
}

void PythonProject::appendStreamToArray(ASTNode node, QJsonArray &array) {
  QJsonObject newObj;
  astToJson(node, newObj);
  if (!newObj["stream"].toArray().isEmpty()) {
    foreach (QJsonValue value, newObj["stream"].toArray()) {
      array.append(value);
    }
  }
}

bool PythonProject::isValid() { return true; }

void PythonProject::consoleMessage() {
  QByteArray stdOut;
  QByteArray stdErr;
  if (m_running.loadRelaxed() == 1) {
    stdOut = m_runningProcess.readAllStandardOutput();
    stdErr = m_runningProcess.readAllStandardError();
  } else if (m_building.loadRelaxed() == 1) {
    stdOut = m_buildProcess.readAllStandardOutput();
    stdErr = m_buildProcess.readAllStandardError();
  } else {
    qDebug() << "WARNING: consoleMessage() called but nothing running";
  }
  if (!stdOut.isEmpty()) {
    m_stdOut.append(stdOut);
    emit outputText(stdOut);
  }
  if (!stdErr.isEmpty()) {
    m_stdErr.append(stdErr);
    emit errorText(stdErr);
  }
}
