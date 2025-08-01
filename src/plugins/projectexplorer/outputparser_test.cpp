// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "ioutputparser.h"
#include "outputparser_test.h"
#include "task.h"
#include "taskhub.h"

#include <QTest>

namespace ProjectExplorer {

class TestTerminator final : public OutputTaskParser
{
public:
    explicit TestTerminator(OutputParserTester *t)
        : m_tester(t)
    {
        if (!t->lineParsers().isEmpty()) {
            for (const Utils::FilePath &searchDir : t->lineParsers().constFirst()->searchDirectories())
                addSearchDir(searchDir);
        }
    }

private:
    Result handleLine(const QString &line, Utils::OutputFormat type) final
    {
        if (type == Utils::StdOutFormat)
            m_tester->m_receivedStdOutChildLines << line;
        else
            m_tester->m_receivedStdErrChildLines << line;
        return Status::Done;
    }

    OutputParserTester *m_tester = nullptr;
};

static inline QByteArray msgFileComparisonFail(const Utils::FilePath &f1, const Utils::FilePath &f2)
{
    const QString result = '"' + f1.toUserOutput() + "\" != \"" + f2.toUserOutput() + '"';
    return result.toLocal8Bit();
}

// test functions:
OutputParserTester::OutputParserTester()
{
    connect(&taskHub(), &TaskHub::taskAdded, this, [this](const Task &t) {
        m_receivedTasks.append(t);
    });
}

OutputParserTester::~OutputParserTester()
{
    taskHub().disconnect(this);
}

void OutputParserTester::testParsing(const QString &input,
                                     Channel inputChannel,
                                     Tasks tasks,
                                     const QStringList &childStdOutLines,
                                     const QStringList &childStdErrLines)
{
    for (Utils::OutputLineParser * const parser : lineParsers())
        parser->skipFileExistsCheck();
    const auto terminator = new TestTerminator(this);
    if (!lineParsers().isEmpty())
        terminator->setRedirectionDetector(lineParsers().constLast());
    addLineParser(terminator);
    reset();

    appendMessage(input, inputChannel == STDOUT ? Utils::StdOutFormat : Utils::StdErrFormat);
    flush();

    // delete the parser(s) to test
    emit aboutToDeleteParser();
    setLineParsers({});

    QCOMPARE(m_receivedStdErrChildLines, childStdErrLines);
    QCOMPARE(m_receivedStdOutChildLines, childStdOutLines);
    QCOMPARE(m_receivedTasks.size(), tasks.size());
    if (m_receivedTasks.size() == tasks.size()) {
        for (int i = 0; i < tasks.size(); ++i) {
            QCOMPARE(m_receivedTasks.at(i).category(), tasks.at(i).category());
            if (m_receivedTasks.at(i).description() != tasks.at(i).description()) {
                qDebug() << "---" << tasks.at(i).description();
                qDebug() << "+++" << m_receivedTasks.at(i).description();
            }
            QCOMPARE(m_receivedTasks.at(i).description(), tasks.at(i).description());
            QVERIFY2(m_receivedTasks.at(i).file() == tasks.at(i).file(),
                     msgFileComparisonFail(m_receivedTasks.at(i).file(), tasks.at(i).file()));
            QCOMPARE(m_receivedTasks.at(i).line(), tasks.at(i).line());
            QCOMPARE(m_receivedTasks.at(i).column(), tasks.at(i).column());
            QCOMPARE(static_cast<int>(m_receivedTasks.at(i).type()), static_cast<int>(tasks.at(i).type()));
            // Skip formats check if we haven't specified expected
            if (tasks.at(i).formats().size() == 0)
                continue;
            QCOMPARE(m_receivedTasks.at(i).formats().size(), tasks.at(i).formats().size());
            for (int j = 0; j < tasks.at(i).formats().size(); ++j) {
                QCOMPARE(m_receivedTasks.at(i).formats().at(j).start, tasks.at(i).formats().at(j).start);
                QCOMPARE(m_receivedTasks.at(i).formats().at(j).length, tasks.at(i).formats().at(j).length);
                QCOMPARE(m_receivedTasks.at(i).formats().at(j).format.anchorHref(), tasks.at(i).formats().at(j).format.anchorHref());
            }
        }
    }
}

void OutputParserTester::setDebugEnabled(bool debug)
{
    m_debug = debug;
}

void OutputParserTester::reset()
{
    m_receivedStdErrChildLines.clear();
    m_receivedStdOutChildLines.clear();
    m_receivedTasks.clear();
}

class OutputParserTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAnsiFilterOutputParser_data();
    void testAnsiFilterOutputParser();
};

void OutputParserTest::testAnsiFilterOutputParser_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<OutputParserTester::Channel>("inputChannel");
    QTest::addColumn<QStringList>("childStdOutLines");
    QTest::addColumn<QStringList>("childStdErrLines");

    QTest::newRow("pass-through stdout")
            << QString::fromLatin1("Sometext") << OutputParserTester::STDOUT
            << QStringList("Sometext") << QStringList();
    QTest::newRow("pass-through stderr")
            << QString::fromLatin1("Sometext") << OutputParserTester::STDERR
            << QStringList() << QStringList("Sometext");

    QString input = QString::fromLatin1("te") + QChar(27) + QString::fromLatin1("Nst");
    QTest::newRow("ANSI: ESC-N")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
    input = QString::fromLatin1("te") + QChar(27) + QLatin1String("^ignored") + QChar(27) + QLatin1String("\\st");
    QTest::newRow("ANSI: ESC-^ignoredESC-\\")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
    input = QString::fromLatin1("te") + QChar(27) + QLatin1String("]0;ignored") + QChar(7) + QLatin1String("st");
    QTest::newRow("ANSI: window title change")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
    input = QString::fromLatin1("te") + QChar(27) + QLatin1String("[Ast");
    QTest::newRow("ANSI: cursor up")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
    input = QString::fromLatin1("te") + QChar(27) + QLatin1String("[2Ast");
    QTest::newRow("ANSI: cursor up (with int parameter)")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
    input = QString::fromLatin1("te") + QChar(27) + QLatin1String("[2;3Hst");
    QTest::newRow("ANSI: position cursor")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
    input = QString::fromLatin1("te") + QChar(27) + QLatin1String("[31;1mst");
    QTest::newRow("ANSI: bold red")
            << input << OutputParserTester::STDOUT
            << QStringList("test") << QStringList();
}

void OutputParserTest::testAnsiFilterOutputParser()
{
    OutputParserTester testbench;
    QFETCH(QString, input);
    QFETCH(OutputParserTester::Channel, inputChannel);
    QFETCH(QStringList, childStdOutLines);
    QFETCH(QStringList, childStdErrLines);

    testbench.testParsing(input, inputChannel, Tasks(), childStdOutLines, childStdErrLines);
}

QObject *createOutputParserTest()
{
    return new OutputParserTest;
}

} // namespace ProjectExplorer

#include "outputparser_test.moc"

#endif // WITH_TESTS
