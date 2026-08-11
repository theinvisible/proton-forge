// Turning a build into a list of files to write.
//
// Every rule here is one that fails quietly when it is wrong:
//
//   Language selection: too few depots and the game has no audio; too many and
//     the download is twice the size it needed to be.
//   "*" is shared content — the executable and the assets — and is needed
//     whatever language was picked. Treating it as just another language tag
//     gives you a language pack with no game under it.
//   DLC depots are listed inside the *base game's* build, so ownership has to be
//     filtered on rather than trusted.
//   Depot overlay order decides which file wins when two provide the same path.
//   Case collisions cannot both exist on the NTFS drive a second game library
//     usually is.

#include <QTest>
#include <QFile>

#include "gog/GogContentClient.h"
#include "gog/GogInstallPlan.h"

using BuildMeta = GogContentClient::BuildMeta;
using DepotRef = GogContentClient::DepotRef;
using DepotManifest = GogContentClient::DepotManifest;
using DepotItem = GogContentClient::DepotItem;
using Chunk = GogContentClient::Chunk;

class TstGogPlan : public QObject
{
    Q_OBJECT

private slots:
    void listsTheLanguagesOnOffer();

    void alwaysTakesTheSharedDepot();
    void takesOnlyTheWantedLanguage();
    void skipsDlcTheUserDoesNotOwn();
    void takesDlcTheUserOwns();
    void respectsBitness();

    void buildsAPlanFromAManifest();
    void refusesItemsThatEscapeTheInstallDirectory();
    void laterDepotsWinOnTheSamePath();
    void putsDepotsThatDisagreeOnCaseInOneDirectory();
    void keepsTheFirstSpellingEvenDeepInTheTree();
    void doesNotMergeFilesThatDifferOnlyInCase();
    void pointsLinksAtTheSpellingItKept();
    void warnsAboutRedistributablesItWillNotRun();

