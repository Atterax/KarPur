using System.Diagnostics;
using System.IO.Compression;

namespace KarPurLauncher;

// The Saves page: the game's save data in the build's profile folder (runtime/fs_host.c):
//   <profile>/save/<uid>/          the committed save (files main, backup, poke_trade)
//   <profile>/save-tx/ready-<n>/   a finished commit the next start swaps in (newer than save/<uid>)
//   <profile>/save-backup/<time>/  the runtime's backups of earlier saves
// <profile> is build\game\profile when it exists (a portable folder), else build\profile. Export writes the committed
// save into a .zip; Import (and Restore) first moves the current save into save-backup\<time>-launcher, never deletes it.
// Both refuse while the game runs.
partial class MainForm
{
    readonly Label saveWhere = Theme.Label("", 9.5f, muted: true), saveInfo = Theme.Label("", 11f, style: FontStyle.Bold),
                   saveNote = Theme.Label("", 9.5f, muted: true);
    readonly Button exportSave = Theme.Button("Export…", true, Theme.GlyphExport), importSave = Theme.Button("Import…", glyph: Theme.GlyphImport),
                    openSaves = Theme.Button("Open folder", glyph: Theme.GlyphFolder), restoreBackup = Theme.Button("Restore", glyph: Theme.GlyphHistory);
    readonly ComboBox backups = Combo();

