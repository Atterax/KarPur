using System.Diagnostics;
using System.Net.Http.Headers;
using System.Text.Json;

namespace KarPurLauncher;

// Updates from the GitHub releases page: a release tagged vX.Y.Z with a KarPur-Launcher.exe asset. The new launcher
// replaces the running one (renamed to .old, deleted at the next start) and is started with --updated, which rebuilds
// what the release changed.
static class Updater
{
    const string Api = "https://api.github.com/repos/Grunttanamo/KarPur/releases/latest";
    const string Asset = "KarPur-Launcher.exe";

    public record Release(Version Version, string Tag, string Url, string Notes);

    static HttpClient Http()
    {
        var http = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("KarPur-Launcher", Program.Version));
        http.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
        return http;
    }

    // The newest release, or null when there is none yet.
    public static async Task<Release?> Latest(CancellationToken cancel)
    {
        using var http = Http();
        using var r = await http.GetAsync(Api, cancel);
        if (r.StatusCode == System.Net.HttpStatusCode.NotFound) return null;
        r.EnsureSuccessStatusCode();
        using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync(cancel));
        string tag = doc.RootElement.GetProperty("tag_name").GetString() ?? "";
        if (!Version.TryParse(tag.TrimStart('v', 'V'), out var v)) return null;
        string? url = null;
        foreach (var a in doc.RootElement.GetProperty("assets").EnumerateArray())
            if (a.GetProperty("name").GetString() == Asset) url = a.GetProperty("browser_download_url").GetString();
        string notes = doc.RootElement.TryGetProperty("body", out var b) ? b.GetString() ?? "" : "";
        return url == null ? null : new Release(v, tag, url, notes);
    }

    public static bool Newer(Release r) =>
        new Version(r.Version.Major, r.Version.Minor, Math.Max(0, r.Version.Build)) > Program.Current;

    // Downloads the release's launcher, puts it in place of this one and starts it; the caller then closes.
    public static async Task Install(Release r, CancellationToken cancel)
    {
        string self = Environment.ProcessPath ?? throw new InvalidOperationException("unknown launcher path");
        string fresh = self + ".new";
        using (var http = Http())
            await File.WriteAllBytesAsync(fresh, await http.GetByteArrayAsync(r.Url, cancel), cancel);
        string old = self + ".old";
        if (File.Exists(old)) File.Delete(old);
        File.Move(self, old);          // a running exe can be renamed, not overwritten
        File.Move(fresh, self);
        Process.Start(new ProcessStartInfo(self, "--updated") { UseShellExecute = false });
    }

    public static void CleanUpOld()
    {
        try
        {
            string? self = Environment.ProcessPath;
            if (self != null && File.Exists(self + ".old")) File.Delete(self + ".old");
        }
        catch { }
    }
}
