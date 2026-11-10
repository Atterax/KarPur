using System.ComponentModel;
using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace KarPurLauncher;

// The Home page: a banner in the edition's colours, what the build holds at a glance and the Play button. The banners
// for Violet and Scarlet are pictures the maintainer drew (launcher/assets, embedded); their painted scenes below stay
// as the fallback, and the neutral palette (before a build) keeps its painted dusk.
//   Violet: a night over Paldea seen from the future: an indigo sky, a neon horizon grid, circuit lines and a ringed
//           moon in the second accent.
//   Scarlet: an evening from the past: a wine-red sky turning amber, a low sun with rays and layered mountain ridges.
//   Before a build: the neutral palette's quiet dusk.
class Hero : Panel
{
    static readonly Random Stars = new(7);
    static readonly Dictionary<string, Image?> Banners = new();

    static Image? Banner(string edition)
    {
        if (Banners.TryGetValue(edition, out var img)) return img;
        try
        {
            using var s = typeof(Hero).Assembly.GetManifestResourceStream($"banner-{edition}.jpg");
            img = s != null ? new Bitmap(s) : null;
        }
        catch { img = null; }
        return Banners[edition] = img;
    }
    readonly (float X, float Y, float R, int A)[] stars;

    public Hero()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw |
                 ControlStyles.UserPaint, true);
        BackColor = Theme.Back;
        stars = Enumerable.Range(0, 70).Select(_ => ((float)Stars.NextDouble(), (float)Stars.NextDouble() * 0.6f,
                                                      0.6f + (float)Stars.NextDouble() * 1.4f, 60 + Stars.Next(160))).ToArray();
    }

    protected override void OnPaintBackground(PaintEventArgs e) => PaintScene(e.Graphics);

    // The scene under a child control (buttons, chips): drawn by the child itself, shifted to its place, so its rounded
    // corners show the scene and not a stale or black rectangle (Windows' transparency through nested panels).
    public void PaintBehind(Graphics g, Control child)
    {
        var at = PointToClient(child.Parent!.PointToScreen(child.Location));
        var state = g.Save();
        g.TranslateTransform(-at.X, -at.Y);
        PaintScene(g);
        g.Restore(state);
    }

    void PaintScene(Graphics g)
    {
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Back);
        if (Width < 40 || Height < 40) return;   // a minimised window: GDI+ refuses paths and brushes of no size
        var p = Theme.Current;
        var r = new RectangleF(0, 0, Width - 1, Height - 1);
        using var clip = Theme.Rounded(r, 18);
        g.SetClip(clip);
        var banner = p.Name == "neutral" ? null : Banner(p.Name);
        if (banner != null)
        {
            // cover: fill the banner, keep the picture's aspect, centred
            float scale = Math.Max(r.Width / banner.Width, r.Height / banner.Height);
            float w = banner.Width * scale, h = banner.Height * scale;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.DrawImage(banner, (r.Width - w) / 2, (r.Height - h) / 2, w, h);
        }
        else
        {
            using (var sky = new LinearGradientBrush(r, p.Sky1, p.Sky2, 90f)) g.FillRectangle(sky, r);
            if (p.Name == "scarlet") PaintScarlet(g, r, p);
            else PaintViolet(g, r, p);
        }
        // a dark veil on the left, where the text sits
        using (var veil = new LinearGradientBrush(r, Theme.Alpha(p.Back, 225), Theme.Alpha(p.Back, 0), 0f))
        {
            var blend = new Blend { Positions = new[] { 0f, 0.45f, 1f }, Factors = new[] { 0f, 0.55f, 1f } };
            veil.Blend = blend;
            g.FillRectangle(veil, r);
        }
        g.ResetClip();
        using var border = new Pen(Theme.Alpha(p.Accent, 90));
        g.DrawPath(border, clip);
    }

    void PaintViolet(Graphics g, RectangleF r, Theme.Palette p)
    {
        foreach (var (x, y, rad, a) in stars)
        {
            using var b = new SolidBrush(Color.FromArgb(a, 255, 255, 255));
            g.FillEllipse(b, x * r.Width, y * r.Height, rad, rad);
        }
        // the ringed moon
        float mx = r.Width * 0.78f, my = r.Height * 0.30f, mr = r.Height * 0.16f;
        using (var halo = new GraphicsPath())
        {
            halo.AddEllipse(mx - mr * 2.4f, my - mr * 2.4f, mr * 4.8f, mr * 4.8f);
            using var hb = new PathGradientBrush(halo) { CenterColor = Theme.Alpha(p.Accent, 90), SurroundColors = new[] { Theme.Alpha(p.Accent, 0) } };
            g.FillPath(hb, halo);
        }
        using (var moon = new LinearGradientBrush(new RectangleF(mx - mr, my - mr, mr * 2, mr * 2), Theme.Mix(p.Accent, Color.White, 0.55f), p.Accent, 60f))
            g.FillEllipse(moon, mx - mr, my - mr, mr * 2, mr * 2);
        using (var ring = new Pen(Theme.Alpha(p.Accent2, 200), 2.2f))
            g.DrawEllipse(ring, mx - mr * 1.9f, my - mr * 0.45f, mr * 3.8f, mr * 0.9f);
        // the horizon glow and the neon grid below it
        float hy = r.Height * 0.68f;
        using (var glow = new LinearGradientBrush(new RectangleF(0, hy - 60, r.Width, 120), Theme.Alpha(p.Glow, 0), Theme.Alpha(p.Glow, 110), 90f))
        {
            glow.Blend = new Blend { Positions = new[] { 0f, 0.5f, 1f }, Factors = new[] { 0f, 1f, 0f } };
            g.FillRectangle(glow, 0, hy - 60, r.Width, 120);
        }
        using (var ground = new LinearGradientBrush(new RectangleF(0, hy, r.Width, r.Height - hy + 1), Theme.Mix(p.Sky2, p.Back, 0.4f), p.Back, 90f))
            g.FillRectangle(ground, 0, hy, r.Width, r.Height - hy + 1);
        using var line = new Pen(Theme.Alpha(p.Accent2, 120), 1.2f);
        float vx = r.Width * 0.62f;
        for (int i = -14; i <= 14; i++) g.DrawLine(line, vx, hy, vx + i * r.Width * 0.09f, r.Height + 4);
        for (int i = 1; i <= 7; i++)
        {
            float t = i / 7f, y = hy + (r.Height - hy) * t * t;
            using var l = new Pen(Theme.Alpha(p.Accent2, (int)(40 + 120 * t)), 1.2f);
            g.DrawLine(l, 0, y, r.Width, y);
        }
        using var horizon = new Pen(Theme.Alpha(p.Accent2, 230), 2f);
        g.DrawLine(horizon, 0, hy, r.Width, hy);
        // circuit lines in the sky
        using var trace = new Pen(Theme.Alpha(p.Accent, 110), 1.4f);
        for (int k = 0; k < 4; k++)
        {
            float y0 = r.Height * (0.14f + k * 0.11f), x0 = r.Width * (0.42f + k * 0.04f);
            g.DrawLines(trace, new[] { new PointF(x0, y0), new PointF(x0 + 60, y0), new PointF(x0 + 80, y0 + 20), new PointF(x0 + 150, y0 + 20) });
            using var dot = new SolidBrush(Theme.Alpha(p.Accent2, 220));
            g.FillEllipse(dot, x0 + 147, y0 + 17, 6, 6);
        }
    }

    void PaintScarlet(Graphics g, RectangleF r, Theme.Palette p)
    {
        float sx = r.Width * 0.74f, sy = r.Height * 0.62f, sr = r.Height * 0.2f;
        // rays
        var state = g.Save();
        g.TranslateTransform(sx, sy);
        for (int i = 0; i < 18; i++)
        {
            using var ray = new GraphicsPath();
            ray.AddPolygon(new[] { new PointF(0, 0), new PointF(-14, -r.Width), new PointF(14, -r.Width) });
            using var rb = new SolidBrush(Theme.Alpha(p.Accent2, 18));
            g.FillPath(rb, ray);
            g.RotateTransform(20);
        }
        g.Restore(state);
        using (var halo = new GraphicsPath())
        {
            halo.AddEllipse(sx - sr * 2.6f, sy - sr * 2.6f, sr * 5.2f, sr * 5.2f);
            using var hb = new PathGradientBrush(halo) { CenterColor = Theme.Alpha(p.Accent2, 150), SurroundColors = new[] { Theme.Alpha(p.Accent2, 0) } };
            g.FillPath(hb, halo);
        }
        using (var sun = new LinearGradientBrush(new RectangleF(sx - sr, sy - sr, sr * 2, sr * 2), Theme.Mix(p.Accent2, Color.White, 0.5f), p.Accent, 90f))
            g.FillEllipse(sun, sx - sr, sy - sr, sr * 2, sr * 2);
        // three mountain ridges, darker towards the front
        for (int layer = 0; layer < 3; layer++)
        {
            float baseY = r.Height * (0.66f + layer * 0.1f);
            var pts = new List<PointF> { new(0, r.Height + 2) };
            var rnd = new Random(11 + layer);
            for (float x = 0; x <= r.Width + 40; x += 40 + layer * 10)
                pts.Add(new PointF(x, baseY - (float)rnd.NextDouble() * r.Height * (0.16f - layer * 0.03f)));
            pts.Add(new PointF(r.Width + 2, r.Height + 2));
            using var b = new SolidBrush(Theme.Mix(Theme.Mix(p.Sky2, p.Back, 0.45f + layer * 0.22f), p.Accent, 0.08f));
            g.FillPolygon(b, pts.ToArray());
        }
        // embers
        foreach (var (x, y, rad, a) in stars.Take(40))
        {
            using var b = new SolidBrush(Color.FromArgb(a / 2, p.Accent2));
            g.FillEllipse(b, x * r.Width, (0.15f + y) * r.Height, rad * 1.4f, rad * 1.4f);
        }
    }
}

