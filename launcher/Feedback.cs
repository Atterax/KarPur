using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace KarPurLauncher;

// The Feedback tab's data: what the launcher can tell about this PC, and the GitHub issue form it opens
// (.github/ISSUE_TEMPLATE/feedback.yml, its field ids are the keys of Fields). Nothing is sent by the launcher itself:
// the player sees the filled-in form in their browser and submits it there.
static class Feedback
{
    const string NewIssue = "https://github.com/Grunttanamo/KarPur/issues/new";
    public const int ReportLimit = 5000;
    const int UrlLimit = 7800;   // GitHub refuses much longer request URLs

    public record System_(string Cpu, string Gpu, string Ram, string Windows);

    // CPU, GPU, RAM (type and size) and Windows, from Windows' own inventory (one PowerShell CIM query, a few seconds);
    // the registry and the kernel when that fails.
    public static async Task<System_> Detect()
    {
        string cpu = "", gpu = "", ramType = "";
        long installed = 0;   // the modules' size (the kernel reports less: some is reserved)
        try
        {
            const string query =
                "$c=(Get-CimInstance Win32_Processor | Select -First 1).Name;" +
                "$g=@(Get-CimInstance Win32_VideoController | Where-Object { $_.Name -notmatch 'Basic|Remote|Virtual|Parsec' } | ForEach-Object Name);" +
                // a CPU's built-in graphics only when there is no other card
                "$d=@($g | Where-Object { $_ -notmatch '^(AMD Radeon\\(TM\\) Graphics|Intel\\(R\\) (UHD|HD|Iris).*)$' }); if ($d.Count) { $g=$d };" +
                "$g=$g -join ' + ';" +
                "$m=(Get-CimInstance Win32_PhysicalMemory | Measure-Object Capacity -Sum).Sum;" +
                "$t=(Get-CimInstance Win32_PhysicalMemory | Select -First 1).SMBIOSMemoryType;" +
                "@{cpu=$c;gpu=$g;type=$t;mem=$m} | ConvertTo-Json -Compress";
            var psi = new ProcessStartInfo("powershell", $"-NoProfile -NonInteractive -Command \"{query}\"")
            {
                RedirectStandardOutput = true, UseShellExecute = false, CreateNoWindow = true, StandardOutputEncoding = Encoding.UTF8,
            };
            using var p = Process.Start(psi)!;
            string json = await p.StandardOutput.ReadToEndAsync();
            await p.WaitForExitAsync();
            using var doc = JsonDocument.Parse(json);
            cpu = doc.RootElement.GetProperty("cpu").GetString() ?? "";
            gpu = doc.RootElement.GetProperty("gpu").GetString() ?? "";
            if (doc.RootElement.TryGetProperty("mem", out var m) && m.ValueKind == JsonValueKind.Number) installed = m.GetInt64();
            if (doc.RootElement.TryGetProperty("type", out var t) && t.ValueKind == JsonValueKind.Number)
                ramType = t.GetInt32() switch { 24 => "DDR3", 26 => "DDR4", 34 => "DDR5", 35 => "LPDDR5", 30 => "LPDDR4", _ => "" };
        }
        catch { }
        if (cpu == "")
            cpu = Registry.GetValue(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0", "ProcessorNameString", "") as string ?? "";
        long gb = (long)Math.Round((installed > 0 ? installed : (double)TotalMemory()) / (1024.0 * 1024 * 1024));
        string ram = (ramType != "" ? ramType + ", " : "") + $"{gb} GB";
        return new System_(cpu.Trim(), gpu.Trim(), ram, WindowsName());
    }

    public static string WindowsName() => Environment.OSVersion.Version.Build >= 22000 ? "Windows 11" : "Windows 10";

    [StructLayout(LayoutKind.Sequential)]
    struct MemoryStatus
    {
        public uint Length, Load;
        public ulong TotalPhys, AvailPhys, TotalPage, AvailPage, TotalVirtual, AvailVirtual, AvailExtended;
    }
    [DllImport("kernel32.dll")]
    static extern bool GlobalMemoryStatusEx(ref MemoryStatus s);

    static ulong TotalMemory()
    {
        var s = new MemoryStatus { Length = (uint)Marshal.SizeOf<MemoryStatus>() };
        return GlobalMemoryStatusEx(ref s) ? s.TotalPhys : 0;
    }

    // The last lines of the game's log that fit into room characters, with the user's folder names left out.
    public static string LogTail(string target, int room)
    {
        string log = Path.Combine(Builder.Root(target), "build", "game", "logs", "karpur.log");
        if (!File.Exists(log) || room < 200) return "";
        string[] lines;
        using (var fs = new FileStream(log, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
        using (var r = new StreamReader(fs)) lines = r.ReadToEnd().Split('\n');
        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        var picked = new List<string>();
        int used = 40;
        for (int i = lines.Length - 1; i >= 0; i--)
        {
            string l = lines[i].TrimEnd('\r').Replace(home, "%USERPROFILE%", StringComparison.OrdinalIgnoreCase);
            if (used + l.Length + 1 > room) break;
            picked.Insert(0, l);
            used += l.Length + 1;
        }
        return "```\n" + string.Join("\n", picked).Trim() + "\n```";
    }

    public static string Text(Dictionary<string, string> f) =>
        $"Launcher version: {f["launcher"]}\nGame version: {f["game"]}\nLatest game update (4.0.0): {f["update"]}\n" +
        $"DLC: {f["dlc"]}\nGPU: {f["gpu"]}\nCPU: {f["cpu"]}\nRAM: {f["ram"]}\nWindows version: {f["windows"]}\n\n{f["report"]}";

    // Opens the issue form with every field filled in. Returns false when the report had to stay out of the URL (too
    // long once encoded); it is then on the clipboard for the player to paste.
    public static bool Open(Dictionary<string, string> f, string title)
    {
        string url = NewIssue + "?template=feedback.yml&title=" + Uri.EscapeDataString(title);
        foreach (var (k, v) in f)
            if (k != "report") url += "&" + k + "=" + Uri.EscapeDataString(v);
        string withReport = url + "&report=" + Uri.EscapeDataString(f["report"]);
        bool fits = withReport.Length <= UrlLimit;
        if (!fits)
        {
            Clipboard.SetText(f["report"]);
            withReport = url + "&report=" + Uri.EscapeDataString("(The report is on the clipboard: paste it here with Ctrl+V.)");
        }
        Process.Start(new ProcessStartInfo(withReport) { UseShellExecute = true });
        return fits;
    }
}
