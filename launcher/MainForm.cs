using System.Diagnostics;
using System.Text.Json;

namespace KarPurLauncher;

// The launcher window: a sidebar (name, version, Setup / Settings / Update, Play) and one page at a time. The paths and
// the colour choice are remembered in %LOCALAPPDATA%\KarPur\launcher.json (paths only, never file contents).
class MainForm : Form
{
    // Setup
    readonly TextBox baseBox = Theme.TextBox(), updateBox = Theme.TextBox(), dlcBox = Theme.TextBox(),
                     keysBox = Theme.TextBox(), targetBox = Theme.TextBox();
    readonly Button buildButton = Theme.Button("Build", true, Theme.GlyphBuild),
                    repairButton = Theme.Button("Repair", glyph: Theme.GlyphRepair),
                    cancelButton = Theme.Button("Cancel", glyph: Theme.GlyphStop),
                    playButton = Theme.Button("Play", true, Theme.GlyphPlay);
    readonly Bar progress = new();
    readonly Label status = Theme.Label("Choose your files and a target folder, then press Build.", 9.5f, muted: true);
    readonly CheckBox details = new() { Text = "Show details", AutoSize = true, ForeColor = Theme.Muted, BackColor = Theme.Card };
    readonly TextBox log = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Dock = DockStyle.Fill,
        Visible = false, BackColor = Theme.Card, ForeColor = Theme.Muted, BorderStyle = BorderStyle.None,
        Font = new Font("Cascadia Mono", 8.5f),
    };
    // Settings
    readonly CheckBox fullscreen = new() { AutoSize = true, ForeColor = Theme.Text, Text = "Start in fullscreen", BackColor = Theme.Card };
    readonly ComboBox windowSize = Combo(), fpsLimit = Combo(), present = Combo(), upscaler = Combo(), overlay = Combo(),
                      population = Combo(), colour = Combo(), antiAliasing = Combo(), dlssOut = Combo(), dlssModel = Combo();
    readonly Button addDlss = Theme.Button("Add nvngx_dlss.dll", glyph: Theme.GlyphFolder);
    readonly Label dlssNote = Theme.Label("", 9.5f, muted: true);
    readonly TrackBar sharpness = new() { Minimum = 0, Maximum = 100, TickFrequency = 10, Width = 280, BackColor = Theme.Card };
    readonly Label sharpnessValue = Theme.Label("80");
    readonly Label settingsNote = Theme.Label("", 9.5f, muted: true);
    readonly Button saveSettings = Theme.Button("Save settings", true, "");
    // Update
    readonly Label updateStatus = Theme.Label("", 10f);
    readonly Button checkUpdate = Theme.Button("Check for updates", true, Theme.GlyphUpdate),
                    installUpdate = Theme.Button("Download and install", glyph: Theme.GlyphSetup);
    readonly TextBox notes = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Theme.Card,
        ForeColor = Theme.Text, BorderStyle = BorderStyle.None, Visible = false,
    };
    Updater.Release? release;

    readonly Panel content = new() { Dock = DockStyle.Fill, BackColor = Theme.Back, Padding = new Padding(32, 18, 32, 22) };
    readonly Dictionary<string, (NavButton Nav, Control Page)> pages = new();
    CancellationTokenSource? cancel;
    readonly bool updated;

    static readonly string SettingsFile = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "KarPur", "launcher.json");

    // Values the game's PC settings use (runtime/cfg.c), with the names shown here.
    static readonly (string Value, string Name)[] Sizes =
        { ("1280x720", "1280 × 720"), ("1600x900", "1600 × 900"), ("1920x1080", "1920 × 1080"), ("2560x1440", "2560 × 1440"),
          ("3840x2160", "3840 × 2160") };
    static readonly (string, string)[] Fps =
        { ("30", "30"), ("40", "40"), ("45", "45"), ("60", "60 (recommended)"), ("90", "90 (experimental)"),
          ("120", "120 (experimental)"), ("144", "144 (experimental)"), ("display", "Display refresh rate (experimental)") };
    static readonly (string, string)[] Presents =
        { ("mailbox", "Smooth, no tearing (mailbox)"), ("fifo", "V-Sync (fifo)"), ("immediate", "Off, lowest latency (immediate)") };
    static readonly (string, string)[] Upscalers = { ("fsr", "AMD FSR"), ("bilinear", "Bilinear") };
    static readonly (string, string)[] AntiAliasings = { ("0", "Game's own"), ("1", "NVIDIA DLAA (RTX)") };
    static readonly (string, string)[] DlssOuts =
        { ("game", "Game resolution (1080p)"), ("window", "Window size (DLSS upscaling)"), ("2560x1440", "2560 x 1440"),
          ("3840x2160", "3840 x 2160 (4K)") };
    static readonly (string, string)[] DlssModels =
        { ("5", "CNN (steady, recommended)"), ("11", "Transformer (sharper, can smear)") };
    static readonly (string, string)[] Overlays = { ("off", "Off"), ("fps", "FPS only"), ("full", "Full") };
    static readonly (string, string)[] Populations =
        { ("switch", "Switch (15, default)"), ("switch2", "Switch 2 (40)"), ("enhanced", "Enhanced (80)"),
          ("extreme", "Extreme (120, can lower the frame rate)") };
    static readonly (string, string)[] Colours = { ("auto", "Automatic (from your game)"), ("violet", "Violet"), ("scarlet", "Scarlet") };

    readonly string startTab;

    public MainForm(bool updated, string tab = "Setup")
    {
        this.updated = updated;
        startTab = tab;
        Text = "KarPur Launcher";
        Font = new Font("Segoe UI", 10f);
        BackColor = Theme.Back;
        ForeColor = Theme.Text;
        MinimumSize = new Size(1000, 680);
        Size = new Size(1080, 720);
        StartPosition = FormStartPosition.CenterScreen;
        try { Icon = Icon.ExtractAssociatedIcon(Environment.ProcessPath!); } catch { }

        Controls.Add(content);
        Controls.Add(Sidebar());
        AddPage("Setup", SetupPage());
        AddPage("Settings", SettingsPage());
        AddPage("Update", UpdatePage());
        AddPage("Feedback", FeedbackPage());

        buildButton.Click += async (_, _) => await Build(repair: false);
        repairButton.Click += async (_, _) => await Build(repair: true);
        cancelButton.Click += (_, _) => cancel?.Cancel();
        cancelButton.Enabled = false;
        playButton.Click += (_, _) => Play();
        details.CheckedChanged += (_, _) => log.Visible = details.Checked;
        targetBox.TextChanged += (_, _) => Refresh_();
        sharpness.ValueChanged += (_, _) => sharpnessValue.Text = sharpness.Value.ToString();
        saveSettings.Click += (_, _) => SaveGameSettings();
        addDlss.Click += (_, _) => AddDlss();
        antiAliasing.SelectedIndexChanged += (_, _) => DlssStatus();
        colour.SelectedIndexChanged += (_, _) => { ApplyColour(); if (!loading) SaveSettings(); };
        checkUpdate.Click += async (_, _) => await CheckUpdate();
        installUpdate.Click += async (_, _) => await InstallUpdate();
        installUpdate.Visible = false;

        HandleCreated += (_, _) => Theme.DarkTitleBar(this);
        Load += async (_, _) =>
        {
            LoadSettings();
            Refresh_();
            Show(pages.ContainsKey(startTab) ? startTab : "Setup");
            if (this.updated && File.Exists(Builder.Exe(targetBox.Text)))
            {
                status.Text = $"Updated to version {Program.Version}. Rebuilding what changed …";
                await Build(repair: false);
            }
        };
        FormClosing += (_, e) =>
        {
            if (cancel != null && MessageBox.Show(this, "A build is running. Stop it and close?", Text, MessageBoxButtons.YesNo,
                    MessageBoxIcon.Warning) == DialogResult.No)
                e.Cancel = true;
            else cancel?.Cancel();
        };
    }

    // ---- layout

    Control Sidebar()
    {
        var side = new Panel { Dock = DockStyle.Left, Width = 224, BackColor = Theme.Panel };
        var logo = new PictureBox { Size = new Size(40, 40), Location = new Point(22, 24), SizeMode = PictureBoxSizeMode.Zoom, BackColor = Theme.Panel };
        try { logo.Image = new Icon(Icon.ExtractAssociatedIcon(Environment.ProcessPath!)!, 48, 48).ToBitmap(); } catch { }
        var title = Theme.Label("KarPur", 17f, style: FontStyle.Bold);
        title.Location = new Point(70, 20);
        var version = Theme.Label($"Version {Program.Version}", 9f, muted: true);
        version.Location = new Point(72, 48);
        side.Controls.Add(logo);
        side.Controls.Add(title);
        side.Controls.Add(version);
        int y = 104;
        foreach (var (name, glyph) in new[] { ("Setup", Theme.GlyphSetup), ("Settings", Theme.GlyphSettings), ("Update", Theme.GlyphUpdate), ("Feedback", "") })
        {
            var b = new NavButton { Text = name, Glyph = glyph, Width = side.Width, Location = new Point(0, y) };
            b.Click += (_, _) => Show(name);
            side.Controls.Add(b);
            pages[name] = (b, new Panel());
            y += 46;
        }
        playButton.Font = new Font("Segoe UI Semibold", 13f);
        playButton.Size = new Size(184, 54);
        ((RoundButton)playButton).Radius = 12;
        playButton.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
        side.Controls.Add(playButton);
        side.Resize += (_, _) => playButton.Location = new Point(20, side.Height - 78);
        return side;
    }

    void AddPage(string name, Control page)
    {
        page.Dock = DockStyle.Fill;
        page.Visible = false;
        content.Controls.Add(page);
        pages[name] = (pages[name].Nav, page);
    }

    void Show(string name)
    {
        foreach (var (key, (nav, page)) in pages)
        {
            page.Visible = key == name;
            nav.Selected = key == name;
        }
        if (name == "Settings") LoadGameSettings();
        if (name == "Feedback") _ = FillFeedback();
    }

    static TableLayoutPanel Grid(int columns) =>
        new() { Dock = DockStyle.Top, AutoSize = true, ColumnCount = columns, BackColor = Theme.Card };

    static Card CardOf(params Control[] children)
    {
        var card = new Card { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink };
        foreach (var c in children.Reverse()) card.Controls.Add(c);
        return card;
    }

    static Control Spacer(int h, Color? back = null) => new Panel { Dock = DockStyle.Top, Height = h, BackColor = back ?? Theme.Back };

    static Control Header(string title, string subtitle)
    {
        var p = new Panel { Dock = DockStyle.Top, Height = 86, BackColor = Theme.Back };
        var t = Theme.Label(title, 22f, style: FontStyle.Bold);
        var s = Theme.Label(subtitle, 9.5f, muted: true);
        s.Location = new Point(3, 50);
        p.Controls.Add(t);
        p.Controls.Add(s);
        return p;
    }

    static ComboBox Combo() => new()
    {
        DropDownStyle = ComboBoxStyle.DropDownList, FlatStyle = FlatStyle.Flat, BackColor = Theme.Field, ForeColor = Theme.Text,
        Width = 320, Font = new Font("Segoe UI", 10f),
    };

    Control SetupPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var grid = Grid(3);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        FileRow(grid, "Base game (.xci)", baseBox, () => PickFile(baseBox, "Base game as XCI|*.xci"));
        FileRow(grid, "Update (.nsp)", updateBox, () => PickFile(updateBox, "Update as NSP|*.nsp"));
        FileRow(grid, "DLC (.nsp, optional)", dlcBox, () => PickFile(dlcBox, "DLC as NSP|*.nsp"));
        FileRow(grid, "prod.keys", keysBox, () => PickFile(keysBox, "Keys|*.keys|All files|*.*"));
        FileRow(grid, "Target folder", targetBox, PickFolder);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        details.Margin = new Padding(6, 10, 0, 0);
        buttons.Controls.AddRange(new Control[] { buildButton, repairButton, cancelButton, details });
        var statusPanel = new Panel { Dock = DockStyle.Top, Height = 30, BackColor = Theme.Card };
        status.Location = new Point(0, 8);
        statusPanel.Controls.Add(status);
        var logPanel = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 4, 0, 0), BackColor = Theme.Back };
        logPanel.Controls.Add(log);

        page.Controls.Add(logPanel);
        page.Controls.Add(Spacer(10));
        page.Controls.Add(CardOf(buttons, Spacer(16, Theme.Card), progress, statusPanel));
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Setup", "Choose your own dumps and keys and a folder for the game. Everything stays on your PC."));
        foreach (Control c in new Control[] { buttons, statusPanel }) c.BackColor = Theme.Card;
        return page;
    }

    static void FileRow(TableLayoutPanel grid, string label, TextBox box, Action browse)
    {
        int row = grid.RowCount++;
        var l = Theme.Label(label);
        l.Margin = new Padding(0, 13, 16, 0);
        grid.Controls.Add(l, 0, row);
        grid.Controls.Add(new Field(box), 1, row);
        var b = Theme.Button("Browse", glyph: Theme.GlyphFolder);
        b.Margin = new Padding(0, 3, 0, 3);
        b.Click += (_, _) => browse();
        grid.Controls.Add(b, 2, row);
    }

    Control SettingsPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var grid = Grid(2);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        Fill(windowSize, Sizes);
        Fill(fpsLimit, Fps);
        Fill(present, Presents);
        Fill(upscaler, Upscalers);
        Fill(antiAliasing, AntiAliasings);
        Fill(dlssOut, DlssOuts);
        Fill(dlssModel, DlssModels);
        Fill(overlay, Overlays);
        Fill(population, Populations);
        Fill(colour, Colours);
        var sharp = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        sharpnessValue.Margin = new Padding(6, 6, 0, 0);
        sharp.Controls.Add(sharpness);
        sharp.Controls.Add(sharpnessValue);
        SettingRow(grid, "Display", fullscreen);
        SettingRow(grid, "Window size", windowSize);
        SettingRow(grid, "FPS limit", fpsLimit);
        SettingRow(grid, "Frame sync", present);
        SettingRow(grid, "Upscaling", upscaler);
        SettingRow(grid, "Sharpness", sharp);
        var aa = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0), WrapContents = false };
        antiAliasing.Margin = new Padding(0, 3, 10, 0);
        addDlss.Margin = new Padding(0);
        aa.Controls.Add(antiAliasing);
        aa.Controls.Add(addDlss);
        SettingRow(grid, "Anti-aliasing", aa);
        SettingRow(grid, "DLSS output", dlssOut);
        SettingRow(grid, "DLSS model", dlssModel);
        dlssNote.MaximumSize = new Size(560, 0);
        dlssNote.AutoSize = true;
        SettingRow(grid, "", dlssNote);
        SettingRow(grid, "Performance overlay", overlay);
        SettingRow(grid, "Wild Pokémon", population);
        var look = Grid(2);
        look.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        look.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        SettingRow(look, "Launcher colour", colour);
        var bottom = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Back };
        settingsNote.Margin = new Padding(6, 10, 0, 0);
        bottom.Controls.Add(saveSettings);
        bottom.Controls.Add(settingsNote);
        page.Controls.Add(bottom);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(look));
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Settings", "Used the next time the game starts. The game's own PC settings page changes the same values."));
        return page;
    }

    static void SettingRow(TableLayoutPanel grid, string label, Control c)
    {
        int row = grid.RowCount++;
        var l = Theme.Label(label);
        l.Margin = new Padding(0, 9, 28, 6);
        l.MinimumSize = new Size(170, 0);
        c.Margin = new Padding(0, 5, 0, 5);
        grid.Controls.Add(l, 0, row);
        grid.Controls.Add(c, 1, row);
    }

    static void Fill(ComboBox c, (string Value, string Name)[] items)
    {
        c.Items.Clear();
        foreach (var (_, name) in items) c.Items.Add(name);
        c.Tag = items;
    }

    static void Select(ComboBox c, string? value, string fallback)
    {
        var items = ((string Value, string Name)[])c.Tag!;
        int i = Array.FindIndex(items, x => x.Value.Equals(value ?? fallback, StringComparison.OrdinalIgnoreCase));
        c.SelectedIndex = i >= 0 ? i : Math.Max(0, Array.FindIndex(items, x => x.Value == fallback));
    }

    static string Value(ComboBox c) => ((string Value, string Name)[])c.Tag! is var items && c.SelectedIndex >= 0
        ? items[c.SelectedIndex].Value : "";

    Control UpdatePage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Theme.Card };
        var installed = Theme.Label($"Installed: version {Program.Version}", 11f);
        installed.Margin = new Padding(0, 0, 0, 14);
        var row = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        row.Controls.Add(checkUpdate);
        row.Controls.Add(installUpdate);
        updateStatus.Margin = new Padding(0, 14, 0, 0);
        top.Controls.Add(installed);
        top.Controls.Add(row);
        top.Controls.Add(updateStatus);
        var notesPanel = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 4, 0, 0), BackColor = Theme.Back };
        notesPanel.Controls.Add(notes);
        page.Controls.Add(notesPanel);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(top));
        page.Controls.Add(Header("Update", "New versions come from the KarPur releases on GitHub. Only what changed is rebuilt."));
        return page;
    }

    // ---- Feedback (Feedback.cs; a GitHub issue the player submits in their browser)

    readonly TextBox fbLauncher = Theme.TextBox(), fbGpu = Theme.TextBox(), fbCpu = Theme.TextBox(), fbRam = Theme.TextBox();
    readonly ComboBox fbGame = Combo(), fbUpdate = Combo(), fbDlc = Combo(), fbWindows = Combo();
    readonly TextBox fbReport = new()
    {
        Multiline = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Theme.Field, ForeColor = Theme.Text,
        BorderStyle = BorderStyle.None, MaxLength = Feedback.ReportLimit, Font = new Font("Segoe UI", 10f),
        PlaceholderText = "How does it run? What went wrong? Where? Logs are welcome.",
    };
    readonly Label fbCount = Theme.Label($"0 / {Feedback.ReportLimit}", 9f, muted: true);
    readonly Label fbNote = Theme.Label("", 9.5f, muted: true);
    readonly Button fbSend = Theme.Button("Open on GitHub", true, ""), fbCopy = Theme.Button("Copy report", glyph: ""),
                    fbLog = Theme.Button("Attach log", glyph: "");
    bool fbDetected;
    static readonly (string, string)[] Games = { ("Violet", "Violet"), ("Scarlet", "Scarlet") };
    static readonly (string, string)[] YesNo = { ("Yes", "Yes"), ("No", "No") };
    static readonly (string, string)[] Windows = { ("Windows 11", "Windows 11"), ("Windows 10", "Windows 10") };

    Control FeedbackPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        Fill(fbGame, Games);
        Fill(fbUpdate, YesNo);
        Fill(fbDlc, YesNo);
        Fill(fbWindows, Windows);
        fbLauncher.ReadOnly = true;
        fbLauncher.Text = Program.Version;
        var grid = Grid(4);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        void Pair(string a, Control ca, string b, Control cb)
        {
            int row = grid.RowCount++;
            foreach (var (label, c, col) in new[] { (a, ca, 0), (b, cb, 2) })
            {
                var l = Theme.Label(label);
                l.Margin = new Padding(col == 0 ? 0 : 18, 12, 14, 0);
                grid.Controls.Add(l, col, row);
                Control box = c is TextBox t ? new Field(t) : c;
                box.Margin = new Padding(0, 4, 0, 4);
                if (c is ComboBox cbx) { cbx.Width = 10; cbx.Dock = DockStyle.Fill; cbx.Margin = new Padding(0, 8, 0, 4); }
                grid.Controls.Add(box, col + 1, row);
            }
        }
        Pair("Launcher version", fbLauncher, "Game", fbGame);
        Pair("Update 4.0.0", fbUpdate, "DLC", fbDlc);
        Pair("GPU", fbGpu, "CPU", fbCpu);
        Pair("RAM (type, size)", fbRam, "Windows", fbWindows);

        var reportCard = new Card { Dock = DockStyle.Fill };
        var reportField = new Field(fbReport) { Dock = DockStyle.Fill, Padding = new Padding(10, 8, 4, 8) };
        var reportHead = new Panel { Dock = DockStyle.Top, Height = 30, BackColor = Theme.Card };
        var rl = Theme.Label("Report");
        reportHead.Controls.Add(rl);
        fbCount.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        reportHead.Controls.Add(fbCount);
        reportHead.Resize += (_, _) => fbCount.Location = new Point(reportHead.Width - fbCount.Width, 2);
        reportCard.Controls.Add(reportField);
        reportCard.Controls.Add(reportHead);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, BackColor = Theme.Back, Padding = new Padding(0, 12, 0, 0) };
        fbNote.Margin = new Padding(6, 10, 0, 0);
        buttons.Controls.AddRange(new Control[] { fbSend, fbCopy, fbLog, fbNote });

        page.Controls.Add(reportCard);
        page.Controls.Add(buttons);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Feedback", "Tell us how KarPur runs on your PC. It opens as a GitHub issue that you check and submit."));

        fbReport.TextChanged += (_, _) => fbCount.Text = $"{fbReport.TextLength} / {Feedback.ReportLimit}";
        fbLog.Click += (_, _) =>
        {
            string tail = Feedback.LogTail(targetBox.Text, Feedback.ReportLimit - fbReport.TextLength - 2);
            if (tail == "") { fbNote.Text = "No game log found (start the game once), or the report is full."; return; }
            fbReport.AppendText((fbReport.TextLength > 0 ? "\r\n\r\n" : "") + tail.Replace("\n", "\r\n"));
            fbNote.Text = "Log attached. Folder names of your user account are left out.";
        };
        fbCopy.Click += (_, _) => { Clipboard.SetText(Feedback.Text(FeedbackFields())); fbNote.Text = "Report copied to the clipboard."; };
        fbSend.Click += (_, _) =>
        {
            if (fbReport.Text.Trim() == "") { fbNote.Text = "Please write a few words in the report first."; return; }
            var f = FeedbackFields();
            string title = $"[Feedback] {f["game"]}, {f["gpu"]}, launcher {f["launcher"]}";
            fbNote.Text = Feedback.Open(f, title)
                ? "Opened in your browser: check it and press Submit new issue (a free GitHub account is needed)."
                : "Opened in your browser. The report was too long for the link: paste it from the clipboard (Ctrl+V).";
        };
        return page;
    }

    Dictionary<string, string> FeedbackFields() => new()
    {
        ["launcher"] = fbLauncher.Text, ["game"] = Value(fbGame), ["update"] = Value(fbUpdate), ["dlc"] = Value(fbDlc),
        ["gpu"] = fbGpu.Text.Trim(), ["cpu"] = fbCpu.Text.Trim(), ["ram"] = fbRam.Text.Trim(), ["windows"] = Value(fbWindows),
        ["report"] = fbReport.Text.Replace("\r\n", "\n").Trim(),
    };

    // Filled in once, the first time the tab opens: the game from the build, the PC from Windows.
    async Task FillFeedback()
    {
        if (fbDetected) return;
        fbDetected = true;
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        Select(fbGame, built && Builder.Game(targetBox.Text) == "scarlet" ? "Scarlet" : "Violet", "Violet");
        Select(fbUpdate, built || updateBox.Text != "" ? "Yes" : "No", "Yes");
        Select(fbDlc, dlcBox.Text != "" ? "Yes" : "No", "No");
        Select(fbWindows, Feedback.WindowsName(), "Windows 11");
        fbNote.Text = "Reading your PC's hardware …";
        var s = await Feedback.Detect();
        if (fbGpu.Text == "") fbGpu.Text = s.Gpu;
        if (fbCpu.Text == "") fbCpu.Text = s.Cpu;
        if (fbRam.Text == "") fbRam.Text = s.Ram;
        fbNote.Text = "Filled in from your PC. Change anything that is wrong.";
    }

    // ---- Setup

    void PickFile(TextBox box, string filter)
    {
        using var d = new OpenFileDialog { Filter = filter, CheckFileExists = true };
        if (File.Exists(box.Text)) d.InitialDirectory = Path.GetDirectoryName(box.Text);
        if (d.ShowDialog(this) == DialogResult.OK) box.Text = d.FileName;
    }

    void PickFolder()
    {
        using var d = new FolderBrowserDialog { Description = "Where should the game go?", UseDescriptionForTitle = true };
        if (Directory.Exists(targetBox.Text)) d.InitialDirectory = targetBox.Text;
        if (d.ShowDialog(this) == DialogResult.OK) targetBox.Text = d.SelectedPath;
    }

    // Messages for what the player can fix before anything runs.
    string? CheckInputs()
    {
        if (!File.Exists(baseBox.Text)) return "Please choose your base game (.xci).";
        if (!baseBox.Text.EndsWith(".xci", StringComparison.OrdinalIgnoreCase))
            return "The base game must be an .xci file. An .nsp does not work as the base game.";
        if (!File.Exists(updateBox.Text) || !updateBox.Text.EndsWith(".nsp", StringComparison.OrdinalIgnoreCase))
            return "Please choose the update (.nsp).";
        if (dlcBox.Text != "" && (!File.Exists(dlcBox.Text) || !dlcBox.Text.EndsWith(".nsp", StringComparison.OrdinalIgnoreCase)))
            return "The DLC file was not found or is not an .nsp. Leave the field empty if you have no DLC.";
        if (!File.Exists(keysBox.Text)) return "Please choose your prod.keys.";
        if (targetBox.Text == "") return "Please choose a target folder.";
        if (targetBox.Text.Contains(' ')) return "The target folder must not contain spaces (for example C:\\Games).";
        return null;
    }

    async Task Build(bool repair)
    {
        string? problem = CheckInputs();
        if (problem != null)
        {
            Show("Setup");
            MessageBox.Show(this, problem, Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        SaveSettings();
        var inputs = new Inputs(baseBox.Text, updateBox.Text, dlcBox.Text, keysBox.Text, targetBox.Text);
        cancel = new CancellationTokenSource();
        SetBusy(true);
        log.Clear();
        status.ForeColor = Theme.Muted;
        var b = new Builder
        {
            Status = s => BeginInvoke(() => status.Text = s),
            Percent = p => BeginInvoke(() => progress.Value = p),
            Log = s => BeginInvoke(() => { if (log.TextLength > 2_000_000) log.Clear(); log.AppendText(s + Environment.NewLine); }),
        };
        string? error;
        try { error = await b.Run(inputs, repair, cancel.Token); }
        catch (OperationCanceledException) { error = "Stopped. The next Build goes on where it stopped."; }
        catch (Exception e) { error = e.Message; }
        cancel = null;
        SetBusy(false);
        Refresh_();
        if (error == null)
        {
            status.Text = "Done! Press Play, or use the shortcut in your target folder.";
            status.ForeColor = Theme.Text;
        }
        else
        {
            status.Text = "Error: " + error;
            status.ForeColor = Theme.Error;
            MessageBox.Show(this, "The build did not work:\n\n" + error + "\n\nThe full log is in " +
                            Path.Combine(Builder.Root(inputs.Target), "launcher.log"), Text, MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
        }
    }

    void SetBusy(bool busy)
    {
        buildButton.Enabled = repairButton.Enabled = checkUpdate.Enabled = installUpdate.Enabled = !busy;
        cancelButton.Enabled = busy;
        playButton.Enabled = !busy && File.Exists(Builder.Exe(targetBox.Text));
        foreach (var t in new[] { baseBox, updateBox, dlcBox, keysBox, targetBox }) t.ReadOnly = busy;
    }

    // Play, Repair and the colour follow what the target folder holds.
    void Refresh_()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        playButton.Enabled = cancel == null && built;
        repairButton.Enabled = cancel == null && built;
        buildButton.Text = built ? "Rebuild" : "Build";
        if (cancel == null && status.ForeColor == Theme.Muted)
            status.Text = built ? "Ready to play. Rebuild or Repair only when something is missing or after a change."
                                : "Choose your files and a target folder, then press Build.";
        ApplyColour();
    }

    void ApplyColour()
    {
        string choice = colour.SelectedIndex >= 0 ? Value(colour) : "auto";
        string game = choice == "auto" ? (targetBox.Text != "" ? Builder.Game(targetBox.Text) : "unknown") : choice;
        Theme.Accent = Theme.AccentFor(game);
        Theme.Recolor(this);
    }

    void Play()
    {
        string exe = Builder.Exe(targetBox.Text);
        if (!File.Exists(exe)) return;
        Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = Builder.Root(targetBox.Text), UseShellExecute = true });
    }

    // ---- Settings (karpur.ini next to karpur.exe)

    void LoadGameSettings()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        var ini = new Ini(built ? Builder.Ini(targetBox.Text) : "");
        fullscreen.Checked = (built ? ini.Get("fullscreen") : null) == "1";
        Select(windowSize, built ? ini.Get("window_size") : null, "1920x1080");
        Select(fpsLimit, built ? ini.Get("fps_limit") : null, "60");
        Select(present, built ? ini.Get("vk_present") : null, "mailbox");
        Select(upscaler, built ? ini.Get("upscaler") : null, "fsr");
        Select(antiAliasing, built ? ini.Get("dlaa") : null, "0");
        Select(dlssOut, built ? ini.Get("dlss_out") : null, "game");
        Select(dlssModel, built ? ini.Get("dlaa_preset") : null, "5");
        Select(overlay, built ? ini.Get("perf_overlay") : null, "off");
        Select(population, built ? ini.Get("population") : null, "switch");
        sharpness.Value = int.TryParse(built ? ini.Get("sharpness") : null, out int s) ? Math.Clamp(s, 0, 100) : 80;
        foreach (Control c in new Control[] { fullscreen, windowSize, fpsLimit, present, upscaler, antiAliasing, addDlss, dlssOut, dlssModel, overlay, population, sharpness, saveSettings })
            c.Enabled = built;
        settingsNote.Text = built ? "" : "Build the game first: the settings belong to its folder.";
        DlssStatus();
    }

    // ---- NVIDIA DLAA: nvngx_dlss.dll is NVIDIA's and not part of KarPur; the player adds their own copy next to
    // karpur.exe (from a game that ships it, or NVIDIA's DLSS SDK). The runtime loads it through the driver.

    string DlssPath() => Path.Combine(Path.GetDirectoryName(Builder.Exe(targetBox.Text))!, "nvngx_dlss.dll");

    void DlssStatus()
    {
        if (targetBox.Text == "" || !File.Exists(Builder.Exe(targetBox.Text))) { dlssNote.Text = ""; return; }
        string dll = DlssPath();
        if (File.Exists(dll))
            dlssNote.Text = $"nvngx_dlss.dll {FileVersionInfo.GetVersionInfo(dll).FileVersion?.Replace(',', '.')} is in place. "
                          + "DLAA smooths edges on NVIDIA RTX cards (about 1 ms per frame); with a DLSS output above "
                          + "1080p, DLSS also sharpens the 3D world to that size (the HUD stays at 1080p).";
        else
            dlssNote.Text = Value(antiAliasing) == "1"
                ? "DLAA needs NVIDIA's nvngx_dlss.dll, which KarPur cannot ship: add your own copy (games with DLSS have it). "
                  + "Without it the game starts with its own anti-aliasing."
                : "NVIDIA DLAA needs an RTX card and NVIDIA's nvngx_dlss.dll (add your own copy).";
    }

    void AddDlss()
    {
        using var dlg = new OpenFileDialog { Title = "Choose nvngx_dlss.dll", Filter = "NVIDIA DLSS|nvngx_dlss.dll" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            // a check against picking the wrong file, not a trust decision: the name in the Authenticode certificate
            // (CreateFromSignedFile reads it without verifying the chain; .NET has no successor for signed files) and the
            // file's own description; the NVIDIA driver loads and checks the DLL itself
            string signer;
#pragma warning disable SYSLIB0057
            try { signer = System.Security.Cryptography.X509Certificates.X509Certificate.CreateFromSignedFile(dlg.FileName).Subject; }
            catch { signer = ""; }
#pragma warning restore SYSLIB0057
            var info = FileVersionInfo.GetVersionInfo(dlg.FileName);
            if (!signer.Contains("NVIDIA Corporation") || !(info.FileDescription ?? "").Contains("DLSS"))
            {
                dlssNote.Text = "That file is not NVIDIA's signed nvngx_dlss.dll.";
                return;
            }
            File.Copy(dlg.FileName, DlssPath(), true);
            Select(antiAliasing, "1", "0");
            DlssStatus();
            settingsNote.Text = "Press Save settings to use DLAA.";
        }
        catch (Exception e) { dlssNote.Text = "Could not copy it: " + e.Message; }
    }

    void SaveGameSettings()
    {
        try
        {
            var ini = new Ini(Builder.Ini(targetBox.Text));
            ini.Set("display", "fullscreen", fullscreen.Checked ? "1" : "0");
            ini.Set("display", "window_size", Value(windowSize));
            ini.Set("frame rate", "fps_limit", Value(fpsLimit));
            ini.Set("display", "vk_present", Value(present));
            ini.Set("display", "upscaler", Value(upscaler));
            ini.Set("display", "dlaa", Value(antiAliasing));
            ini.Set("display", "dlss_out", Value(dlssOut));
            ini.Set("display", "dlaa_preset", Value(dlssModel));
            ini.Set("display", "sharpness", sharpness.Value.ToString());
            ini.Set("display", "perf_overlay", Value(overlay));
            ini.Set("display", "population", Value(population));
            ini.Save();
            settingsNote.Text = "Saved. They apply the next time the game starts.";
        }
        catch (Exception e) { settingsNote.Text = "Could not save: " + e.Message; }
    }

    // ---- Update

    async Task CheckUpdate()
    {
        checkUpdate.Enabled = false;
        updateStatus.Text = "Checking …";
        try
        {
            release = await Updater.Latest(CancellationToken.None);
            if (release == null) updateStatus.Text = "No releases published yet.";
            else if (Updater.Newer(release))
            {
                updateStatus.Text = $"Version {release.Version.ToString(3)} is available.";
                installUpdate.Visible = true;
                notes.Text = release.Notes.Replace("\n", Environment.NewLine);
                notes.Visible = release.Notes.Trim() != "";
            }
            else updateStatus.Text = $"You have the newest version ({Program.Version}).";
        }
        catch (Exception e) { updateStatus.Text = "Could not check for updates: " + e.Message; }
        checkUpdate.Enabled = true;
    }

    async Task InstallUpdate()
    {
        if (release == null) return;
        installUpdate.Enabled = checkUpdate.Enabled = false;
        updateStatus.Text = "Downloading …";
        try
        {
            SaveSettings();
            await Updater.Install(release, CancellationToken.None);
            Close();
        }
        catch (Exception e)
        {
            updateStatus.Text = "The update failed: " + e.Message;
            installUpdate.Enabled = checkUpdate.Enabled = true;
        }
    }

    // ---- remembered choices

    record Saved(string Base, string Update, string Dlc, string Keys, string Target, string? Colour);

    bool loading;   // LoadSettings fills the controls: their change events must not save half-filled settings

    void LoadSettings()
    {
        loading = true;
        Select(colour, "auto", "auto");
        try
        {
            var s = JsonSerializer.Deserialize<Saved>(File.ReadAllText(SettingsFile));
            if (s != null)
            {
                (baseBox.Text, updateBox.Text, dlcBox.Text, keysBox.Text, targetBox.Text) = (s.Base, s.Update, s.Dlc, s.Keys, s.Target);
                Select(colour, s.Colour, "auto");
            }
        }
        catch { }
        loading = false;
        // Started from the target folder (the copy a build puts there): that folder is the target.
        string here = Path.GetDirectoryName(Environment.ProcessPath ?? "") ?? "";
        if (here != "" && File.Exists(Builder.Exe(here))) targetBox.Text = here;
    }

    void SaveSettings()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile)!);
            File.WriteAllText(SettingsFile, JsonSerializer.Serialize(new Saved(baseBox.Text, updateBox.Text, dlcBox.Text,
                                                                                 keysBox.Text, targetBox.Text, Value(colour))));
        }
        catch { }
    }
}
