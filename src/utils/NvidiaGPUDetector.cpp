#include "NvidiaGPUDetector.h"
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>

QList<GPUInfo> NvidiaGPUDetector::detect()
{
    QList<GPUInfo> gpus;

    QProcess process;
    process.start("nvidia-smi", QStringList() << "-q");

    if (!process.waitForStarted(1000)) {
        qWarning() << "Failed to start nvidia-smi";
        return gpus;
    }

    if (!process.waitForFinished(5000)) {
        qWarning() << "nvidia-smi timed out";
        return gpus;
    }

    QString output = process.readAllStandardOutput();

    if (output.isEmpty()) {
        qWarning() << "nvidia-smi returned empty output";
        return gpus;
    }

    // Split output by GPU sections
    QStringList sections = output.split(QRegularExpression("GPU \\d+:"), Qt::SkipEmptyParts);

    for (int i = 0; i < sections.size(); ++i) {
        GPUInfo info = parseNvidiaSmiOutput(sections[i], i);
        if (!info.name.isEmpty()) {
            gpus.append(info);
        }
    }

    return gpus;
}

GPUInfo NvidiaGPUDetector::parseNvidiaSmiOutput(const QString& output, int index)
{
    GPUInfo info;
    info.vendor = GPUInfo::NVIDIA;
    info.index = index;

    // Product Name
    info.name = extractValue(output, "Product Name");

    // Architecture (not always available in nvidia-smi, try to extract from Product Name)
    info.architecture = extractValue(output, "Product Architecture");
    if (info.architecture.isEmpty()) {
        // Try to infer from name (e.g., "RTX 4090" -> "Ada Lovelace")
        if (info.name.contains("RTX 40")) info.architecture = "Ada Lovelace";
        else if (info.name.contains("RTX 30")) info.architecture = "Ampere";
        else if (info.name.contains("RTX 20")) info.architecture = "Turing";
        else if (info.name.contains("GTX 16")) info.architecture = "Turing";
        else if (info.name.contains("GTX 10")) info.architecture = "Pascal";
    }

    // Driver and Versions
    info.driverVersion = extractValue(output, "Driver Version");
    info.cudaVersion = extractValue(output, "CUDA Version");
    info.vbiosVersion = extractValue(output, "VBIOS Version");

    // GPU Part Number
    info.gpuPartNumber = extractValue(output, "Product Brand");
    if (info.gpuPartNumber.isEmpty()) {
        info.gpuPartNumber = extractValue(output, "Board ID");
    }

    // Compute Capability
    QString cudaMajor = extractValue(output, "CUDA Capability Major/Minor Version");
    if (!cudaMajor.isEmpty()) {
        info.computeCapability = cudaMajor;
    }

    // Memory
    QString memStr = extractValue(output, "Total");
    if (!memStr.isEmpty()) {
        QRegularExpression memRegex("(\\d+)\\s*MiB");
        QRegularExpressionMatch match = memRegex.match(memStr);
        if (match.hasMatch()) {
            info.memoryTotalMB = match.captured(1).toInt();
        }
    }

    // PCI Info
    info.pciId = extractValue(output, "Bus Id");

    QString currentLinkGen = extractValue(output, "Current");
    if (currentLinkGen.contains("Gen")) {
        info.pcieCurrentGen = currentLinkGen;
    }

    QString maxLinkGen = extractValue(output, "Max");
    if (maxLinkGen.contains("Gen")) {
        info.pcieMaxGen = maxLinkGen;
    }

    QString linkWidth = extractValue(output, "Link Width");
    info.pcieLinkWidth = linkWidth;

    // Clocks - need to parse from specific sections
    // Find "Clocks" section (not "Max Clocks" or other sections)
    QRegularExpression clocksRegex("Clocks\\s*\\n\\s*Graphics\\s*:\\s*(\\d+)\\s*MHz\\s*\\n\\s*SM\\s*:\\s*\\d+\\s*MHz\\s*\\n\\s*Memory\\s*:\\s*(\\d+)\\s*MHz");
    QRegularExpressionMatch clocksMatch = clocksRegex.match(output);
    if (clocksMatch.hasMatch()) {
        info.currentGraphicsClock = clocksMatch.captured(1).toInt();
        info.currentMemoryClock = clocksMatch.captured(2).toInt();
    }

    // Find "Max Clocks" section
    QRegularExpression maxClocksRegex("Max Clocks\\s*\\n\\s*Graphics\\s*:\\s*(\\d+)\\s*MHz\\s*\\n\\s*SM\\s*:\\s*\\d+\\s*MHz\\s*\\n\\s*Memory\\s*:\\s*(\\d+)\\s*MHz");
    QRegularExpressionMatch maxClocksMatch = maxClocksRegex.match(output);
    if (maxClocksMatch.hasMatch()) {
        info.maxGraphicsClock = maxClocksMatch.captured(1).toInt();
        info.maxMemoryClock = maxClocksMatch.captured(2).toInt();
    }

    // Power
    QString powerDrawStr = extractValue(output, "Power Draw");
    if (!powerDrawStr.isEmpty()) {
        QRegularExpression powerRegex("([\\d.]+)\\s*W");
        QRegularExpressionMatch match = powerRegex.match(powerDrawStr);
        if (match.hasMatch()) {
            info.currentPowerDraw = match.captured(1).toDouble();
        }
    }

    QString powerLimitStr = extractValue(output, "Power Limit");
    if (!powerLimitStr.isEmpty()) {
        QRegularExpression powerRegex("([\\d.]+)\\s*W");
        QRegularExpressionMatch match = powerRegex.match(powerLimitStr);
        if (match.hasMatch()) {
            info.powerLimit = match.captured(1).toDouble();
        }
    }

    // Temperature
    QString tempStr = extractValue(output, "GPU Current Temp");
    if (!tempStr.isEmpty()) {
        QRegularExpression tempRegex("(\\d+)\\s*C");
        QRegularExpressionMatch match = tempRegex.match(tempStr);
        if (match.hasMatch()) {
            info.temperature = match.captured(1).toInt();
        }
    }

    // Fan Speed
    QString fanStr = extractValue(output, "Fan Speed");
    if (!fanStr.isEmpty()) {
        QRegularExpression fanRegex("(\\d+)\\s*%");
        QRegularExpressionMatch match = fanRegex.match(fanStr);
        if (match.hasMatch()) {
            info.fanSpeed = match.captured(1).toInt();
        }
    }

    // Performance State
    info.performanceState = extractValue(output, "Performance State");

    // UUID
    info.uuid = extractValue(output, "UUID");

    // Display Connected
    QString displayActive = extractValue(output, "Display Active");
    info.displayConnected = (displayActive.toLower() == "enabled" ||
                             displayActive.toLower() == "yes");

    return info;
}

QString NvidiaGPUDetector::extractValue(const QString& output, const QString& key)
{
    // Try different patterns to extract values
    QRegularExpression regex(key + "\\s*:\\s*([^\\n]+)");
    QRegularExpressionMatch match = regex.match(output);

    if (match.hasMatch()) {
        QString value = match.captured(1).trimmed();
        // Remove "N/A" values
        if (value == "N/A" || value.isEmpty()) {
            return QString();
        }
        return value;
    }

    return QString();
}

int NvidiaGPUDetector::extractIntValue(const QString& output, const QString& key)
{
    QString value = extractValue(output, key);
    if (value.isEmpty()) {
        return 0;
    }

    QRegularExpression intRegex("(\\d+)");
    QRegularExpressionMatch match = intRegex.match(value);

    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }

    return 0;
}
