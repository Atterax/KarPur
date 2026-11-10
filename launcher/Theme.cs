using System.ComponentModel;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace KarPurLauncher;

// The launcher's dark look: rounded buttons, fields and cards, glyph icons from the Windows icon font. The accent follows
// the game: violet for Violet, red for Scarlet (build/setup_info.json after a build, or the player's choice).
static class Theme
{
    // An edition's palette: page, sidebar, card, field and border colours, text, and two accents (the second for
    // gradients and highlights). Violet: night indigo with violet and a neon cyan (the future); Scarlet: warm wine with
    // scarlet and amber (the past); neutral before a build.
    public record Palette(string Name, Color Back, Color Panel, Color Card, Field_ Fields, Color Text, Color Muted,
                          Color Accent, Color Accent2, Color Sky1, Color Sky2, Color Glow);
    public record Field_(Color Field, Color Border);

    public static readonly Palette VioletPalette = new("violet",
        Color.FromArgb(15, 13, 28), Color.FromArgb(22, 19, 42), Color.FromArgb(30, 26, 55),
        new(Color.FromArgb(42, 37, 74), Color.FromArgb(70, 60, 116)), Color.FromArgb(240, 237, 255), Color.FromArgb(160, 152, 196),
        Color.FromArgb(151, 102, 255), Color.FromArgb(0, 214, 232), Color.FromArgb(12, 10, 40), Color.FromArgb(62, 24, 110),
        Color.FromArgb(0, 214, 232));
    public static readonly Palette ScarletPalette = new("scarlet",
        Color.FromArgb(26, 13, 15), Color.FromArgb(38, 19, 22), Color.FromArgb(50, 25, 29),
        new(Color.FromArgb(68, 36, 40), Color.FromArgb(108, 58, 62)), Color.FromArgb(255, 240, 236), Color.FromArgb(198, 158, 156),
        Color.FromArgb(238, 72, 72), Color.FromArgb(255, 176, 64), Color.FromArgb(46, 10, 18), Color.FromArgb(170, 52, 34),
        Color.FromArgb(255, 176, 64));
    public static readonly Palette NeutralPalette = new("neutral",
        Color.FromArgb(19, 19, 25), Color.FromArgb(28, 28, 36), Color.FromArgb(33, 33, 43),
        new(Color.FromArgb(44, 44, 56), Color.FromArgb(62, 62, 78)), Color.FromArgb(236, 236, 244), Color.FromArgb(150, 150, 168),
        Color.FromArgb(139, 92, 246), Color.FromArgb(229, 72, 77), Color.FromArgb(18, 18, 30), Color.FromArgb(48, 36, 70),
        Color.FromArgb(139, 92, 246));

    public static Palette Current = NeutralPalette;
    public static Color Back = NeutralPalette.Back, Panel = NeutralPalette.Panel, Card = NeutralPalette.Card,
                        Field = NeutralPalette.Fields.Field, Border = NeutralPalette.Fields.Border, Text = NeutralPalette.Text,
                        Muted = NeutralPalette.Muted, Accent = NeutralPalette.Accent, Accent2 = NeutralPalette.Accent2;
    public static readonly Color Error = Color.FromArgb(255, 120, 120);
    public static readonly Color Good = Color.FromArgb(74, 222, 128);
    public static readonly Color Violet = VioletPalette.Accent;
    public static readonly Color Scarlet = ScarletPalette.Accent;

    public static Palette PaletteFor(string game) => game == "scarlet" ? ScarletPalette : game == "violet" ? VioletPalette : NeutralPalette;
    public static Color AccentFor(string game) => PaletteFor(game).Accent;
    public static Color Mix(Color a, Color b, float t) =>
        Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
    public static Color Alpha(Color c, int a) => Color.FromArgb(Math.Clamp(a, 0, 255), c);

    // Switches the palette: every control whose back or fore colour is one of the old palette's takes the new one
    // (controls are built with Theme.Card and friends), custom-painted ones read the statics when they paint.
    public static void SetPalette(Palette p, Control root)
    {
        if (p == Current) { root.Invalidate(true); return; }
        var map = new Dictionary<int, Color>
        {
            [Back.ToArgb()] = p.Back, [Panel.ToArgb()] = p.Panel, [Card.ToArgb()] = p.Card, [Field.ToArgb()] = p.Fields.Field,
            [Border.ToArgb()] = p.Fields.Border, [Text.ToArgb()] = p.Text, [Muted.ToArgb()] = p.Muted,
        };
        void Walk(Control c)
        {
            if (map.TryGetValue(c.BackColor.ToArgb(), out var nb) && c.BackColor.A == 255) c.BackColor = nb;
            if (map.TryGetValue(c.ForeColor.ToArgb(), out var nf)) c.ForeColor = nf;
            foreach (Control k in c.Controls) Walk(k);
        }
        Walk(root);
        Current = p;
        (Back, Panel, Card, Field, Border, Text, Muted, Accent, Accent2) =
            (p.Back, p.Panel, p.Card, p.Fields.Field, p.Fields.Border, p.Text, p.Muted, p.Accent, p.Accent2);
        root.Invalidate(true);
    }

