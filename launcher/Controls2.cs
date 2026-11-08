using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace KarPurLauncher;

// The launcher's own dropdown, scrollbars and key buttons, in the look of its rounded buttons and fields.
static class DarkScroll
{
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr hwnd, string? app, string? idList);

    // Windows' own dark scrollbars (Windows 10 1809 and later: dark; Windows 11: thin, rounded, widening on hover). Older
    // systems keep their light ones.
    public static void Apply(Control c)
    {
        if (c.IsHandleCreated) SetWindowTheme(c.Handle, "DarkMode_Explorer", null);
        else c.HandleCreated += (_, _) => SetWindowTheme(c.Handle, "DarkMode_Explorer", null);
    }
}

// A ComboBox drawn as a rounded field with a chevron (closed) and a dark list with the accent on the chosen item (open).
// It stays a ComboBox, so Items, SelectedIndex and SelectedIndexChanged work as before.
class RoundCombo : ComboBox
{
    bool hover;
    const int WM_PAINT = 0x000F;

    public RoundCombo()
    {
        DrawMode = DrawMode.OwnerDrawFixed;
        DropDownStyle = ComboBoxStyle.DropDownList;
        FlatStyle = FlatStyle.Flat;
        ItemHeight = 26;
        BackColor = Theme.Field;
        ForeColor = Theme.Text;
        Font = new Font("Segoe UI", 10f);
        Cursor = Cursors.Hand;
        MaxDropDownItems = 14;
    }