// A small rounded label on the hero: a glyph and a text on a translucent field.
class Chip : Control
{
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public string Glyph { get; set; } = "";
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public bool Good { get; set; }

    public Chip()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        Font = new Font("Segoe UI Semibold", 9.5f);
        Height = 32;
        Margin = new Padding(0, 0, 8, 0);
    }

    protected override void OnTextChanged(EventArgs e)
    {
        base.OnTextChanged(e);
        Width = TextRenderer.MeasureText(Text, Font).Width + (Glyph != "" ? 52 : 28);
        Invalidate();
    }

    protected override void OnPaintBackground(PaintEventArgs e) { }
    Hero? FindHero() { for (var p = Parent; p != null; p = p.Parent) if (p is Hero h) return h; return null; }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        if (FindHero() is Hero hero) hero.PaintBehind(g, this);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Height / 2f);
        using (var b = new SolidBrush(Theme.Alpha(Theme.Back, 170))) g.FillPath(b, path);
        using (var pen = new Pen(Theme.Alpha(Good ? Theme.Good : Theme.Border, 200))) g.DrawPath(pen, path);
        int x = 14;
        if (Glyph != "")
        {
            using var gf = new Font(Theme.IconFont, 10f);
            TextRenderer.DrawText(g, Glyph, gf, new Rectangle(x, 0, 20, Height), Good ? Theme.Good : Theme.Accent2,
                                  TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            x += 24;
        }
        TextRenderer.DrawText(g, Text, Font, new Rectangle(x, 0, Width - x, Height), Theme.Text, TextFormatFlags.VerticalCenter);
    }
}

