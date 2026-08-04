# Captured tool output

Real output from `nvidia-smi -q`, `lspci -D -nnk`, `lscpu` and `kscreen-doctor -o`,
used by `lib/stubs.sh` to stand in for those programs.

They are here because the parsers that read them (`NvidiaGPUDetector`,
`GPUDetector`, `CPUDetector`, `KdeDisplayProbe`) are pure text-to-value functions
and this is the only way to exercise them against real-world input on a machine
with no NVIDIA GPU — which is every CI runner.

Machine-identifying values (GPU UUID, display id, home directory, the running
process list) have been replaced with placeholders. Everything else is verbatim,
because the point is the real shape of the text.

To refresh one of these, run the corresponding command and scrub it again:

    nvidia-smi -q          > nvidia-smi-q.txt
    lspci -D -nnk          > lspci-D-nnk.txt
    LC_ALL=C lscpu         > lscpu.txt
    kscreen-doctor -o      > kscreen-doctor-o.txt