    protected override void OnHandleCreated(EventArgs e) { base.OnHandleCreated(e); DarkScroll.Apply(this); }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }
    protected override void OnSelectedIndexChanged(EventArgs e) { Invalidate(); base.OnSelectedIndexChanged(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
    protected override void OnDropDownClosed(EventArgs e) { Invalidate(); base.OnDropDownClosed(e); }

    // the open list's rows
    protected override void OnDrawItem(DrawItemEventArgs e)
    {
        if (e.Index < 0) return;
        if ((e.State & DrawItemState.ComboBoxEdit) != 0) return;   // the closed face is painted in WndProc
        bool sel = (e.State & DrawItemState.Selected) != 0;
        using (var b = new SolidBrush(sel ? Theme.Mix(Theme.Accent, Theme.Field, 0.35f) : Theme.Field)) e.Graphics.FillRectangle(b, e.Bounds);
        TextRenderer.DrawText(e.Graphics, GetItemText(Items[e.Index]), Font, new Rectangle(e.Bounds.X + 8, e.Bounds.Y, e.Bounds.Width - 8, e.Bounds.Height),
                              Theme.Text, TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
    }

    protected override void WndProc(ref Message m)
    {
        base.WndProc(ref m);
        if (m.Msg == WM_PAINT) { using var g = Graphics.FromHwnd(Handle); PaintFace(g); }
    }

    void PaintFace(Graphics g)
    {
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new RectangleF(0, 0, Width - 1, Height - 1);
        g.Clear(Parent?.BackColor ?? Theme.Card);
        using (var path = Theme.Rounded(r, 8))
        {
            using var b = new SolidBrush(!Enabled ? Theme.Mix(Theme.Field, Theme.Back, 0.4f) : hover ? Theme.Mix(Theme.Field, Theme.Border, 0.5f) : Theme.Field);
            g.FillPath(b, path);
            using var pen = new Pen(Focused || DroppedDown ? Theme.Accent : Theme.Border);
            g.DrawPath(pen, path);
        }
        string text = SelectedIndex >= 0 ? GetItemText(Items[SelectedIndex]) ?? "" : "";
        TextRenderer.DrawText(g, text, Font, new Rectangle(10, 0, Width - 40, Height), Enabled ? Theme.Text : Theme.Muted,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
        // the chevron
        float cx = Width - 18, cy = Height / 2f;
        using var cp = new Pen(Enabled ? Theme.Muted : Theme.Border, 1.8f) { StartCap = LineCap.Round, EndCap = LineCap.Round };
        g.DrawLines(cp, new[] { new PointF(cx - 5, cy - 2.5f), new PointF(cx, cy + 2.5f), new PointF(cx + 5, cy - 2.5f) });
    }
}

// A key binding: shows the key; a click waits for the next key or mouse button ("Press a key ..."), Esc cancels; the
// right mouse button offers the default and none. Key holds runtime/input_map.c's key name, "default" or "none".
class KeyButton : RoundButton
{
    string key = "default";
    bool waiting;
    readonly string defaultKey;
    public event EventHandler? KeyChosen;

    public KeyButton(string defaultKey)
    {
        this.defaultKey = defaultKey;
        Width = 150;
        Height = 34;
        Font = new Font("Segoe UI Semibold", 9.5f);
        Margin = new Padding(0, 4, 0, 4);
        var menu = new ContextMenuStrip { Renderer = new DarkMenuRenderer(), ShowImageMargin = false, BackColor = Theme.Field, ForeColor = Theme.Text };
        menu.Items.Add($"Default ({Pretty(defaultKey)})", null, (_, _) => Set("default"));
        menu.Items.Add("None", null, (_, _) => Set("none"));
        ContextMenuStrip = menu;
        UpdateText();
    }

    public string Key { get => key; set { key = value; waiting = false; UpdateText(); } }

    void Set(string k) { Key = k; KeyChosen?.Invoke(this, EventArgs.Empty); }

    void UpdateText()
    {
        Primary = waiting;
        Text = waiting ? "Press a key ..." : key == "default" ? Pretty(defaultKey) : key == "none" ? "-" : Pretty(key);
        Width = 150;
        Invalidate();
    }

    protected override void OnClick(EventArgs e)
    {
        base.OnClick(e);
        if (waiting) return;
        waiting = true;
        UpdateText();
        Focus();
    }

    protected override void OnLostFocus(EventArgs e) { if (waiting) { waiting = false; UpdateText(); } base.OnLostFocus(e); }

    // the next mouse button while waiting (the click that started waiting has been handled by then)
    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (waiting)
        {
            string? m = e.Button switch
            {
                MouseButtons.Left => "mouse_l", MouseButtons.Right => "mouse_r", MouseButtons.Middle => "mouse_m",
                MouseButtons.XButton1 => "mouse_x1", MouseButtons.XButton2 => "mouse_x2", _ => null,
            };
            if (m != null) { Set(m); return; }
        }
        base.OnMouseDown(e);
    }

    protected override bool IsInputKey(Keys keyData) => waiting || base.IsInputKey(keyData);

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (!waiting) return base.ProcessCmdKey(ref msg, keyData);
        Keys k = keyData & Keys.KeyCode;
        if (k == Keys.Escape) { waiting = false; UpdateText(); return true; }
        string? name = KeyName(k);
        if (name != null) Set(name);
        return true;
    }

    [DllImport("user32.dll")] static extern short GetKeyState(int vk);
    static bool Down(int vk) => (GetKeyState(vk) & 0x8000) != 0;

    // .NET's key to input_map.c's name (positions of a US layout, as the game reads them); null for keys it has none for
    static string? KeyName(Keys k)
    {
        if (k >= Keys.A && k <= Keys.Z) return ((char)('a' + (k - Keys.A))).ToString();
        if (k >= Keys.D0 && k <= Keys.D9) return ((char)('0' + (k - Keys.D0))).ToString();
        if (k >= Keys.NumPad0 && k <= Keys.NumPad9) return "num" + (k - Keys.NumPad0);
        return k switch
        {
            Keys.Space => "space", Keys.Tab => "tab", Keys.Up => "up", Keys.Down => "down", Keys.Left => "left", Keys.Right => "right",
            Keys.ShiftKey or Keys.LShiftKey or Keys.RShiftKey => Down(0xA1) ? "rshift" : "lshift",
            Keys.ControlKey or Keys.LControlKey or Keys.RControlKey => Down(0xA3) ? "rctrl" : "lctrl",
            Keys.Menu or Keys.LMenu => "lalt",
            Keys.Enter => "enter", Keys.Back => "backspace", Keys.CapsLock => "capslock", Keys.Insert => "insert",
            Keys.Delete => "delete", Keys.Home => "home", Keys.End => "end", Keys.PageUp => "pageup", Keys.PageDown => "pagedown",
            Keys.F2 => "f2", Keys.F3 => "f3", Keys.F4 => "f4", Keys.F5 => "f5", Keys.F6 => "f6", Keys.F7 => "f7", Keys.F9 => "f9",
            Keys.F12 => "f12", Keys.Add => "num_add", Keys.Subtract => "num_sub", Keys.Multiply => "num_mul",
            Keys.Divide => "num_div", Keys.Decimal => "num_decimal", Keys.OemMinus => "minus", Keys.Oemplus => "equals",
            Keys.OemOpenBrackets => "lbracket", Keys.OemCloseBrackets => "rbracket", Keys.OemSemicolon => "semicolon",
            Keys.OemQuotes => "apostrophe", Keys.Oemcomma => "comma", Keys.OemPeriod => "period", Keys.OemQuestion => "slash",
            Keys.OemBackslash or Keys.OemPipe => "backslash", Keys.Oemtilde => "grave",
            _ => null,
        };
    }

    // the words on the button
    public static string Pretty(string k) => k switch
    {
        "mouse_l" => "Left mouse", "mouse_r" => "Right mouse", "mouse_m" => "Middle mouse", "mouse_x1" => "Mouse 4",
        "mouse_x2" => "Mouse 5", "lshift" => "Left Shift", "rshift" => "Right Shift", "lctrl" => "Left Ctrl",
        "rctrl" => "Right Ctrl", "lalt" => "Alt", "space" => "Space", "tab" => "Tab", "enter" => "Enter",
        "backspace" => "Backspace", "capslock" => "Caps Lock", "up" => "Up", "down" => "Down", "left" => "Left",
        "right" => "Right", "pageup" => "Page Up", "pagedown" => "Page Down",
        _ when k.StartsWith("num") && k.Length == 4 => "Num " + k[3],
        _ when k.StartsWith("num_") => "Num " + k[4..],
        _ when k.Length == 1 => k.ToUpperInvariant(),
        _ when k.StartsWith('f') && k.Length <= 3 => k.ToUpperInvariant(),
        _ => char.ToUpperInvariant(k[0]) + k[1..],
    };
}

// Dark menus (the key buttons' right-click menu)
class DarkMenuRenderer : ToolStripProfessionalRenderer
{
    public DarkMenuRenderer() : base(new DarkColors()) { RoundedEdges = true; }
    protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e) { e.TextColor = Theme.Text; base.OnRenderItemText(e); }
    class DarkColors : ProfessionalColorTable
    {
        public override Color MenuItemSelected => Theme.Mix(Theme.Accent, Theme.Field, 0.35f);
        public override Color MenuItemBorder => Theme.Accent;
        public override Color MenuBorder => Theme.Border;
        public override Color ToolStripDropDownBackground => Theme.Field;
        public override Color ImageMarginGradientBegin => Theme.Field;
        public override Color ImageMarginGradientMiddle => Theme.Field;
        public override Color ImageMarginGradientEnd => Theme.Field;
    }
}