// A card with a glyph, a title, a big value and a note: one fact about the build.
class StatCard : Card
{
    public readonly Label Value = Theme.Label("", 15f, style: FontStyle.Bold), Note = Theme.Label("", 9f, muted: true);
    readonly Label title;
    readonly string glyph;

    public StatCard(string glyph, string title)
    {
        this.glyph = glyph;
        this.title = Theme.Label(title.ToUpperInvariant(), 8.5f, muted: true, style: FontStyle.Bold);
        Padding = new Padding(18, 14, 18, 14);
        Height = 112;
        Margin = new Padding(0, 0, 12, 0);
        this.title.Location = new Point(48, 18);
        Value.Location = new Point(16, 44);
        Note.Location = new Point(18, 78);
        Controls.Add(this.title);
        Controls.Add(Value);
        Controls.Add(Note);
        foreach (Control c in Controls) c.BackColor = Theme.Card;
    }

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        base.OnPaintBackground(e);
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var top = new LinearGradientBrush(new RectangleF(0, 0, Width, 3), Theme.Accent, Theme.Accent2, 0f);
        using var bar = Theme.Rounded(new RectangleF(14, 0, Width - 28, 3), 1.5f);
        g.FillPath(top, bar);
        using var gf = new Font(Theme.IconFont, 13f);
        TextRenderer.DrawText(g, glyph, gf, new Rectangle(16, 12, 28, 28), Theme.Accent2, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
    }
}