    // Segoe Fluent Icons (Windows 11), else Segoe MDL2 Assets (Windows 10): the same code points for these glyphs.
    public static readonly string IconFont = FontFamily.Families.Any(f => f.Name == "Segoe Fluent Icons")
        ? "Segoe Fluent Icons" : "Segoe MDL2 Assets";
    public const string GlyphSetup = "", GlyphSettings = "", GlyphUpdate = "", GlyphPlay = "",
                        GlyphFolder = "", GlyphRepair = "", GlyphBuild = "", GlyphStop = "",
                        GlyphHome = "", GlyphSave = "", GlyphGame = "", GlyphShaders = "",
                        GlyphCheck = "", GlyphFeedback = "", GlyphExport = "", GlyphImport = "",
                        GlyphHistory = "";

    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

    // A dark title bar (Windows 10 2004 and later) and rounded window corners (Windows 11); older systems keep theirs.
    public static void DarkTitleBar(Form f)
    {
        int on = 1;
        if (DwmSetWindowAttribute(f.Handle, 20, ref on, sizeof(int)) != 0) DwmSetWindowAttribute(f.Handle, 19, ref on, sizeof(int));
        int round = 2;
        DwmSetWindowAttribute(f.Handle, 33, ref round, sizeof(int));
    }

    public static GraphicsPath Rounded(RectangleF r, float radius)
    {
        var p = new GraphicsPath();
        float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
        if (d <= 1 || r.Width <= 1 || r.Height <= 1)
        {
            // no room for arcs (a control of no size while the window is minimised): GDI+ throws on an arc of size 0
            p.AddRectangle(new RectangleF(r.X, r.Y, Math.Max(r.Width, 1), Math.Max(r.Height, 1)));
            return p;
        }
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    public static Button Button(string text, bool primary = false, string glyph = "") =>
        new RoundButton { Text = text, Primary = primary, Glyph = glyph };


    public static TextBox TextBox() => new()
    {
        BackColor = Field, ForeColor = Text, BorderStyle = BorderStyle.None, Dock = DockStyle.Fill, Font = new Font("Segoe UI", 10f),
    };

    public static Label Label(string text, float size = 10f, bool muted = false, FontStyle style = FontStyle.Regular) => new()
    {
        Text = text, AutoSize = true, ForeColor = muted ? Muted : Text, Font = new Font("Segoe UI", size, style),
        BackColor = Color.Transparent,
    };
}

// A rounded, flat button: the accent when primary, else a field colour; lighter under the mouse, dim when disabled.
class RoundButton : Button
{
    bool hover;
    public bool Primary { get; set; }
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public string Glyph { get; set; } = "";
    public int Radius { get; set; } = 8;

    public RoundButton()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.SupportsTransparentBackColor, true);
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Cursor = Cursors.Hand;
        Font = new Font("Segoe UI Semibold", 10f);
        Height = 38;
        Margin = new Padding(0, 0, 10, 0);
        AutoSize = false;
    }

    protected override void OnTextChanged(EventArgs e) { base.OnTextChanged(e); FitWidth(); }
    protected override void OnFontChanged(EventArgs e) { base.OnFontChanged(e); FitWidth(); }
    void FitWidth()
    {
        if (Dock != DockStyle.None || Anchor.HasFlag(AnchorStyles.Right) && Anchor.HasFlag(AnchorStyles.Left)) return;
        int glyph = Glyph != "" ? 26 : 0;
        Width = Math.Max(96, TextRenderer.MeasureText(Text, Font).Width + 36 + glyph);
    }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
    protected override void OnPaintBackground(PaintEventArgs e) { if (BackColor.A == 255 || FindHero() == null) base.OnPaintBackground(e); }
    Hero? FindHero() { for (var p = Parent; p != null; p = p.Parent) if (p is Hero h) return h; return null; }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        if (BackColor.A == 255) g.Clear(Parent?.BackColor ?? Theme.Back);
        else if (FindHero() is Hero hero) hero.PaintBehind(g, this);   // on the hero: its scene behind the rounded corners
        Color fill = !Enabled ? Theme.Mix(Theme.Field, Theme.Back, 0.4f)
            : Primary ? (hover ? Theme.Mix(Theme.Accent, Color.White, 0.15f) : Theme.Accent)
            : hover ? Theme.Border : Theme.Field;
        using (var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Radius))
        {
            if (Primary && Enabled)
            {
                using var grad = new LinearGradientBrush(ClientRectangle, Theme.Mix(fill, Color.White, 0.1f), Theme.Mix(fill, Theme.Accent2, 0.35f), 20f);
                g.FillPath(grad, path);
            }
            else
            {
                using var b = new SolidBrush(fill);
                g.FillPath(b, path);
            }
        }
        Color fore = Enabled ? Theme.Text : Theme.Muted;
        int glyphW = Glyph != "" ? (int)(Font.Size * 2.2f) : 0;
        var textSize = TextRenderer.MeasureText(Text, Font);
        int x = (Width - textSize.Width - glyphW) / 2;
        if (Glyph != "")
        {
            using var gf = new Font(Theme.IconFont, Font.Size * 0.95f);
            TextRenderer.DrawText(g, Glyph, gf, new Rectangle(x, 0, glyphW, Height), fore,
                                  TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.NoPadding);
        }
        TextRenderer.DrawText(g, Text, Font, new Rectangle(x + glyphW, 0, textSize.Width + 2, Height), fore,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.Left);
    }
}

