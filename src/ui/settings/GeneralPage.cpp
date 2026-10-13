#include "ui/settings/GeneralPage.h"

#include <QGroupBox>

#include "utils/GpuInfoCache.h"

GeneralPage::GeneralPage(QWidget* parent)
    : SettingsPage(parent)
{
    QGroupBox* group = new QGroupBox("General", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    bindFlag(layout, &DLSSSettings::enableNVAPI,
             "Enable NVAPI",
             "NVIDIA API support in Proton (dxvk-nvapi).\n\n"
             "Required for DLSS and other NVIDIA features to work. "
             "dxvk-nvapi translates NVIDIA-specific DirectX calls to Vulkan.\n\n"
             "Current Proton versions enable it on NVIDIA GPUs by default, so nothing "
             "is written while this is checked. Unchecking it sets PROTON_DISABLE_NVAPI=1.\n\n"
             "Recommended: Enabled for all NVIDIA GPU users.",
             "PROTON_DISABLE_NVAPI");

    bindFlag(layout, &DLSSSettings::enableNGXUpdater,
             "Enable NGX Updater (PROTON_ENABLE_NGX_UPDATER)",
             "Allow NVIDIA NGX to automatically update DLSS DLLs.\n\n"
             "When enabled, NGX can download newer DLSS versions from NVIDIA servers. "
             "This may improve quality and performance in some games.\n\n"
             "Note: Requires internet connection and may increase loading times.",
             "PROTON_ENABLE_NGX_UPDATER");

    // Driver level — stays live on a native game.
    m_enablePrimeOffload =
        bindFlag(layout, &DLSSSettings::enablePrimeRenderOffload,
                 "Use NVIDIA GPU (PRIME Render Offload)",
                 "Render this game on the NVIDIA dGPU instead of the integrated GPU.\n\n"
                 "For hybrid-graphics systems (e.g. laptops with Intel/AMD iGPU + NVIDIA "
                 "dGPU) where the iGPU is the primary GPU. Not needed when the NVIDIA "
                 "GPU is the only or primary GPU — some distros configure this "
                 "automatically.\n\n"
                 "Sets __NV_PRIME_RENDER_OFFLOAD=1, __GLX_VENDOR_LIBRARY_NAME=nvidia "
                 "and __VK_LAYER_NV_optimus=NVIDIA_only (works on any distro, no "
                 "hard-coded driver paths).",
                 "__NV_PRIME_RENDER_OFFLOAD");

    bindFlag(layout, &DLSSSettings::enableReflex,
             "Enable Reflex (DXVK_NVAPI_VKREFLEX)",
             "Enable NVIDIA Reflex for Vulkan games via DXVK-NVAPI's Reflex layer.\n\n"
             "Reduces input latency and is the recommended companion to Frame Generation "
             "and Smooth Motion. Needed for Reflex in native Vulkan titles "
             "(e.g. Portal RTX, Path of Exile).\n\n"
             "Sets DXVK_NVAPI_VKREFLEX=1.",
             "DXVK_NVAPI_VKREFLEX", FeatureGate::Feature::Reflex, QStringLiteral("Reflex"));

    bindFlag(layout, &DLSSSettings::enableVkd3dLowLatency,
             "VKD3D Low Latency (PROTON_VKD3D_LOWLATENCY)",
             "Enable low-latency frame pacing in VKD3D-Proton for DirectX 12 games.\n\n"
             "Hardware-agnostic and natively implements the NVIDIA Reflex API, "
             "reducing input latency in DX12 titles. The DX12 counterpart to the "
             "Reflex option above.\n\n"
             "Requires: Proton-CachyOS 11.0-20260703 or newer.\n\n"
             "Sets PROTON_VKD3D_LOWLATENCY=1.",
             "PROTON_VKD3D_LOWLATENCY", FeatureGate::Feature::Vkd3dLowLatency, QStringLiteral("VKD3D Low Latency"));

    bindFlag(layout, &DLSSSettings::enableVkd3dDescriptorHeap,
             "VKD3D Descriptor Heap (experimental)",
             "Enable vkd3d-proton's new VK_EXT_descriptor_heap path for DirectX 12 "
             "games.\n\n"
             "Can improve performance and frame pacing, but is experimental and "
             "known to crash some games — test per game.\n\n"
             "Requires: NVIDIA driver 595.44.02 or newer.\n\n"
             "Sets VKD3D_CONFIG=descriptor_heap.",
             "VKD3D_CONFIG", FeatureGate::Feature::Vkd3dDescriptorHeap, QStringLiteral("VKD3D Descriptor Heap"));

    bindFlag(layout, &DLSSSettings::enableD3D12ShaderExtensions,
             "D3D12 NVIDIA Shader Extensions (experimental)",
             "Let DirectX 12 games use NVIDIA's shader extensions — today that means "
             "Shader Execution Reordering (SER), which speeds up ray and path tracing.\n\n"
             "Benefits e.g. Alan Wake 2, Cyberpunk 2077 (with some known issues) and many "
             "Unreal Engine 5 titles such as Black Myth: Wukong. Experimental — DXVK-NVAPI "
             "describes it as a troubleshooting tweak, so test per game.\n\n"
             "Requires: DXVK-NVAPI 0.9.2 and vkd3d-proton 3.0.1 (Valve Proton 11.0-2, "
             "current Proton-CachyOS / GE-Proton).\n\n"
             "Sets DXVK_NVAPI_D3D12_NV_SHADER_EXTN=1.",
             "DXVK_NVAPI_D3D12_NV_SHADER_EXTN", FeatureGate::Feature::D3D12ShaderExtensions, QStringLiteral("D3D12 Shader Extensions"));

    bindFlag(layout, &DLSSSettings::showIndicator,
             "Show DLSS Indicator (PROTON_DLSS_INDICATOR)",
             "Display an on-screen DLSS status indicator in-game.\n\n"
             "Shows which DLSS features are active (SR/RR/FG) and their settings. "
             "Useful for verifying that your settings are being applied correctly.\n\n"
             "The indicator appears as an overlay in the corner of the screen.",
             "PROTON_DLSS_INDICATOR");

    pageLayout()->addWidget(group);
}

void GeneralPage::collectWarnings(const FeatureGate::Context& ctx, QStringList& lines) const
{
    SettingsPage::collectWarnings(ctx, lines);

    // PRIME offload is gated by hardware, not driver/Proton versions: warn only
    // when the probe positively found a non-hybrid system (Unknown stays silent).
    if (m_enablePrimeOffload->isChecked() &&
        GpuInfoCache::instance().hybridGpu() == GPUDetector::HybridGpu::No) {
        lines << QStringLiteral(
            "⚠ PRIME Render Offload: no hybrid iGPU + dGPU setup detected — "
            "this option has no effect on this system");
    }
}
