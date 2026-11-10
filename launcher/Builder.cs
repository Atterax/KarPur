using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace KarPurLauncher;

// The inputs the player chose. Dlc is empty when they have none.
record Inputs(string Base, string Update, string Dlc, string Keys, string Target, string Firmware = "");

// Everything a build does outside the window: unpack the repository files, set up Python, run tools/setup_build.py,
// then put the launcher and a shortcut into the target folder. Progress comes back through the callbacks on a worker
// thread; the window marshals them.
class Builder
{
    public Action<string>? Status;       // a line for the status label
    public Action<int>? Percent;         // overall progress, 0-100
    public Action<string>? Log;          // raw lines for the details box and launcher.log

    public static string Root(string target) => Path.Combine(target, "KarPur");
    public static string Exe(string target) => Path.Combine(Root(target), "build", "game", "karpur.exe");
    public static string Ini(string target) => Path.Combine(Root(target), "build", "game", "karpur.ini");

    // The steps of tools/setup_build.py: their names here and their weight (rough share of a first build's time).
    static readonly Dictionary<string, (string Text, int Weight)> Steps = new()
    {
        ["check"] = ("Checking the game files", 1),
        ["toolchain"] = ("Downloading the compiler (about 160 MB)", 4),
        ["extract"] = ("Unpacking the game code", 2),
        ["fonts"] = ("Unpacking the system fonts", 1),
        ["romfs"] = ("Merging the game data (5-20 minutes)", 18),
        ["menu"] = ("Setting up the PC settings menu", 1),
        ["fps"] = ("Preparing the 60 FPS patches", 1),
        ["icon"] = ("Taking the game icon", 1),
        ["lift"] = ("Translating the game code (10-40 minutes)", 55),
        ["build"] = ("Building karpur.exe (about 5 minutes)", 14),
        ["selftest"] = ("Self-test", 1),
        ["shaders"] = ("Preparing all shaders (about 5 minutes)", 6),
        ["cleanup"] = ("Cleaning up", 1),
    };
    const int SetupShare = 5;   // unpacking the files and setting up Python, before setup_build.py starts

