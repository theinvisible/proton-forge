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

    // Parse PCIe Generation and Link Width
    QRegularExpression pcieGenRegex("PCIe Generation\\s*\\n\\s*Max\\s*:\\s*(\\d+)\\s*\\n\\s*Current\\s*:\\s*(\\d+)");
    QRegularExpressionMatch pcieGenMatch = pcieGenRegex.match(output);
    if (pcieGenMatch.hasMatch()) {
        int maxGen = pcieGenMatch.captured(1).toInt();
        int currentGen = pcieGenMatch.captured(2).toInt();
        info.pcieMaxGen = QString("Gen %1").arg(maxGen);
        info.pcieCurrentGen = QString("Gen %1").arg(currentGen);
    }

    QRegularExpression linkWidthRegex("Link Width\\s*\\n\\s*Max\\s*:\\s*(\\d+x)\\s*\\n\\s*Current\\s*:\\s*(\\d+x)");
    QRegularExpressionMatch linkWidthMatch = linkWidthRegex.match(output);
    if (linkWidthMatch.hasMatch()) {
        QString currentWidth = linkWidthMatch.captured(2);
        info.pcieLinkWidth = currentWidth;

        // Calculate PCIe Link Speed (GT/s)
        int currentGen = info.pcieCurrentGen.remove("Gen ").toInt();
        double gtPerSecond = 0;
        switch (currentGen) {
            case 1: gtPerSecond = 2.5; break;
            case 2: gtPerSecond = 5.0; break;
            case 3: gtPerSecond = 8.0; break;
            case 4: gtPerSecond = 16.0; break;
            case 5: gtPerSecond = 32.0; break;
            case 6: gtPerSecond = 64.0; break;
            default: gtPerSecond = 0; break;
        }

        if (gtPerSecond > 0) {
            info.pcieLinkSpeed = QString("%1 GT/s PCIe %2").arg(gtPerSecond).arg(currentWidth);
        }
    }

    // BAR1 Memory (for Resizeable BAR detection)
    QRegularExpression bar1Regex("BAR1 Memory Usage\\s*\\n\\s*Total\\s*:\\s*(\\d+)\\s*MiB");
    QRegularExpressionMatch bar1Match = bar1Regex.match(output);
    if (bar1Match.hasMatch()) {
        info.bar1TotalMB = bar1Match.captured(1).toInt();
        // Resizeable BAR is enabled if BAR1 >= 16 GB (16384 MiB)
        // Standard BAR is only 256 MiB
        info.resizeableBarEnabled = (info.bar1TotalMB >= 16384);
    }

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

    // CUDA Cores (lookup table based on GPU name/architecture)
    info.cudaCores = getCudaCoreCount(info.name, info.architecture);

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

int NvidiaGPUDetector::getCudaCoreCount(const QString& gpuName, const QString& architecture)
{
    // CUDA Core lookup table for common NVIDIA GPUs
    // Based on GPU name and architecture

    QString name = gpuName.toUpper();

    // Ada Lovelace (RTX 40 Series)
    if (name.contains("RTX 4090")) return 16384;
    if (name.contains("RTX 4080 SUPER")) return 10240;
    if (name.contains("RTX 4080")) return 9728;
    if (name.contains("RTX 4070 TI SUPER")) return 8448;
    if (name.contains("RTX 4070 TI")) return 7680;
    if (name.contains("RTX 4070 SUPER")) return 7168;
    if (name.contains("RTX 4070")) return 5888;
    if (name.contains("RTX 4060 TI")) return 4352;
    if (name.contains("RTX 4060")) return 3072;
    if (name.contains("RTX 4050")) return 2560;

    // Ampere (RTX 30 Series)
    if (name.contains("RTX 3090 TI")) return 10752;
    if (name.contains("RTX 3090")) return 10496;
    if (name.contains("RTX 3080 TI")) return 10240;
    if (name.contains("RTX 3080 12GB")) return 8960;
    if (name.contains("RTX 3080")) return 8704;
    if (name.contains("RTX 3070 TI")) return 6144;
    if (name.contains("RTX 3070")) return 5888;
    if (name.contains("RTX 3060 TI")) return 4864;
    if (name.contains("RTX 3060 12GB")) return 3584;
    if (name.contains("RTX 3060")) return 3584;
    if (name.contains("RTX 3050")) return 2560;

    // Turing (RTX 20 Series & GTX 16 Series)
    if (name.contains("RTX 2080 TI")) return 4352;
    if (name.contains("RTX 2080 SUPER")) return 3072;
    if (name.contains("RTX 2080")) return 2944;
    if (name.contains("RTX 2070 SUPER")) return 2560;
    if (name.contains("RTX 2070")) return 2304;
    if (name.contains("RTX 2060 SUPER")) return 2176;
    if (name.contains("RTX 2060")) return 1920;
    if (name.contains("GTX 1660 TI")) return 1536;
    if (name.contains("GTX 1660 SUPER")) return 1408;
    if (name.contains("GTX 1660")) return 1408;
    if (name.contains("GTX 1650 SUPER")) return 1280;
    if (name.contains("GTX 1650")) return 896;

    // Pascal (GTX 10 Series)
    if (name.contains("GTX 1080 TI")) return 3584;
    if (name.contains("GTX 1080")) return 2560;
    if (name.contains("GTX 1070 TI")) return 2432;
    if (name.contains("GTX 1070")) return 1920;
    if (name.contains("GTX 1060 6GB")) return 1280;
    if (name.contains("GTX 1060 3GB")) return 1152;
    if (name.contains("GTX 1060")) return 1280;
    if (name.contains("GTX 1050 TI")) return 768;
    if (name.contains("GTX 1050")) return 640;

    // Professional/Workstation Cards
    if (name.contains("RTX 6000 ADA")) return 18176;
    if (name.contains("RTX 5880 ADA")) return 14080;
    if (name.contains("RTX 5000 ADA")) return 12800;
    if (name.contains("RTX 4500 ADA")) return 7680;
    if (name.contains("RTX 4000 ADA")) return 6144;
    if (name.contains("A100")) return 6912;
    if (name.contains("A40")) return 10752;
    if (name.contains("A30")) return 3584;
    if (name.contains("A10")) return 9216;
    if (name.contains("A6000")) return 10752;
    if (name.contains("A5500")) return 10240;
    if (name.contains("A5000")) return 8192;
    if (name.contains("A4500")) return 5888;
    if (name.contains("A4000")) return 6144;
    if (name.contains("A2000")) return 3328;

    // Titan Series
    if (name.contains("TITAN RTX")) return 4608;
    if (name.contains("TITAN V")) return 5120;
    if (name.contains("TITAN XP")) return 3840;
    if (name.contains("TITAN X")) return 3584;

    // If not found in table, return 0 (unknown)
    return 0;
}
