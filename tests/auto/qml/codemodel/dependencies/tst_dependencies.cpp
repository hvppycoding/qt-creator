// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QString>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QLibraryInfo>
#include <QTest>

#include <QDebug>

#include <qmljs/qmljsinterpreter.h>
#include <qmljs/qmljsdocument.h>
#include <qmljs/qmljsbind.h>
#include <qmljs/qmljslink.h>
#include <qmljs/qmljscontext.h>
#include <qmljs/qmljsviewercontext.h>
#include <qmljs/qmljscheck.h>
#include <qmljs/qmljsimportdependencies.h>
#include <qmljs/parser/qmljsast_p.h>
#include <qmljs/parser/qmljsengine_p.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljstools/qmljssemanticinfo.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/filepath.h>

#include <optional>

using namespace QmlJS;
using namespace QmlJS::AST;
using namespace QmlJS::StaticAnalysis;

static QString getValue(const QString &data,
                        const QString &re,
                        const QString &defaultValue = QString::number(0)) {
    const QRegularExpressionMatch m = QRegularExpression(re).match(data);
    return m.hasMatch() ? m.captured(1) : defaultValue;
}

struct TestData
{
    TestData(Document::MutablePtr document, int nSemanticMessages, int nStaticMessages)
        : doc(document)
        , semanticMessages(nSemanticMessages)
        , staticMessages(nStaticMessages)
    {}
    Document::MutablePtr doc;
    const int semanticMessages;
    const int staticMessages;
};

static std::optional<TestData> testData(const QString &path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return {};
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    Document::MutablePtr doc = Document::create(Utils::FilePath::fromString(path), Dialect::Qml);
    doc->setSource(content);
    doc->parse();
    const QString nSemantic = getValue(content, "//\\s*ExpectedSemanticMessages: (\\d+)");
    const QString nStatic = getValue(content, "//\\s*ExpectedStaticMessages: (\\d+)");
    return TestData(doc, nSemantic.toInt(), nStatic.toInt());
}

void printUnexpectedMessages(const QmlJSTools::SemanticInfo &info, int nSemantic, int nStatic)
{
    if (nSemantic == 0 && info.semanticMessages.length() > 0)
        for (auto msg: info.semanticMessages)
            qDebug() << msg.message;
    if (nStatic == 0 && info.staticAnalysisMessages.length() > 0)
        for (auto msg: info.staticAnalysisMessages)
            qDebug() << msg.message;
    return;
}

class tst_Dependencies : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void test_data();
    void test();

private:
    QString m_path;
    QStringList m_basePaths;
};

void tst_Dependencies::initTestCase()
{
    m_path = QLatin1String(TESTSRCDIR "/samples");

    m_basePaths.append(QLibraryInfo::path(QLibraryInfo::Qml2ImportsPath));

    if (!ModelManagerInterface::instance())
        new ModelManagerInterface;

    if (!ExtensionSystem::PluginManager::instance())
        new ExtensionSystem::PluginManager;
}

void tst_Dependencies::test_data()
{
    QTest::addColumn<QString>("filename");
    QTest::addColumn<int>("nSemanticMessages");
    QTest::addColumn<int>("nStaticMessages");

    QDirIterator it(m_path, QDir::Files);
    while (it.hasNext()) {
        const QString &filepath = it.next();
        const QString name = getValue(filepath, ".*/(.*).qml", filepath);
        QTest::newRow(name.toLatin1().data()) << filepath;
    }
}

void tst_Dependencies::test()
{
    QFETCH(QString, filename);

    ModelManagerInterface *modelManager = ModelManagerInterface::instance();

    PathsAndLanguages lPaths;
    QStringList paths(m_basePaths);
    paths << m_path;
    for (auto p: std::as_const(paths))
        lPaths.maybeInsert(Utils::FilePath::fromString(p), Dialect::Qml);
    ModelManagerInterface::importScan(ModelManagerInterface::workingCopy(), lPaths,
                                      ModelManagerInterface::instance(), false);
    ModelManagerInterface::instance()->test_joinAllThreads();
    const auto data = testData(filename);
    QVERIFY(data);
    Document::MutablePtr doc = data->doc;
    int nExpectedSemanticMessages = data->semanticMessages;
    int nExpectedStaticMessages = data->staticMessages;
    QVERIFY(!doc->source().isEmpty());

    Snapshot snapshot = modelManager->snapshot();

    QmlJSTools::SemanticInfo semanticInfo;
    semanticInfo.document = doc;
    semanticInfo.snapshot = snapshot;

    Link link(semanticInfo.snapshot, modelManager->defaultVContext(doc->language(), doc),
              modelManager->builtins(doc));

    semanticInfo.context = link(doc, &semanticInfo.semanticMessages);

    ScopeChain *scopeChain = new ScopeChain(doc, semanticInfo.context);
    semanticInfo.setRootScopeChain(QSharedPointer<const ScopeChain>(scopeChain));

    Check checker(doc, semanticInfo.context);
    semanticInfo.staticAnalysisMessages = checker();

    printUnexpectedMessages(semanticInfo, nExpectedSemanticMessages, nExpectedStaticMessages);

    QCOMPARE(semanticInfo.semanticMessages.length(), nExpectedSemanticMessages);
    QCOMPARE(semanticInfo.staticAnalysisMessages.length(), nExpectedStaticMessages);
}

QTEST_GUILESS_MAIN(tst_Dependencies)

#include "tst_dependencies.moc"
