namespace KarPurLauncher;

// karpur.ini next to karpur.exe, the file the game's own PC settings page writes (runtime/cfg.c): "[section]" lines and
// "key = value" lines, keys in lower case. Changing a value keeps every other line, comment and order as it is.
class Ini
{
    readonly string path;
    readonly List<string> lines;

    public Ini(string path)
    {
        this.path = path;
        lines = File.Exists(path) ? File.ReadAllLines(path).ToList() : new List<string>();
    }

    public string? Get(string key)
    {
        foreach (var l in lines)
        {
            int eq = l.IndexOf('=');
            if (eq > 0 && l[..eq].Trim().Equals(key, StringComparison.OrdinalIgnoreCase)) return l[(eq + 1)..].Trim();
        }
        return null;
    }

    public void Set(string section, string key, string value)
    {
        key = key.ToLowerInvariant();
        for (int i = 0; i < lines.Count; i++)
        {
            int eq = lines[i].IndexOf('=');
            if (eq > 0 && lines[i][..eq].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
            {
                lines[i] = $"{key} = {value}";
                return;
            }
        }
        int at = lines.FindIndex(l => l.Trim().Equals($"[{section}]", StringComparison.OrdinalIgnoreCase));
        if (at < 0)
        {
            if (lines.Count > 0 && lines[^1].Trim() != "") lines.Add("");
            lines.Add($"[{section}]");
            lines.Add($"{key} = {value}");
            return;
        }
        int end = at + 1;
        while (end < lines.Count && !lines[end].TrimStart().StartsWith('[') && lines[end].Trim() != "") end++;
        lines.Insert(end, $"{key} = {value}");
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllLines(path, lines);
    }
}
