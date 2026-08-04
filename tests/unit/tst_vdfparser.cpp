// The hand-written VDF parser reads every Steam config the app touches:
// libraryfolders.vdf, appmanifest_*.acf, localconfig.vdf, config.vdf and
// toolmanifest.vdf. Valve's format has no specification, so what matters here
// is that the accepted dialect stays exactly as wide as it is today.

#include <QTest>
#include <QTemporaryDir>
#include <QFile>

#include "parsers/VDFParser.h"

class TstVdfParser : public QObject
{
    Q_OBJECT

private slots:
    void parsesRealLibraryFolders();
    void parsesRealAppManifest();
    void nestedObjects();
    void skipsComments();
    void escapes_data();
    void escapes();
    void unquotedKeys();
    void emptyValueIsNeitherValueNorObject();
    void rejects_data();
    void rejects();
    void getIntFallsBackOnGarbage();
    void parseFileReadsFromDisk();
    void missingFileFails();
};

void TstVdfParser::parsesRealLibraryFolders()
{
    // Shape taken verbatim from a live install.
    const QString content = R"("libraryfolders"
{
	"0"
	{
		"path"		"/home/user/.local/share/Steam"
		"label"		""
		"contentid"		"6386869439825518753"
		"totalsize"		"0"
		"apps"
		{
			"228980"		"128606066"
			"1245620"		"52000000000"
		}
	}
	"1"
	{
		"path"		"/mnt/games/SteamLibrary"
		"apps"
		{
		}
	}
}
)";

    VDFParser parser;
    QVERIFY2(parser.parse(content), qPrintable(parser.errorString()));

    const VDFNode root = parser.root();
    QVERIFY(root.hasChild("libraryfolders"));

    const VDFNode folders = root.child("libraryfolders");
    QCOMPARE(folders.child("0").getString("path"), QString("/home/user/.local/share/Steam"));
    QCOMPARE(folders.child("1").getString("path"), QString("/mnt/games/SteamLibrary"));
    QCOMPARE(folders.child("0").getInt("contentid"), 6386869439825518753LL);
    QCOMPARE(folders.child("0").child("apps").getInt("1245620"), 52000000000LL);

    // A library with no games in it still has to yield its path — that is all
    // SteamLauncher::libraryPaths() reads. (The empty "apps" block itself is
    // neither value nor object; see emptyValueIsNeitherValueNorObject.)
    QVERIFY(folders.hasChild("1"));
    QVERIFY(folders.child("1").hasChild("apps"));
}

void TstVdfParser::parsesRealAppManifest()
{
    const QString content = R"("AppState"
{
	"appid"		"1245620"
	"name"		"ELDEN RING"
	"StateFlags"		"6"
	"installdir"		"ELDEN RING"
	"SizeOnDisk"		"52000000000"
	"buildid"		"24407790"
	"InstalledDepots"
	{
		"1245621"
		{
			"manifest"		"123"
			"size"		"456"
		}
	}
}
)";

    VDFParser parser;
    QVERIFY2(parser.parse(content), qPrintable(parser.errorString()));

    const VDFNode app = parser.root().child("AppState");
    QCOMPARE(app.getString("appid"), QString("1245620"));
    QCOMPARE(app.getString("name"), QString("ELDEN RING"));
    QCOMPARE(app.getInt("StateFlags"), 6LL);
    QCOMPARE(app.getInt("SizeOnDisk"), 52000000000LL);
    // Nested objects inside an app section are real and common — the reason
    // SteamLauncher's regex rewrite of localconfig.vdf is fragile.
    QVERIFY(app.child("InstalledDepots").child("1245621").isObject());
    QCOMPARE(app.child("InstalledDepots").child("1245621").getString("manifest"), QString("123"));
}

void TstVdfParser::nestedObjects()
{
    VDFParser parser;
    QVERIFY(parser.parse(R"("a" { "b" { "c" { "d" "deep" } } })"));
    QCOMPARE(parser.root().child("a").child("b").child("c").getString("d"), QString("deep"));
}

void TstVdfParser::skipsComments()
{
    const QString content = R"(// leading comment
"root"
{
	// a comment between entries
	"key"		"value"	// trailing comment
}
)";
    VDFParser parser;
    QVERIFY2(parser.parse(content), qPrintable(parser.errorString()));
    QCOMPARE(parser.root().child("root").getString("key"), QString("value"));
}

