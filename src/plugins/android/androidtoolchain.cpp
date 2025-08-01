// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidtoolchain.h"
#include "androidtr.h"

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/toolchainmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/toolchainconfigwidget.h>

#include <utils/environment.h>

#include <QLoggingCategory>

using namespace ProjectExplorer;
using namespace Utils;

namespace Android::Internal {

static Q_LOGGING_CATEGORY(androidTCLog, "qtc.android.toolchainmanagement", QtWarningMsg);

using ClangTargetsType = QHash<QString, Abi>;

const ClangTargetsType &clangTargets()
{
    static const ClangTargetsType targets {
        {"arm-linux-androideabi",
         Abi(Abi::ArmArchitecture, Abi::LinuxOS, Abi::AndroidLinuxFlavor, Abi::ElfFormat, 32)},
        {"i686-linux-android",
         Abi(Abi::X86Architecture, Abi::LinuxOS, Abi::AndroidLinuxFlavor, Abi::ElfFormat, 32)},
        {"x86_64-linux-android",
         Abi(Abi::X86Architecture, Abi::LinuxOS, Abi::AndroidLinuxFlavor, Abi::ElfFormat, 64)},
        {"aarch64-linux-android",
         Abi(Abi::ArmArchitecture, Abi::LinuxOS, Abi::AndroidLinuxFlavor, Abi::ElfFormat, 64)}
    };
    return targets;
}

static Toolchain *findToolchain(FilePath &compilerPath, Id lang, const QString &target,
                                const ToolchainList &alreadyKnown)
{
    Toolchain *tc = Utils::findOrDefault(alreadyKnown, [target, compilerPath, lang](Toolchain *tc) {
        return tc->typeId() == Constants::ANDROID_TOOLCHAIN_TYPEID
                && tc->language() == lang
                && tc->targetAbi() == clangTargets().value(target)
                && tc->compilerCommand() == compilerPath;
    });
    return tc;
}

AndroidToolchain::AndroidToolchain()
    : GccToolchain(Constants::ANDROID_TOOLCHAIN_TYPEID, Clang)
{
    setTypeDisplayName(Tr::tr("Android Clang"));
}

FilePath AndroidToolchain::ndkLocation() const
{
    return m_ndkLocation;
}

void AndroidToolchain::setNdkLocation(const FilePath &ndkLocation)
{
    m_ndkLocation = ndkLocation;
}

AndroidToolchain::~AndroidToolchain() = default;

bool AndroidToolchain::isValid() const
{
    if (m_ndkLocation.isEmpty()) {
        QStringList ndkParts(compilerCommand().toFSPathString().split("toolchains/llvm/prebuilt/"));
        if (ndkParts.size() > 1) {
            QString ndkLocation(ndkParts.first());
            if (ndkLocation.endsWith('/'))
                ndkLocation.chop(1);
            m_ndkLocation = FilePath::fromString(ndkLocation);
        }
    }

    const bool isChildofNdk = compilerCommand().isChildOf(m_ndkLocation);
    const bool isChildofSdk = compilerCommand().isChildOf(AndroidConfig::sdkLocation());

    return GccToolchain::isValid() && typeId() == Constants::ANDROID_TOOLCHAIN_TYPEID
           && targetAbi().isValid() && (isChildofNdk || isChildofSdk)
           && !originalTargetTriple().isEmpty();
}

void AndroidToolchain::addToEnvironment(Environment &env) const
{
    env.set(QLatin1String("ANDROID_NDK_HOST"), AndroidConfig::toolchainHostFromNdk(m_ndkLocation));
    const FilePath javaHome = AndroidConfig::openJDKLocation();
    if (javaHome.exists()) {
        env.set(Constants::JAVA_HOME_ENV_VAR, javaHome.toUserOutput());
        const FilePath javaBin = javaHome.pathAppended("bin");
        const FilePath currentJavaFilePath
            = env.searchInPath("java", {}, {}, FilePath::WithExeSuffix);
        if (!currentJavaFilePath.isChildOf(javaBin))
            env.prependOrSetPath(javaBin);
    }
    env.set(QLatin1String("ANDROID_HOME"), AndroidConfig::sdkLocation().toUserOutput());
    env.set(QLatin1String("ANDROID_SDK_ROOT"), AndroidConfig::sdkLocation().toUserOutput());
}

void AndroidToolchain::fromMap(const Store &data)
{
    GccToolchain::fromMap(data);
    if (hasError())
        return;
    if (!isValid())
        reportError();
}

QStringList AndroidToolchain::suggestedMkspecList() const
{
    return {"android-g++", "android-clang"};
}

FilePath AndroidToolchain::makeCommand(const Environment &env) const
{
    Q_UNUSED(env)
    FilePath makePath = AndroidConfig::makePathFromNdk(m_ndkLocation);
    return makePath.exists() ? makePath : FilePath("make");
}

GccToolchain::DetectedAbisResult AndroidToolchain::detectSupportedAbis() const
{
    for (auto itr = clangTargets().constBegin(); itr != clangTargets().constEnd(); ++itr) {
        if (itr.value() == targetAbi())
            return GccToolchain::DetectedAbisResult({targetAbi()}, itr.key());
    }
    return GccToolchain::DetectedAbisResult({targetAbi()}, "");
}

// AndroidToolchainFactory

static FilePath clangPlusPlusPath(const FilePath &clangPath)
{
    return clangPath.parentDir().pathAppended(clangPath.baseName() + "++").withExecutableSuffix();
}

static FilePaths uniqueNdksForCurrentQtVersions()
{
    const QtSupport::QtVersions androidQtVersions = QtSupport::QtVersionManager::versions(
        &QtSupport::QtVersion::isAndroidQtVersion);

    FilePaths uniqueNdks;
    for (const QtSupport::QtVersion *version : androidQtVersions) {
        FilePath ndk = AndroidConfig::ndkLocation(version);
        if (!uniqueNdks.contains(ndk))
            uniqueNdks.append(ndk);
    }

    return uniqueNdks;
}

ToolchainList autodetectToolchainsFromNdks(
    const ToolchainList &alreadyKnown,
    const QList<FilePath> &ndkLocations,
    const bool isCustom)
{
    QList<Toolchain *> newToolchains;

    const Id LanguageIds[] {
        ProjectExplorer::Constants::CXX_LANGUAGE_ID,
        ProjectExplorer::Constants::C_LANGUAGE_ID
    };

    for (const FilePath &ndkLocation : ndkLocations) {
        const FilePath clangPath = AndroidConfig::clangPathFromNdk(ndkLocation);
        if (!clangPath.exists()) {
            qCDebug(androidTCLog) << "Clang toolchains detection fails. Can not find Clang"
                                  << clangPath;
            continue;
        }

        for (const Id &lang : LanguageIds) {
            FilePath compilerCommand = clangPath;
            if (lang == ProjectExplorer::Constants::CXX_LANGUAGE_ID)
                compilerCommand = clangPlusPlusPath(clangPath);

            if (!compilerCommand.exists()) {
                qCDebug(androidTCLog)
                    << "Skipping Clang toolchain. Can not find compiler" << compilerCommand;
                continue;
            }

            auto targetItr = clangTargets().constBegin();
            while (targetItr != clangTargets().constEnd()) {
                const Abi &abi = targetItr.value();
                const QString target = targetItr.key();
                Toolchain *tc = findToolchain(compilerCommand, lang, target, alreadyKnown);

                const QString customStr = isCustom ? "Custom " : QString();
                const QString displayName(
                    customStr
                    + QString("Android Clang (%1, NDK %2)")
                          .arg(
                              AndroidConfig::displayName(abi),
                              AndroidConfig::ndkVersion(ndkLocation).toString()));
                if (tc) {
                    // make sure to update the toolchain with current name format
                    if (tc->displayName() != displayName)
                        tc->setDisplayName(displayName);
                } else {
                    qCDebug(androidTCLog) << "New Clang toolchain found" << abi.toString() << lang
                                          << "for NDK" << ndkLocation;
                    auto atc = new AndroidToolchain();
                    atc->setNdkLocation(ndkLocation);
                    atc->setOriginalTargetTriple(target);
                    atc->setLanguage(lang);
                    atc->setTargetAbi(clangTargets().value(target));
                    atc->setPlatformCodeGenFlags({"-target", target});
                    atc->setPlatformLinkerFlags({"-target", target});
                    atc->setDisplayName(displayName);
                    tc = atc;

                    newToolchains << tc;
                }

                // Do not only reset newly created toolchains. This triggers call to
                // addToEnvironment, so that e.g. JAVA_HOME gets updated.
                if (auto gccTc = dynamic_cast<GccToolchain*>(tc))
                    gccTc->resetToolchain(compilerCommand);

                tc->setDetectionSource(DetectionSource::FromSystem);
                ++targetItr;
            }
        }
    }

    return newToolchains;
}

ToolchainList autodetectToolchains(const ToolchainList &alreadyKnown)
{
    const QList<FilePath> uniqueNdks = uniqueNdksForCurrentQtVersions();
    return autodetectToolchainsFromNdks(alreadyKnown, uniqueNdks);
}

class AndroidToolchainFactory final : public ToolchainFactory
{
public:
    AndroidToolchainFactory()
    {
        setDisplayName(Tr::tr("Android Clang"));
        setSupportedToolchainType(Constants::ANDROID_TOOLCHAIN_TYPEID);
        setSupportedLanguages(
            {ProjectExplorer::Constants::C_LANGUAGE_ID,
             ProjectExplorer::Constants::CXX_LANGUAGE_ID});
        setToolchainConstructor([] { return new AndroidToolchain; });
    }

private:
    std::unique_ptr<ToolchainConfigWidget> createConfigurationWidget(
        const ToolchainBundle &bundle) const final
    {
        return GccToolchain::createConfigurationWidget(bundle);
    }

    FilePath correspondingCompilerCommand(const FilePath &srcPath, Id targetLang) const override
    {
        return GccToolchain::correspondingCompilerCommand(srcPath, targetLang, "clang", "clang++");
    }
};

void setupAndroidToolchain()
{
    static AndroidToolchainFactory theAndroidToolchainFactory;
}

} // Android::Internal