    // Builds (or repairs: Python set up again from scratch) and returns null, or the reason it failed.
    public async Task<string?> Run(Inputs inp, bool repair, CancellationToken cancel)
    {
        string root = Root(inp.Target);
        Directory.CreateDirectory(root);
        using var logFile = new StreamWriter(Path.Combine(root, "launcher.log"), append: true, Encoding.UTF8);
        void L(string s) { lock (logFile) { logFile.WriteLine(s); logFile.Flush(); } Log?.Invoke(s); }
        L($"=== {DateTime.Now:yyyy-MM-dd HH:mm:ss} launcher {Program.Version}{(repair ? " (repair)" : "")}");

        Status?.Invoke(repair ? "Restoring the project files" : "Unpacking the project files");
        await Task.Run(() => UnpackPayload(root), cancel);
        Percent?.Invoke(2);
        string python = await SetupPython(root, repair, L, cancel);
        Percent?.Invoke(SetupShare);

        var psi = new ProcessStartInfo(python)
        {
            WorkingDirectory = root, RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false,
            CreateNoWindow = true, StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
        };
        foreach (var a in new[] { Path.Combine(root, "tools", "setup_build.py"), "--base", inp.Base, "--update", inp.Update,
                                  "--keys", inp.Keys })
            psi.ArgumentList.Add(a);
        if (inp.Dlc != "") { psi.ArgumentList.Add("--dlc"); psi.ArgumentList.Add(inp.Dlc); }
        // the shared fonts from the console's system files: a base game as NSP, or an XCI converted from one (issue #18)
        if (inp.Firmware != "") { psi.ArgumentList.Add("--firmware"); psi.ArgumentList.Add(inp.Firmware); }
        psi.Environment["PYTHONIOENCODING"] = "utf-8";
        psi.Environment["PYTHONUNBUFFERED"] = "1";

        using var p = Process.Start(psi) ?? throw new InvalidOperationException("Python could not be started");
        using var reg = cancel.Register(() => { try { p.Kill(entireProcessTree: true); } catch { } });
        var stderr = p.StandardError.ReadToEndAsync();
        string? error = null, exe = null;
        List<string> plan = new();
        int before = SetupShare, weight = 0;
        string? line;
        while ((line = await p.StandardOutput.ReadLineAsync()) != null)
        {
            var parts = line.Split(' ', 4);
            switch (parts[0])
            {
                case "PLAN" when parts.Length >= 2:
                    plan = parts[1].Split(',').ToList();
                    L(line);
                    break;
                case "STEP" when parts.Length >= 4:
                    int total = Math.Max(1, plan.Sum(s => Steps.GetValueOrDefault(s).Weight));
                    int done = plan.TakeWhile(s => s != parts[3]).Sum(s => Steps.GetValueOrDefault(s).Weight);
                    before = SetupShare + done * (100 - SetupShare) / total;
                    weight = Steps.GetValueOrDefault(parts[3]).Weight * (100 - SetupShare) / total;
                    Status?.Invoke($"Step {parts[1]} of {parts[2]}: " + (Steps.TryGetValue(parts[3], out var st) ? st.Text : parts[3]));
                    Percent?.Invoke(before);
                    L(line);
                    break;
                case "PERCENT" when parts.Length >= 2 && int.TryParse(parts[^1], out int pc):
                    Percent?.Invoke(before + weight * Math.Clamp(pc, 0, 100) / 100);
                    break;
                case "LOG":
                    L(line.Length > 4 ? line[4..] : "");
                    break;
                case "DONE":
                    exe = line[5..];
                    L(line);
                    break;
                case "ERROR":
                    error = line[6..];
                    L(line);
                    break;
                default:
                    L(line);
                    break;
            }
        }
        await p.WaitForExitAsync(cancel);
        string err = await stderr;
        if (err.Trim() != "") L(err);
        if (exe == null || p.ExitCode != 0)
            return error ?? (err.Trim() != "" ? err.Trim().Split('\n')[^1] : $"The build ended with code {p.ExitCode}");
        try { Install(inp.Target, L); } catch (Exception e) { L("warning: " + e.Message); }
        Percent?.Invoke(100);
        return null;
    }

    // The launcher next to the game (for updates) and a shortcut with the game's icon in the target folder.
    static void Install(string target, Action<string> L)
    {
        string self = Environment.ProcessPath ?? "";
        string copy = Path.Combine(target, "KarPur-Launcher.exe");
        if (self != "" && !string.Equals(Path.GetFullPath(self), Path.GetFullPath(copy), StringComparison.OrdinalIgnoreCase))
            File.Copy(self, copy, overwrite: true);
        string exe = Exe(target);
        foreach (var old in Directory.GetFiles(target, "* (KarPur).lnk")) File.Delete(old);
        string name = Game(target) == "scarlet" ? "Pokémon Scarlet (KarPur)" : "Pokémon Violet (KarPur)";
        var shellType = Type.GetTypeFromProgID("WScript.Shell") ?? throw new InvalidOperationException("no WScript.Shell");
        dynamic shell = Activator.CreateInstance(shellType)!;
        dynamic lnk = shell.CreateShortcut(Path.Combine(target, name + ".lnk"));
        lnk.TargetPath = exe;
        lnk.WorkingDirectory = Root(target);
        lnk.IconLocation = exe + ",0";
        lnk.Description = "Start " + name;
        lnk.Save();
        L($"shortcut {name}.lnk and KarPur-Launcher.exe in {target}");
    }

    // "violet", "scarlet" or "unknown": the game a build was made from (tools/setup_build.py, build/setup_info.json).
    // The launcher version that last finished a build of this target (build\launcher_version.txt): a launcher replaced by
    // hand (downloaded, copied) is not started with --updated, and the game stayed built from the older files.
    public static string BuiltWith(string target)
    {
        try { return File.ReadAllText(Path.Combine(Root(target), "build", "launcher_version.txt")).Trim(); }
        catch { return ""; }
    }