void TstVdfParser::escapes_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");

    QTest::newRow("newline")   << R"("k" "a\nb")"  << QString("a\nb");
    QTest::newRow("tab")       << R"("k" "a\tb")"  << QString("a\tb");
    QTest::newRow("backslash") << R"("k" "a\\b")"  << QString("a\\b");
    QTest::newRow("quote")     << R"("k" "a\"b")"  << QString("a\"b");
    // An unrecognised escape drops the backslash and keeps the character.
    // Steam writes Windows paths with doubled backslashes, so real files come
    // out right — but a hand-edited single backslash silently loses a
    // directory separator, which is worth knowing about.
    QTest::newRow("unknown escape loses the backslash")
        << R"("k" "C:\Program Files")" << QString("C:Program Files");
    QTest::newRow("doubled backslashes are what Steam writes")
        << R"("k" "C:\\Program Files\\Game")" << QString("C:\\Program Files\\Game");
}

void TstVdfParser::escapes()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);

    VDFParser parser;
    QVERIFY2(parser.parse(raw), qPrintable(parser.errorString()));
    QCOMPARE(parser.root().getString("k"), expected);
}

void TstVdfParser::unquotedKeys()
{
    // toolmanifest.vdf in the wild uses unquoted keys.
    VDFParser parser;
    QVERIFY2(parser.parse("manifest\n{\n\tversion\t\"2\"\n\trequire_tool_appid\t\"1628350\"\n}\n"),
             qPrintable(parser.errorString()));
    const VDFNode manifest = parser.root().child("manifest");
    QCOMPARE(manifest.getString("version"), QString("2"));
    QCOMPARE(manifest.getString("require_tool_appid"), QString("1628350"));
}

void TstVdfParser::emptyValueIsNeitherValueNorObject()
{
    // Known quirk, in two shapes. isValue() tests the value for null and an
    // empty token never gets assigned; isObject() additionally requires at
    // least one child. So both `"label" ""` and an empty `{}` answer no to
    // both questions. No caller depends on the distinction today — they all go
    // through getString()/getInt(), which behave sensibly — but a parser
    // rewrite that "fixed" this could change what those return, so it is
    // pinned rather than left to chance.
    VDFParser parser;
    QVERIFY(parser.parse(R"("root" { "label" "" "empty" { } "real" "x" })"));

    const VDFNode root = parser.root().child("root");

    const VDFNode label = root.child("label");
    QVERIFY(!label.isObject());
    QVERIFY(!label.isValue());
    QCOMPARE(root.getString("label", "fallback"), QString("fallback"));

    const VDFNode empty = root.child("empty");
    QVERIFY(!empty.isObject());
    QVERIFY(!empty.isValue());

    // A populated value is unambiguous, which is the case that matters.
    QVERIFY(root.child("real").isValue());
    QVERIFY(!root.child("real").isObject());
    QCOMPARE(root.getString("real"), QString("x"));
}

void TstVdfParser::rejects_data()
{
    QTest::addColumn<QString>("raw");

    // The tokenizer sets a precise message ("Unterminated string") which the
    // parser then overwrites with its own, less specific one, so only the
    // failure itself is asserted here rather than the wording.
    QTest::newRow("unterminated string")  << R"("key" "value)";
    QTest::newRow("unclosed object")      << R"("a" { "b" "c" )";
    QTest::newRow("stray closing brace")  << R"(})";
    QTest::newRow("key without a value")  << R"("a" { "b" )";
}

void TstVdfParser::rejects()
{
    QFETCH(QString, raw);

    VDFParser parser;
    QVERIFY2(!parser.parse(raw), "expected a parse failure");
    QVERIFY2(!parser.errorString().isEmpty(), "a failure with no message");
}

void TstVdfParser::getIntFallsBackOnGarbage()
{
    VDFParser parser;
    QVERIFY(parser.parse(R"("root" { "n" "not-a-number" })"));
    QCOMPARE(parser.root().child("root").getInt("n", -1), -1LL);
    QCOMPARE(parser.root().child("root").getInt("missing", 42), 42LL);
    QCOMPARE(parser.root().child("root").getString("missing"), QString());
}

void TstVdfParser::parseFileReadsFromDisk()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath("libraryfolders.vdf");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"("libraryfolders" { "0" { "path" "/tmp/lib" } })");
    file.close();

    VDFParser parser;
    QVERIFY2(parser.parseFile(path), qPrintable(parser.errorString()));
    QCOMPARE(parser.root().child("libraryfolders").child("0").getString("path"),
             QString("/tmp/lib"));
}

void TstVdfParser::missingFileFails()
{
    VDFParser parser;
    QVERIFY(!parser.parseFile("/nonexistent/definitely/not/here.vdf"));
    QVERIFY(!parser.errorString().isEmpty());
}

QTEST_MAIN(TstVdfParser)
#include "tst_vdfparser.moc"