    void diffOnlyReturnsWhatChanged();
    void fingerprintsSurviveARoundTrip();
    void diffsAgainstAStoredManifest();
    void namesFilesTheNewVersionDropped();
    void treatsAMissingManifestAsAFullInstall();
    void detectsCaseCollisions();

private:
    static QByteArray fixture(const QString& name)
    {
        QFile file(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/gog/" + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static BuildMeta realMeta()
    {
        return GogContentClient::parseBuildMeta(fixture("build-meta.json"));
    }

    static Chunk chunk(const QString& compressedMd5, qint64 size)
    {
        Chunk c;
        c.compressedMd5 = compressedMd5;
        c.md5 = compressedMd5;   // irrelevant here, only identity matters
        c.compressedSize = size / 2;
        c.size = size;
        return c;
    }

    static DepotManifest manifestWith(const QList<DepotItem>& items,
                                      const QString& productId = QStringLiteral("1207664663"))
    {
        DepotManifest manifest;
        manifest.productId = productId;
        manifest.items = items;
        manifest.valid = true;
        return manifest;
    }

    static DepotItem file(const QString& path, const QString& chunkId, qint64 size)
    {
        DepotItem item;
        item.path = path;
        item.type = QStringLiteral("DepotFile");
        item.chunks = {chunk(chunkId, size)};
        return item;
    }

    static DepotItem directory(const QString& path)
    {
        DepotItem item;
        item.path = path;
        item.type = QStringLiteral("DepotDirectory");
        return item;
    }
};

void TstGogPlan::listsTheLanguagesOnOffer()
{
    // "*" is not a language the user can pick, so it must not appear in the
    // picker — it always comes along.
    QCOMPARE(GogInstallPlan::availableLanguages(realMeta()),
             QStringList({"de-DE", "en-US"}));
}

void TstGogPlan::alwaysTakesTheSharedDepot()
{
    // Asking for a language nobody publishes must still get the game itself.
    const QList<DepotRef> selected =
        GogInstallPlan::selectDepots(realMeta(), {"fr-FR"}, {}, 64);

    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().languages, QStringList({"*"}));
}

void TstGogPlan::takesOnlyTheWantedLanguage()
{
    const QList<DepotRef> selected =
        GogInstallPlan::selectDepots(realMeta(), {"en-US"}, {}, 64);

    // The shared depot plus en-US, and specifically not de-DE.
    QCOMPARE(selected.size(), 2);
    QCOMPARE(selected.at(1).languages, QStringList({"en-US"}));
}

void TstGogPlan::skipsDlcTheUserDoesNotOwn()
{
    const QList<DepotRef> selected =
        GogInstallPlan::selectDepots(realMeta(), {"en-US"}, {}, 64);

    for (const DepotRef& depot : selected) {
        QVERIFY2(depot.productId != QLatin1String("1207664703"),
                 "a DLC depot was taken without the DLC being owned");
    }
}

void TstGogPlan::takesDlcTheUserOwns()
{
    const QList<DepotRef> selected =
        GogInstallPlan::selectDepots(realMeta(), {"en-US"}, {"1207664703"}, 64);

    bool sawDlc = false;
    for (const DepotRef& depot : selected) {
        sawDlc = sawDlc || depot.productId == QLatin1String("1207664703");
    }
    QVERIFY(sawDlc);
    QCOMPARE(selected.size(), 3);
}

void TstGogPlan::respectsBitness()
{
    // The fixture has a 32-bit-only depot. A 64-bit install must not take it,
    // and a 32-bit one must not take the 64-bit shared depot.
    const QList<DepotRef> sixtyFour =
        GogInstallPlan::selectDepots(realMeta(), {"en-US"}, {}, 64);
    for (const DepotRef& depot : sixtyFour) {
        QVERIFY(!depot.osBitness.contains("32") || depot.osBitness.contains("64"));
    }

    const QList<DepotRef> thirtyTwo =
        GogInstallPlan::selectDepots(realMeta(), {"en-US"}, {}, 32);
    QCOMPARE(thirtyTwo.size(), 1);
    QCOMPARE(thirtyTwo.first().osBitness, QStringList({"32"}));
}

void TstGogPlan::buildsAPlanFromAManifest()
{
    const BuildMeta meta = realMeta();
    const DepotManifest manifest =
        GogContentClient::parseDepotManifest(fixture("depot-manifest.json"));

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {manifest});

    QVERIFY(plan.valid);
    QCOMPARE(plan.installDirectory, QStringLiteral("The Witcher 3 Wild Hunt"));

    // Seven items in the manifest: one directory, one escaping path refused,
    // five files (one of them a symlink).
    QCOMPARE(plan.files.size(), 5);
    QCOMPARE(plan.directories, QStringList({"bin/x64"}));

    // Sorted, so two runs produce the same plan.
    QStringList paths;
    for (const GogInstallPlan::FileTask& task : plan.files) {
        paths << task.relPath;
    }
    QCOMPARE(paths, QStringList({"bin/x64/witcher3.exe",
                                 "content/patch0.bundle",
                                 "docs/readme-link",
                                 "empty.txt",
                                 "support/redist/vcredist.exe"}));

    // Backslashes normalised on the way in.
    QVERIFY(paths.contains("content/patch0.bundle"));

    const GogInstallPlan::FileTask& exe = plan.files.first();
    QVERIFY(exe.executable);
    QCOMPARE(exe.chunks.size(), 2);
    QCOMPARE(exe.size, 1900LL);           // 1000 + 900
    QCOMPARE(exe.sourceProductId, QStringLiteral("1207664663"));

    // A symlink carries a target and no chunks.
    for (const GogInstallPlan::FileTask& task : plan.files) {
        if (task.relPath == QLatin1String("docs/readme-link")) {
            QCOMPARE(task.linkTarget, QStringLiteral("docs/readme.txt"));
            QVERIFY(task.chunks.isEmpty());
        }
    }