    public static void NoteBuilt(string target)
    {
        try { File.WriteAllText(Path.Combine(Root(target), "build", "launcher_version.txt"), Program.Version); }
        catch { }
    }

    public static string Game(string target)
    {
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(Root(target), "build", "setup_info.json")));
            return doc.RootElement.GetProperty("game").GetString() ?? "unknown";
        }
        catch { return "unknown"; }
    }

    // The repository's files (and the prebuilt translator and hactool) into root; what the build made stays. Files the
    // player changed or deleted come back as they were released: this is also the source half of Repair.
    static void UnpackPayload(string root)
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")
            ?? throw new InvalidOperationException("This launcher lacks its project files (payload.zip)");
        using var zip = new ZipArchive(s);
        string full = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
        foreach (var e in zip.Entries)
        {
            if (e.FullName.EndsWith('/')) continue;
            string dest = Path.GetFullPath(Path.Combine(root, e.FullName));
            if (!dest.StartsWith(full, StringComparison.OrdinalIgnoreCase)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            e.ExtractToFile(dest, overwrite: true);
        }
    }

    record Pin(string url, long size, string sha256);
    record WheelPin(string name, string version, string url, long size, string sha256);
    record Pins(Pin python, string python_version, List<WheelPin> wheels);

    static Pins LoadPins()
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("pins.json")!;
        return JsonSerializer.Deserialize<Pins>(s)!;
    }

    // Python's embeddable package and the pinned wheels in root\python; returns python.exe. Repair sets it up again.
    async Task<string> SetupPython(string root, bool repair, Action<string> L, CancellationToken cancel)
    {
        var pins = LoadPins();
        string dir = Path.Combine(root, "python"), exe = Path.Combine(dir, "python.exe");
        string stamp = Path.Combine(dir, "karpur-pins.txt");
        string want = pins.python.sha256 + string.Concat(pins.wheels.Select(w => w.sha256));
        if (!repair && File.Exists(exe) && File.Exists(stamp) && File.ReadAllText(stamp) == want) return exe;

        if (Directory.Exists(dir)) Directory.Delete(dir, true);
        Directory.CreateDirectory(dir);
        using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        Status?.Invoke($"Downloading Python {pins.python_version}");
        ZipFile.ExtractToDirectory(new MemoryStream(await Download(http, pins.python, L, cancel)), dir);
        string site = Path.Combine(dir, "Lib", "site-packages");
        Directory.CreateDirectory(site);
        foreach (var w in pins.wheels)
        {
            Status?.Invoke($"Downloading the Python package {w.name}");
            ZipFile.ExtractToDirectory(new MemoryStream(await Download(http, new Pin(w.url, w.size, w.sha256), L, cancel)),
                                       site, overwriteFiles: true);
        }
        // The ._pth file decides sys.path in an embeddable Python: the standard library, the wheels, the repository root
        // (recomp/) and tools/ (the scripts import each other).
        foreach (var pth in Directory.GetFiles(dir, "python*._pth"))
        {
            string zipName = Path.GetFileNameWithoutExtension(pth) + ".zip";
            File.WriteAllText(pth, string.Join("\r\n", zipName, ".", @"Lib\site-packages", root,
                                               Path.Combine(root, "tools"), "import site") + "\r\n");
        }
        File.WriteAllText(stamp, want);
        return exe;
    }

    static async Task<byte[]> Download(HttpClient http, Pin pin, Action<string> L, CancellationToken cancel)
    {
        L($"download {pin.url}");
        byte[] data = await http.GetByteArrayAsync(pin.url, cancel);
        string got = Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();
        if (data.Length != pin.size || got != pin.sha256)
            throw new InvalidOperationException($"{Path.GetFileName(pin.url)} does not match its expected checksum");
        return data;
    }
}