// A controller binding: shows the button; a click waits for the next button pressed on an XInput controller (Xbox pads,
// and pads Steam presents as one); the right mouse button lists every button (any controller, the paddles too). Pad
// holds runtime/input_map.c's pad name ("south" ... "rpaddle2"), "default" or "none". Positions, not letters: "south" is
// the bottom face button on every controller (A on Xbox, B on Switch, Cross on PlayStation).
class PadButton : RoundButton
{
    public static readonly (string Name, string Words)[] Pads =
    {
        ("south", "Bottom face button"), ("east", "Right face button"), ("west", "Left face button"), ("north", "Top face button"),
        ("l", "L / LB"), ("r", "R / RB"), ("zl", "ZL / LT"), ("zr", "ZR / RT"), ("lstick", "Left stick press"),
        ("rstick", "Right stick press"), ("start", "+ / Start"), ("back", "- / Back"), ("dpad_up", "D-pad up"),
        ("dpad_down", "D-pad down"), ("dpad_left", "D-pad left"), ("dpad_right", "D-pad right"), ("lpaddle1", "Left paddle 1"),
        ("rpaddle1", "Right paddle 1"), ("lpaddle2", "Left paddle 2"), ("rpaddle2", "Right paddle 2"),
    };

    string pad = "default";
    bool waiting;
    ushort startButtons;
    byte startLt, startRt;
    readonly System.Windows.Forms.Timer poll = new() { Interval = 25 };
    public event EventHandler? PadChosen;