    // support-flagged files are installed like any other. Galaxy would run them;
    // ProtonForge does not — but skipping the files themselves would be a way to
    // break a game for a disk saving nobody asked for.
    QVERIFY(paths.contains("support/redist/vcredist.exe"));

    QCOMPARE(plan.totalSize, 1900LL + 700 + 200);   // exe + bundle + vcredist
}

void TstGogPlan::refusesItemsThatEscapeTheInstallDirectory()
{
    const DepotManifest manifest =
        GogContentClient::parseDepotManifest(fixture("depot-manifest.json"));
    const GogInstallPlan::Plan plan = GogInstallPlan::build(realMeta(), {manifest});

    for (const GogInstallPlan::FileTask& task : plan.files) {
        QVERIFY2(!task.relPath.contains(QLatin1String("..")),
                 qPrintable("a traversal survived: " + task.relPath));
        QVERIFY(!task.relPath.startsWith('/'));
    }

    // And it says so rather than dropping it silently — a build that names a
    // path outside its own directory is worth knowing about.
    bool warned = false;
    for (const QString& warning : plan.warnings) {
        warned = warned || warning.contains("outside the install directory");
    }
    QVERIFY(warned);
}

void TstGogPlan::laterDepotsWinOnTheSamePath()
{
    // How GOG layers a language pack over the shared content: both depots
    // provide the same file and the later one is the one that counts. Getting
    // this backwards installs the wrong language's asset with no error anywhere.
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";
    meta.installDirectory = "Game";

    const DepotManifest shared =
        manifestWith({file("data/voice.pak", "aaaa1111", 100)});
    const DepotManifest german =
        manifestWith({file("data/voice.pak", "bbbb2222", 200)});

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {shared, german});

    QCOMPARE(plan.files.size(), 1);
    QCOMPARE(plan.files.first().chunks.first().compressedMd5, QStringLiteral("bbbb2222"));
    QCOMPARE(plan.totalSize, 200LL);
}

void TstGogPlan::putsDepotsThatDisagreeOnCaseInOneDirectory()
{
    // Anno 1602, exactly: the shared depot puts three .GAD files in GADDATA and
    // the German depot puts its menu definitions in Gaddata. On the filesystem
    // GOG builds for that is one folder. Left as two, the game starts and then
    // draws a black screen where its main menu should be, because whichever
    // spelling it opens holds only part of its data.
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";
    meta.installDirectory = "Anno 1602";

    const DepotManifest shared = manifestWith({
        directory("GADDATA"),
        file("GADDATA/BASE.GAD", "aaaa1111", 100),
    });
    const DepotManifest german = manifestWith({
        directory("Gaddata"),
        file("Gaddata/ANNO.GAD", "bbbb2222", 200),
    });

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {shared, german});

    QStringList paths;
    for (const GogInstallPlan::FileTask& file : plan.files) {
        paths << file.relPath;
    }
    paths.sort();

    // Both files, one directory — and the spelling is the one that was created
    // first, which is the one the original install would have ended up with.
    QCOMPARE(paths, QStringList({"GADDATA/ANNO.GAD", "GADDATA/BASE.GAD"}));
    QCOMPARE(plan.directories, QStringList({"GADDATA"}));
}

void TstGogPlan::keepsTheFirstSpellingEvenDeepInTheTree()
{
    // Component by component, not a prefix match: only the segment that was
    // seen before is replaced, and a directory named the same as one further up
    // does not borrow its spelling.
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {
        manifestWith({file("Data/Sound/Speech/en.pak", "aaaa1111", 10)}),
        manifestWith({file("DATA/SOUND/speech/de.pak", "bbbb2222", 10)}),
        manifestWith({file("data/other/DATA/x.pak", "cccc3333", 10)}),
    });

    QStringList paths;
    for (const GogInstallPlan::FileTask& file : plan.files) {
        paths << file.relPath;
    }
    paths.sort();

    QCOMPARE(paths, QStringList({"Data/Sound/Speech/de.pak",
                                 "Data/Sound/Speech/en.pak",
                                 "Data/other/DATA/x.pak"}));
}