    Control SavesPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var current = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.TopDown, BackColor = Theme.Card, WrapContents = false };
        var title = Theme.Label("CURRENT SAVE", 8.5f, muted: true, style: FontStyle.Bold);
        saveInfo.Margin = new Padding(0, 6, 0, 2);
        saveWhere.MaximumSize = new Size(700, 0);
        current.Controls.AddRange(new Control[] { title, saveInfo, saveWhere });
        var actions = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0, 12, 0, 0) };
        actions.Controls.AddRange(new Control[] { exportSave, importSave, openSaves });

        var restore = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Card, WrapContents = false };
        backups.Width = 360;
        backups.Margin = new Padding(0, 4, 10, 0);
        restore.Controls.AddRange(new Control[] { backups, restoreBackup });
        var backupTitle = Theme.Label("BACKUPS", 8.5f, muted: true, style: FontStyle.Bold);
        var backupText = Theme.Label("KarPur keeps your last saves. Restoring one keeps the current save as a backup too.", 9.5f, muted: true);
        backupText.Margin = new Padding(0, 4, 0, 8);
        saveNote.MaximumSize = new Size(700, 0);

        foreach (var l in new[] { backupTitle, backupText, saveNote }) l.Dock = DockStyle.Top;   // CardOf stacks docked rows
        page.Controls.Add(CardOf(backupTitle, backupText, restore));
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(current, Spacer(14, Theme.Card), actions, Spacer(8, Theme.Card), saveNote));
        page.Controls.Add(Header("Saves", "Your progress, kept in the game's folder. Back it up, move it to another PC or bring one back."));
        foreach (Control c in new Control[] { current, actions, restore }) c.BackColor = Theme.Card;

        exportSave.Click += (_, _) => ExportSave();
        importSave.Click += (_, _) => ImportSave();
        openSaves.Click += (_, _) =>
        {
            string? dir = SaveDir() ?? ProfileDir();
            if (dir != null && Directory.Exists(dir)) Process.Start(new ProcessStartInfo(dir) { UseShellExecute = true });
        };
        restoreBackup.Click += (_, _) => RestoreBackup();
        return page;
    }

    string? ProfileDir()
    {
        if (targetBox.Text == "") return null;
        string root = Builder.Root(targetBox.Text);
        string portable = Path.Combine(root, "build", "game", "profile");
        return Directory.Exists(portable) ? portable : Path.Combine(root, "build", "profile");
    }

    // save\<uid>: the existing folder, else the name the runtime gives it (account.uid's 16 bytes, reversed, in hex)
    string? SaveDir()
    {
        string? profile = ProfileDir();
        if (profile == null) return null;
        string saves = Path.Combine(profile, "save");
        if (Directory.Exists(saves))
        {
            var dirs = Directory.GetDirectories(saves);
            if (dirs.Length > 0) return dirs.OrderByDescending(Directory.GetLastWriteTime).First();
        }
        try
        {
            var uid = File.ReadAllBytes(Path.Combine(profile, "account.uid"));
            if (uid.Length == 16) return Path.Combine(saves, Convert.ToHexString(uid.Reverse().ToArray()).ToLowerInvariant());
        }
        catch { }
        return null;
    }

    DateTime? NewestSave()
    {
        string? dir = SaveDir();
        string? main = dir != null ? Path.Combine(dir, "main") : null;
        return main != null && File.Exists(main) ? File.GetLastWriteTime(main) : null;
    }

    static bool GameRunning() => Process.GetProcessesByName("karpur").Length > 0;

    void RefreshSaves()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        string? dir = SaveDir();
        var last = NewestSave();
        saveInfo.Text = !built ? "Build the game first" : last != null ? $"Saved {last.Value:dddd, d MMMM yyyy, HH:mm}" : "No save yet: start a new game first";
        saveWhere.Text = dir ?? "";
        exportSave.Enabled = built && last != null;
        importSave.Enabled = built && dir != null;
        openSaves.Enabled = built && dir != null;
        backups.Items.Clear();
        string? profile = ProfileDir();
        string bdir = profile != null ? Path.Combine(profile, "save-backup") : "";
        if (built && Directory.Exists(bdir))
            foreach (var d in Directory.GetDirectories(bdir).Where(d => File.Exists(Path.Combine(d, "main"))).OrderByDescending(Path.GetFileName))
                backups.Items.Add(new BackupItem(d));
        if (backups.Items.Count > 0) backups.SelectedIndex = 0;
        backups.Enabled = restoreBackup.Enabled = built && backups.Items.Count > 0;
        bool pending = profile != null && Directory.Exists(Path.Combine(profile, "save-tx")) &&
                       Directory.GetDirectories(Path.Combine(profile, "save-tx"), "ready-*").Length > 0;
        if (saveNote.ForeColor != Theme.Error)
            saveNote.Text = pending ? "The game finished a save that it applies at its next start: start and close the game once before exporting."
                                    : "Close the game before you import or restore a save.";
    }

    record BackupItem(string Dir)
    {
        public override string ToString()
        {
            var t = File.GetLastWriteTime(Path.Combine(Dir, "main"));   // when the game wrote that save
            string name = Path.GetFileName(Dir);
            return $"{t:yyyy-MM-dd  HH:mm}" + (name.Contains("-launcher") ? "  (kept by the launcher)" : "");
        }
    }

    void SaveMessage(string text, bool error = false)
    {
        saveNote.ForeColor = error ? Theme.Error : Theme.Muted;
        saveNote.Text = text;
    }

    void ExportSave()
    {
        string? dir = SaveDir();
        if (dir == null || !Directory.Exists(dir)) return;
        using var dlg = new SaveFileDialog
        {
            Filter = "Save archive|*.zip", FileName = $"KarPur-save-{DateTime.Now:yyyy-MM-dd-HHmm}.zip",
            InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
        };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            if (File.Exists(dlg.FileName)) File.Delete(dlg.FileName);
            ZipFile.CreateFromDirectory(dir, dlg.FileName, CompressionLevel.Optimal, includeBaseDirectory: false);
            SaveMessage($"Exported to {dlg.FileName}.");
        }
        catch (Exception e) { SaveMessage("Could not export: " + e.Message, true); }
    }

    // The current save (and a finished commit not applied yet) into save-backup\<time>-launcher; returns false on failure.
    bool BackUpCurrent(string profile, string dir)
    {
        string dest = Path.Combine(profile, "save-backup", $"{DateTime.Now:yyyyMMdd-HHmmss}-launcher");
        Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
        if (Directory.Exists(dir)) Directory.Move(dir, dest);
        string tx = Path.Combine(profile, "save-tx");
        if (Directory.Exists(tx))
            foreach (var ready in Directory.GetDirectories(tx, "ready-*"))
                Directory.Move(ready, Path.Combine(dest + "-pending-" + Path.GetFileName(ready)));
        return true;
    }

    void ImportSave()
    {
        string? profile = ProfileDir(), dir = SaveDir();
        if (profile == null || dir == null) return;
        if (GameRunning()) { SaveMessage("Close the game first.", true); return; }
        using var dlg = new OpenFileDialog { Filter = "Save archive|*.zip" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            using (var zip = ZipFile.OpenRead(dlg.FileName))
                if (!zip.Entries.Any(e => e.FullName.Equals("main", StringComparison.OrdinalIgnoreCase)))
                {
                    SaveMessage("This archive holds no save: a KarPur save archive has the file \"main\" at its top level.", true);
                    return;
                }
            if (MessageBox.Show(this, "Replace your current save with this one? The current save is kept as a backup.", "Import save",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                return;
            BackUpCurrent(profile, dir);
            Directory.CreateDirectory(dir);
            ZipFile.ExtractToDirectory(dlg.FileName, dir);
            SaveMessage("Imported. The game loads it at its next start.");
        }
        catch (Exception e) { SaveMessage("Could not import: " + e.Message, true); }
        RefreshSaves();
        RefreshHome();
    }

    void RestoreBackup()
    {
        string? profile = ProfileDir(), dir = SaveDir();
        if (profile == null || dir == null || backups.SelectedItem is not BackupItem b) return;
        if (GameRunning()) { SaveMessage("Close the game first.", true); return; }
        if (MessageBox.Show(this, $"Restore the save from {b}? The current save is kept as a backup.", "Restore save",
                MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            return;
        try
        {
            string src = b.Dir;
            BackUpCurrent(profile, dir);
            Directory.CreateDirectory(dir);
            foreach (var f in Directory.GetFiles(src)) File.Copy(f, Path.Combine(dir, Path.GetFileName(f)), true);
            SaveMessage("Restored. The game loads it at its next start.");
        }
        catch (Exception e) { SaveMessage("Could not restore: " + e.Message, true); }
        RefreshSaves();
        RefreshHome();
    }
}
