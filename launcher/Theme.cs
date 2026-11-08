using System.ComponentModel;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace KarPurLauncher;

// The launcher's dark look: rounded buttons, fields and cards, glyph icons from the Windows icon font. The accent follows
// the game: violet for Violet, red for Scarlet (build/setup_info.json after a build, or the player's choice).
static class Theme
{
    public static readonly Color Back = Color.FromArgb(19, 19, 25);
    public static readonly Color Panel = Color.FromArgb(28, 28, 36);
    public static readonly Color Card = Color.FromArgb(33, 33, 43);
    public static readonly Color Field = Color.FromArgb(44, 44, 56);
    public static readonly Color Border = Color.FromArgb(62, 62, 78);
    public static readonly Color Text = Color.FromArgb(236, 236, 244);
    public static readonly Color Muted = Color.FromArgb(150, 150, 168);
    public static readonly Color Error = Color.FromArgb(255, 120, 120);
    public static readonly Color Violet = Color.FromArgb(139, 92, 246);
    public static readonly Color Scarlet = Color.FromArgb(229, 72, 77);
    public static Color Accent = Violet;

    public static Color AccentFor(string game) => game == "scarlet" ? Scarlet : Violet;
    public static Color Mix(Color a, Color b, float t) =>
        Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));

    // Segoe Fluent Icons (Windows 11), else Segoe MDL2 Assets (Windows 10): the same code points for these glyphs.
    public static readonly string IconFont = FontFamily.Families.Any(f => f.Name == "Segoe Fluent Icons")
        ? "Segoe Fluent Icons" : "Segoe MDL2 Assets";
    public const string GlyphSetup = "", GlyphSettings = "", GlyphUpdate = "", GlyphPlay = "",
                        GlyphFolder = "", GlyphRepair = "", GlyphBuild = "", GlyphStop = "";

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
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    public static Button Button(string text, bool primary = false, string glyph = "") =>
        new RoundButton { Text = text, Primary = primary, Glyph = glyph };

    public static void Recolor(Control root) => root.Invalidate(true);   // everything paints with the current accent

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

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Parent?.BackColor ?? Theme.Back);
        Color fill = !Enabled ? Theme.Mix(Theme.Field, Theme.Back, 0.4f)
            : Primary ? (hover ? Theme.Mix(Theme.Accent, Color.White, 0.15f) : Theme.Accent)
            : hover ? Theme.Border : Theme.Field;
        using (var path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Radius))
        {
            if (Primary && Enabled)
            {
                using var grad = new LinearGradientBrush(ClientRectangle, Theme.Mix(fill, Color.White, 0.08f), fill, 90f);
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