void TstGogPlan::doesNotMergeFilesThatDifferOnlyInCase()
{
    // The directory is unified; the file name is not. Two files are two files —
    // quietly collapsing them would drop one, and whether they can coexist is a
    // question about the drive, which wouldCollideCaseInsensitively answers.
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {
        manifestWith({file("GFX/menu.bsh", "aaaa1111", 10)}),
        manifestWith({file("Gfx/MENU.BSH", "bbbb2222", 10)}),
    });

    QCOMPARE(plan.files.size(), 2);
    QString which;
    QVERIFY(GogInstallPlan::wouldCollideCaseInsensitively(plan, &which));
}

void TstGogPlan::pointsLinksAtTheSpellingItKept()
{
    // A link written against the other spelling would dangle: the directory it
    // names is not the one that exists.
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    DepotItem link;
    link.path = QStringLiteral("bin/run.sh");
    link.type = QStringLiteral("DepotLink");
    link.linkTarget = QStringLiteral("Support/start.sh");

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {
        manifestWith({file("SUPPORT/start.sh", "aaaa1111", 10)}),
        manifestWith({link}),
    });

    for (const GogInstallPlan::FileTask& file : plan.files) {
        if (!file.linkTarget.isEmpty()) {
            QCOMPARE(file.linkTarget, QStringLiteral("SUPPORT/start.sh"));
            return;
        }
    }
    QFAIL("the link never made it into the plan");
}

void TstGogPlan::warnsAboutRedistributablesItWillNotRun()
{
    // "Proton provides its own" is true for nearly all of them. Nearly is not
    // all, so the one case where a game will not start has to be discoverable.
    const GogInstallPlan::Plan plan = GogInstallPlan::build(realMeta(), {});

    bool warned = false;
    for (const QString& warning : plan.warnings) {
        warned = warned || (warning.contains("MSVC2019") && warning.contains("does not run"));
    }
    QVERIFY(warned);
}

void TstGogPlan::diffOnlyReturnsWhatChanged()
{
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan installed = GogInstallPlan::build(meta, {manifestWith({
        file("unchanged.dat", "aaaa1111", 100),
        file("changed.dat", "bbbb2222", 100),
    })});

    const GogInstallPlan::Plan target = GogInstallPlan::build(meta, {manifestWith({
        file("unchanged.dat", "aaaa1111", 100),
        file("changed.dat", "cccc3333", 150),     // different content
        file("brand-new.dat", "dddd4444", 50),
    })});

    const QList<GogInstallPlan::FileTask> changed = GogInstallPlan::diff(target, installed);

    QStringList paths;
    for (const GogInstallPlan::FileTask& task : changed) {
        paths << task.relPath;
    }
    paths.sort();

    // This is what makes an update a delta rather than a fresh download.
    QCOMPARE(paths, QStringList({"brand-new.dat", "changed.dat"}));
}

void TstGogPlan::fingerprintsSurviveARoundTrip()
{
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, {manifestWith({
        file("bin/game.exe", "aaaa1111", 100),
        file("data/big.pak", "bbbb2222", 200),
    })});

    const QHash<QString, QString> parsed =
        GogInstallPlan::parseFingerprints(GogInstallPlan::serializeFingerprints(plan));

    QCOMPARE(parsed.size(), 2);
    // Diffing the plan against its own stored fingerprints must find nothing —
    // otherwise every update would re-download everything.
    QVERIFY(GogInstallPlan::diffAgainstFingerprints(plan, parsed).isEmpty());
}