    public PadButton()
    {
        Width = 170;
        Height = 34;
        Font = new Font("Segoe UI Semibold", 9.5f);
        Margin = new Padding(8, 4, 0, 4);
        var menu = new ContextMenuStrip { Renderer = new DarkMenuRenderer(), ShowImageMargin = false, BackColor = Theme.Field, ForeColor = Theme.Text };
        menu.Items.Add("Default", null, (_, _) => Set("default"));
        menu.Items.Add("None", null, (_, _) => Set("none"));
        menu.Items.Add(new ToolStripSeparator());
        foreach (var (name, words) in Pads) menu.Items.Add(words, null, (_, _) => Set(name));
        ContextMenuStrip = menu;
        poll.Tick += (_, _) => Poll();
        UpdateText();
    }

    public string Pad { get => pad; set { pad = value; Stop(); } }

    void Set(string p) { pad = p; Stop(); PadChosen?.Invoke(this, EventArgs.Empty); }

    void Stop() { waiting = false; poll.Stop(); UpdateText(); }

    void UpdateText()
    {
        Primary = waiting;
        Text = waiting ? "Press a button ..." : pad == "default" ? "Default" : pad == "none" ? "-" : Words(pad);
        Width = 170;
        Invalidate();
    }

    public static string Words(string p) => Pads.FirstOrDefault(x => x.Name == p).Words ?? p;

    protected override void OnClick(EventArgs e)
    {
        base.OnClick(e);
        if (waiting) { Stop(); return; }   // a second click cancels
        if (!Read(out var s)) { Text = "No Xbox-type pad: right-click"; Invalidate(); return; }
        startButtons = s.buttons; startLt = s.lt; startRt = s.rt;
        waiting = true;
        UpdateText();
        Focus();
        poll.Start();
    }

    protected override void OnLostFocus(EventArgs e) { if (waiting) Stop(); base.OnLostFocus(e); }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (waiting && (keyData & Keys.KeyCode) == Keys.Escape) { Stop(); return true; }
        return base.ProcessCmdKey(ref msg, keyData);
    }

    void Poll()
    {
        if (!waiting || !Read(out var s)) return;
        ushort fresh = (ushort)(s.buttons & ~startButtons);
        startButtons &= s.buttons;   // released buttons may count again
        (ushort bit, string name)[] map =
        {
            (0x1000, "south"), (0x2000, "east"), (0x4000, "west"), (0x8000, "north"), (0x0100, "l"), (0x0200, "r"),
            (0x0040, "lstick"), (0x0080, "rstick"), (0x0010, "start"), (0x0020, "back"), (0x0001, "dpad_up"),
            (0x0002, "dpad_down"), (0x0004, "dpad_left"), (0x0008, "dpad_right"),
        };
        foreach (var (bit, name) in map)
            if ((fresh & bit) != 0) { Set(name); return; }
        if (s.lt > 128 && startLt <= 128) { Set("zl"); return; }
        if (s.rt > 128 && startRt <= 128) { Set("zr"); return; }
        if (s.lt <= 128) startLt = 0;
        if (s.rt <= 128) startRt = 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct XState { public uint packet; public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] static extern int XInputGetState(int user, out XState s);

    // the first connected XInput controller
    static bool Read(out XState s)
    {
        s = default;
        try
        {
            for (int i = 0; i < 4; i++)
                if (XInputGetState(i, out s) == 0) return true;
        }
        catch (DllNotFoundException) { }
        return false;
    }
}