// One party member: a monogram in the edition's gradient (no game art), the name the game shows, the level, a star when
// shiny.
class TeamCard : Control
{
    public string Nick = "", Level = "";
    public bool Shiny;
    public Image? Icon;   // the Pokemon's icon from the player's own game files (build\game\icons), else the monogram

    public TeamCard()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Size = new Size(124, 132);
        Margin = new Padding(0, 0, 10, 0);
        Font = new Font("Segoe UI Semibold", 10f);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Back);
        using (var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), 12))
        {
            using var b = new SolidBrush(Theme.Card);
            g.FillPath(b, path);
            using var pen = new Pen(Shiny ? Theme.Mix(Theme.Accent2, Theme.Card, 0.2f) : Theme.Border);
            g.DrawPath(pen, path);
        }
        var circle = new RectangleF(Width / 2f - 28, 14, 56, 56);
        if (Icon != null)
        {
            // a soft glow in the edition's colours behind the icon, the icon a little larger than the circle
            using (var halo = new System.Drawing.Drawing2D.GraphicsPath())
            {
                halo.AddEllipse(circle.X - 6, circle.Y - 6, circle.Width + 12, circle.Height + 12);
                using var hb = new PathGradientBrush(halo) { CenterColor = Theme.Alpha(Theme.Accent, 110), SurroundColors = new[] { Theme.Alpha(Theme.Accent2, 0) } };
                g.FillPath(hb, halo);
            }
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.DrawImage(Icon, new RectangleF(Width / 2f - 48, -10, 96, 96));   // the icons carry a margin of their own
        }
        else
        {
            using (var grad = new LinearGradientBrush(circle, Theme.Accent, Theme.Accent2, 45f)) g.FillEllipse(grad, circle);
            string letter = Nick.Length > 0 ? Nick[..1].ToUpperInvariant() : "?";
            using var lf = new Font("Segoe UI", 20f, FontStyle.Bold);
            TextRenderer.DrawText(g, letter, lf, Rectangle.Round(circle), Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }
        if (Shiny)
        {
            using var sf = new Font(Theme.IconFont, 11f);
            TextRenderer.DrawText(g, "", sf, new Rectangle(Width - 28, 6, 22, 22), Theme.Accent2, TextFormatFlags.NoPadding);
        }
        TextRenderer.DrawText(g, Nick, Font, new Rectangle(4, 78, Width - 8, 22), Theme.Text,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.EndEllipsis);
        using var small = new Font("Segoe UI", 9f);
        TextRenderer.DrawText(g, Level, small, new Rectangle(4, 100, Width - 8, 20), Theme.Accent2, TextFormatFlags.HorizontalCenter);
    }
}

partial class MainForm
{
    readonly Hero hero = new() { Dock = DockStyle.Top, Height = 340 };
    readonly Label heroEdition = Theme.Label("", 9.5f, style: FontStyle.Bold), heroTitle = Theme.Label("", 30f, style: FontStyle.Bold),
                   heroText = Theme.Label("", 10.5f, muted: true);
    readonly FlowLayoutPanel heroChips = new() { AutoSize = true, BackColor = Color.Transparent, WrapContents = false };
    readonly Button heroPlay = Theme.Button("Play", true, Theme.GlyphPlay), heroFolder = Theme.Button("Open game folder", glyph: Theme.GlyphFolder),
                    heroSetup = Theme.Button("Go to Setup", true, Theme.GlyphSetup);
    readonly FlowLayoutPanel teamRow = new() { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Back, WrapContents = true };
    readonly Label teamTitle = Theme.Label("YOUR TEAM", 8.5f, muted: true, style: FontStyle.Bold);
    readonly StatCard statGame = new(Theme.GlyphGame, "Game"), statShaders = new(Theme.GlyphShaders, "Shaders"),
                      statSave = new(Theme.GlyphSave, "Last save");