void TstGogPlan::diffsAgainstAStoredManifest()
{
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan installed = GogInstallPlan::build(meta, {manifestWith({
        file("unchanged.dat", "aaaa1111", 100),
        file("changed.dat", "bbbb2222", 100),
    })});
    const GogInstallPlan::Plan target = GogInstallPlan::build(meta, {manifestWith({
        file("unchanged.dat", "aaaa1111", 100),
        file("changed.dat", "cccc3333", 150),
        file("brand-new.dat", "dddd4444", 50),
    })});

    // Through the serialised form, which is what a real update reads.
    const QHash<QString, QString> stored =
        GogInstallPlan::parseFingerprints(GogInstallPlan::serializeFingerprints(installed));

    QStringList paths;
    for (const GogInstallPlan::FileTask& task :
         GogInstallPlan::diffAgainstFingerprints(target, stored)) {
        paths << task.relPath;
    }
    paths.sort();

    QCOMPARE(paths, QStringList({"brand-new.dat", "changed.dat"}));
    // The two routes must agree, or a delta across restarts would differ from
    // one within a single run.
    QCOMPARE(paths.size(), GogInstallPlan::diff(target, installed).size());
}

void TstGogPlan::namesFilesTheNewVersionDropped()
{
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan installed = GogInstallPlan::build(meta, {manifestWith({
        file("keep.dat", "aaaa1111", 100),
        file("bin/old.dll", "bbbb2222", 100),
    })});
    const GogInstallPlan::Plan target = GogInstallPlan::build(meta, {manifestWith({
        file("keep.dat", "aaaa1111", 100),
        file("bin/new.dll", "cccc3333", 100),
    })});

    const QHash<QString, QString> stored =
        GogInstallPlan::parseFingerprints(GogInstallPlan::serializeFingerprints(installed));

    // Left in place, an orphaned DLL can be loaded in preference to the one the
    // new version shipped — a failure that looks nothing like a bad update.
    QCOMPARE(GogInstallPlan::removedPaths(target, stored), QStringList({"bin/old.dll"}));
}

void TstGogPlan::treatsAMissingManifestAsAFullInstall()
{
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan target = GogInstallPlan::build(meta, {manifestWith({
        file("a.dat", "aaaa1111", 100),
        file("b.dat", "bbbb2222", 100),
    })});

    // No manifest — an install from before this existed, or a hand-deleted one.
    // Everything is fetched, which is correct; nothing is deleted, which
    // matters more, since an empty map must not read as "the new version has
    // dropped every file".
    QCOMPARE(GogInstallPlan::diffAgainstFingerprints(target, {}).size(), 2);
    QVERIFY(GogInstallPlan::removedPaths(target, {}).isEmpty());

    for (const QByteArray& garbage : {QByteArray(), QByteArray("not json"),
                                      QByteArray("{}"), QByteArray("[]")}) {
        QVERIFY(GogInstallPlan::parseFingerprints(garbage).isEmpty());
    }
}

void TstGogPlan::detectsCaseCollisions()
{
    BuildMeta meta;
    meta.valid = true;
    meta.baseProductId = "1";

    const GogInstallPlan::Plan fine = GogInstallPlan::build(meta, {manifestWith({
        file("Data/a.pak", "aaaa1111", 10),
        file("data2/b.pak", "bbbb2222", 10),
    })});
    QString which;
    QVERIFY(!GogInstallPlan::wouldCollideCaseInsensitively(fine, &which));

    const GogInstallPlan::Plan colliding = GogInstallPlan::build(meta, {manifestWith({
        file("Data/a.pak", "aaaa1111", 10),
        file("data/A.pak", "bbbb2222", 10),
    })});
    QVERIFY2(GogInstallPlan::wouldCollideCaseInsensitively(colliding, &which),
             "these cannot both exist on the NTFS drive a second game library usually is");
    QVERIFY(!which.isEmpty());
}

QTEST_MAIN(TstGogPlan)
#include "tst_gogplan.moc"