// The launcher's mark in the sidebar: a rounded square in the accent of the chosen edition (a lighter and a darker
// shade of it, nothing of the other accent) with a white "K".
class LogoMark : Control
{
    public LogoMark()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
        g.Clear(Parent?.BackColor ?? Theme.Panel);
        var r = new RectangleF(0, 0, Width - 1, Height - 1);
        using var path = Theme.Rounded(r, Width * 0.24f);
        using (var b = new LinearGradientBrush(r, Theme.Mix(Theme.Accent, Color.White, 0.22f), Theme.Mix(Theme.Accent, Color.Black, 0.25f), 45f))
            g.FillPath(b, path);
        using var f = new Font("Segoe UI", Height * 0.5f, FontStyle.Bold, GraphicsUnit.Pixel);
        TextRenderer.DrawText(g, "K", f, Rectangle.Round(r), Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
    }
}

// A sidebar entry: glyph and name, a rounded highlight and an accent pill when selected.
class NavButton : Control
{
    bool hover;
    public string Glyph = "";
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public bool Selected { get => selected; set { selected = value; Invalidate(); } }
    bool selected;

    public NavButton()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Cursor = Cursors.Hand;
        Height = 44;
        Font = new Font("Segoe UI", 10.5f);
    }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Theme.Panel);
        if (selected || hover)
        {
            using var path = Theme.Rounded(new RectangleF(10, 3, Width - 21, Height - 7), 8);
            using var b = new SolidBrush(selected ? Theme.Field : Theme.Mix(Theme.Panel, Theme.Field, 0.5f));
            g.FillPath(b, path);
        }
        if (selected)
        {
            using var pill = Theme.Rounded(new RectangleF(10, 13, 4, Height - 26), 2);
            using var b = new SolidBrush(Theme.Accent);
            g.FillPath(b, pill);
        }
        Color fore = selected ? Theme.Text : Theme.Muted;
        using var gf = new Font(Theme.IconFont, 12f);
        TextRenderer.DrawText(g, Glyph, gf, new Rectangle(26, 0, 28, Height), selected ? Theme.Accent : fore,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.NoPadding);
        TextRenderer.DrawText(g, Text, Font, new Rectangle(58, 0, Width - 60, Height), fore,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.Left);
    }
}

// A rounded card in a slightly lighter colour than the page; its children sit inside the padding.
class Card : Panel
{
    public Card()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        BackColor = Theme.Card;
        Padding = new Padding(20, 16, 20, 16);
        Margin = new Padding(0, 0, 0, 14);
    }
    protected override void OnPaintBackground(PaintEventArgs e)
    {
        e.Graphics.Clear(Parent?.BackColor ?? Theme.Back);
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        using var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), 12);
        using var b = new SolidBrush(Theme.Card);
        e.Graphics.FillPath(b, path);
    }
}

// A text box inside a rounded field that shows the accent while it has the focus.
class Field : Panel
{
    public readonly TextBox Box;
    public Field(TextBox box)
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        Box = box;
        BackColor = Theme.Field;
        Height = 36;
        Padding = new Padding(10, 9, 10, 6);
        Dock = DockStyle.Fill;
        Margin = new Padding(0, 4, 10, 4);
        box.Dock = DockStyle.Fill;
        Controls.Add(box);
        box.GotFocus += (_, _) => Invalidate();
        box.LostFocus += (_, _) => Invalidate();
    }
    protected override void OnPaintBackground(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Parent?.BackColor ?? Theme.Card);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), 8);
        using var b = new SolidBrush(Theme.Field);
        g.FillPath(b, path);
        using var pen = new Pen(Box.Focused ? Theme.Accent : Theme.Border);
        g.DrawPath(pen, path);
    }
}

// A rounded progress bar in the accent colour (the system one ignores colours under visual styles).
class Bar : Control
{
    int value;
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public int Value { get => value; set { this.value = Math.Clamp(value, 0, 100); Invalidate(); } }
    public Bar()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Height = 8;
        Dock = DockStyle.Top;
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Card);
        using var track = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Height / 2f);
        using var tb = new SolidBrush(Theme.Field);
        g.FillPath(tb, track);
        if (value <= 0) return;
        float w = Math.Max(Height, (Width - 1) * value / 100f);
        using var fill = Theme.Rounded(new RectangleF(0, 0, w, Height - 1), Height / 2f);
        using var fb = new LinearGradientBrush(new RectangleF(0, 0, Math.Max(1, w), Height), Theme.Accent,
                                               Theme.Mix(Theme.Accent, Color.White, 0.25f), 0f);
        g.FillPath(fb, fill);
    }
}
