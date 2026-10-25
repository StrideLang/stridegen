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

#ifndef PYTHONPROJECT_H
#define PYTHONPROJECT_H

#include <QAtomicInt>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

#include "stride/stridegen/builder.hpp"

#include "stride/parser/ast.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/streamnode.h"

class PythonProject : public Builder {
  Q_OBJECT
public:
  explicit PythonProject(std::string platformName, std::string platformPath,
                         std::string strideRoot,
                         std::string projectDir = std::string(),
                         std::string pythonExecutable = std::string());
  virtual ~PythonProject();

  std::string getStdErr() const override { return m_stdErr.toStdString(); }
  std::string getStdOut() const override { return m_stdOut.toStdString(); }
  void clearBuffers() override {
    m_stdErr = "";
    m_stdOut = "";
  }

signals:

public slots:

  virtual std::map<std::string, std::string>
  generateCode(ASTNode tree) override;
  virtual bool build(std::map<std::string, std::string> domainMap) override;
  virtual bool deploy() override { return true; }
  virtual bool run(bool pressed = true) override;
  virtual bool isValid() override;

  void consoleMessage();

  void stopRunning();

private:
  void writeAST(ASTNode tree);
  void astToJson(ASTNode node, QJsonObject &obj);
  void listToJsonArray(std::shared_ptr<ListNode> node, QJsonArray &obj);
  void streamToJsonArray(std::shared_ptr<StreamNode> node, QJsonArray &array);
  void functionToJson(std::shared_ptr<FunctionNode> node, QJsonObject &obj);
  void expressionToJson(std::shared_ptr<ExpressionNode> node, QJsonObject &obj);
  void appendStreamToArray(ASTNode node, QJsonArray &array);

  std::string m_platformName;
  std::string m_pythonExecutable;
  std::string m_jsonFilename;
  QAtomicInt m_running;
  QProcess m_runningProcess;
  QAtomicInt m_building;
  QProcess m_buildProcess;

  QString m_stdOut;
  QString m_stdErr;
};

#endif // PYTHONPROJECT_H