    Control HomePage()
    {
        var page = new Panel { BackColor = Theme.Back };
        foreach (var l in new[] { heroEdition, heroTitle, heroText }) l.BackColor = Color.Transparent;
        // one column, top to bottom: edition, title, text, chips, buttons (sizes follow the system's text scale)
        var column = new FlowLayoutPanel
        {
            AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Color.Transparent,
            Location = new Point(36, 30),
        };
        heroEdition.Margin = new Padding(4, 0, 0, 0);
        heroTitle.Margin = new Padding(0, 2, 0, 0);
        heroText.Margin = new Padding(4, 0, 0, 16);
        heroText.MaximumSize = new Size(500, 0);
        heroChips.Margin = new Padding(4, 0, 0, 22);
        heroPlay.Font = new Font("Segoe UI Semibold", 14f);
        heroPlay.Size = new Size(190, 56);
        ((RoundButton)heroPlay).Radius = 14;
        heroSetup.Font = heroPlay.Font;
        heroSetup.Size = new Size(230, 56);
        ((RoundButton)heroSetup).Radius = 14;
        heroFolder.Height = 56;
        ((RoundButton)heroFolder).Radius = 14;
        foreach (var b in new[] { heroPlay, heroSetup, heroFolder }) { b.BackColor = Color.Transparent; b.Margin = new Padding(0, 0, 12, 0); }
        // the buttons sit on the hero itself, below the column: a button inside a transparent panel inside another one
        // showed black corners
        column.Controls.AddRange(new Control[] { heroEdition, heroTitle, heroText, heroChips });
        hero.Controls.Add(column);
        hero.Controls.AddRange(new Control[] { heroPlay, heroSetup, heroFolder });
        void PlaceButtons()
        {
            int x = column.Left + 4, y = column.Bottom;
            foreach (var b in new[] { heroPlay, heroSetup, heroFolder })
            {
                b.Location = new Point(x, y);
                if (b.Visible) x += b.Width + 12;
            }
            hero.Height = Math.Max(300, y + heroPlay.Height + 34);
        }
        column.SizeChanged += (_, _) => PlaceButtons();
        foreach (var b in new[] { heroPlay, heroSetup, heroFolder }) b.VisibleChanged += (_, _) => PlaceButtons();

        var stats = new TableLayoutPanel { Dock = DockStyle.Top, Height = 112, ColumnCount = 3, BackColor = Theme.Back, Margin = new Padding(0) };
        for (int i = 0; i < 3; i++) stats.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 33.3f));
        int col = 0;
        foreach (var s in new[] { statGame, statShaders, statSave })
        {
            s.Dock = DockStyle.Fill;
            s.Margin = new Padding(col == 0 ? 0 : 6, 0, col == 2 ? 0 : 6, 0);
            stats.Controls.Add(s, col++, 0);
        }

        teamTitle.Dock = DockStyle.Top;
        teamTitle.Padding = new Padding(2, 0, 0, 8);
        page.Controls.Add(teamRow);
        page.Controls.Add(teamTitle);
        page.Controls.Add(Spacer(18));
        page.Controls.Add(stats);
        page.Controls.Add(Spacer(16));
        page.Controls.Add(hero);
        heroPlay.Click += (_, _) => Play();
        heroSetup.Click += (_, _) => Show("Setup");
        heroFolder.Click += (_, _) =>
        {
            string root = Builder.Root(targetBox.Text);
            if (Directory.Exists(root)) Process.Start(new ProcessStartInfo(root) { UseShellExecute = true });
        };
        return page;
    }

    // What the Home page shows: the edition, whether the game is built, the shaders and the newest save.
    void RefreshHome()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        string game = targetBox.Text != "" ? Builder.Game(targetBox.Text) : "unknown";
        string name = game == "scarlet" ? "Pokémon Scarlet" : game == "violet" ? "Pokémon Violet" : "Pokémon Scarlet & Violet";
        heroEdition.Text = name.ToUpperInvariant();
        heroEdition.ForeColor = Theme.Accent2;
        heroTitle.Text = cancel != null ? "Building …" : built ? "Ready to play" : "Let's get started";
        heroText.Text = built ? "Your game files are set up and the game's code is compiled for this PC."
                              : "Choose your own game dump, update and keys in Setup. KarPur builds the PC version from them, on your PC.";
        heroPlay.Visible = built;
        heroFolder.Visible = built;
        heroSetup.Visible = !built;
        heroPlay.Enabled = built && cancel == null;

        heroChips.Controls.Clear();
        if (built)
        {
            bool dlc = dlcBox.Text != "";
            heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphCheck, Good = true, Text = "Version 4.0.0" + (dlc ? " + DLC" : "") });
            int shaders = PreparedShaders();
            if (shaders > 0) heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphShaders, Text = $"{shaders:N0} shaders ready" });
            var save = NewestSave();
            if (save != null) heroChips.Controls.Add(new Chip { Glyph = Theme.GlyphSave, Text = $"Save from {save.Value:yyyy-MM-dd, HH:mm}" });
        }

        statGame.Value.Text = built ? name.Replace("Pokémon ", "") : "Not built";
        statGame.Note.Text = built ? "Update 4.0.0" + (dlcBox.Text != "" ? " · DLC" : "") : "Set it up in Setup";
        int n = built ? PreparedShaders() : 0;
        statShaders.Value.Text = n > 0 ? $"{n:N0}" : "–";
        statShaders.Note.Text = n > 0 ? "prepared during the build: no stutter" : "prepared at the end of a build";
        var last = built ? NewestSave() : null;
        statSave.Value.Text = last != null ? $"{last.Value:dd MMM, HH:mm}" : "–";
        statSave.Note.Text = last != null ? "manage it in Saves" : "no save yet";
        RefreshTeam(built);
        hero.Invalidate(true);
    }

    // <profile>	eam.json, which the game writes when it saves (runtime/team.c): name, level, shiny per party member
    void RefreshTeam(bool built)
    {
        teamRow.Controls.Clear();
        var team = new List<TeamCard>();
        try
        {
            string? profile = ProfileDir();
            string file = profile != null ? Path.Combine(profile, "team.json") : "";
            if (built && File.Exists(file))
            {
                var icons = IconCatalog();
                using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(file));
                foreach (var m in doc.RootElement.GetProperty("team").EnumerateArray())
                {
                    int species = m.GetProperty("species").GetInt32(), form = m.TryGetProperty("form", out var fm) ? fm.GetInt32() : 0;
                    team.Add(new TeamCard
                    {
                        Nick = m.GetProperty("nickname").GetString() ?? "", Level = m.GetProperty("level").GetInt32() is int lv && lv > 0 ? $"Lv. {lv}" : "",
                        Shiny = m.TryGetProperty("shiny", out var sh) && sh.GetBoolean(),
                        Icon = LoadIcon(icons, species, form),
                    });
                }
            }
        }
        catch { team.Clear(); }
        teamTitle.Visible = teamRow.Visible = team.Count > 0;
        teamRow.Controls.AddRange(team.ToArray());
    }

    // build\game\icons\catalog.txt (the shader census: "species form folder" per line) and the folder's PNG
    Dictionary<(int, int), string> IconCatalog()
    {
        var map = new Dictionary<(int, int), string>();
        try
        {
            string dir = Path.Combine(Builder.Root(targetBox.Text), "build", "game", "icons");
            foreach (var line in File.ReadAllLines(Path.Combine(dir, "catalog.txt")))
            {
                var p = line.Split(' ');
                if (p.Length == 3 && int.TryParse(p[0], out int sp) && int.TryParse(p[1], out int fo))
                    map.TryAdd((sp, fo), Path.Combine(dir, p[2] + ".png"));
            }
        }
        catch { }
        return map;
    }

    static Image? LoadIcon(Dictionary<(int, int), string> icons, int species, int form)
    {
        if (!icons.TryGetValue((species, form), out var file) && !icons.TryGetValue((species, 0), out file)) return null;
        try
        {
            if (!File.Exists(file)) return null;
            using var img = Image.FromFile(file);
            return new Bitmap(img);   // a copy: the file stays free for the next build
        }
        catch { return null; }
    }

    int PreparedShaders()
    {
        try
        {
            var f = new FileInfo(Path.Combine(Builder.Root(targetBox.Text), "build", "game", "shader-prepared.bin"));
            return f.Exists && f.Length > 16 ? (int)((f.Length - 16) / 36) : 0;
        }
        catch { return 0; }
    }
}
