namespace KarPurLauncher;

// The Controls tab: the [input] settings of karpur.ini (runtime/cfg.c, runtime/input_map.c), the same values the game's
// own Controls page (F1 / Esc) changes. Gamepad buttons keep their layout ("pad:default"); here the keyboard and mouse
// key of every action is chosen, or the default, or none.
partial class MainForm
{
    // the actions in input_map.c's order, their words, and the default key (input_map.c DEFAULT_KEYS; the first of two)
    static readonly (string Action, string Name, string Default)[] Actions =
    {
        ("LSTICK_UP", "Move forward", "w"), ("LSTICK_LEFT", "Move left", "a"), ("LSTICK_DOWN", "Move back", "s"),
        ("LSTICK_RIGHT", "Move right", "d"), ("RSTICK_UP", "Camera up", "up"), ("RSTICK_LEFT", "Camera left", "left"),
        ("RSTICK_DOWN", "Camera down", "down"), ("RSTICK_RIGHT", "Camera right", "right"), ("A", "A (confirm, talk)", "e"),
        ("B", "B (back, run)", "lshift"), ("X", "X (menu)", "space"), ("Y", "Y", "mouse_l"), ("L", "L", "q"), ("R", "R", "r"),
        ("ZL", "ZL (aim)", "mouse_r"), ("ZR", "ZR (throw)", "f"), ("LCLICK", "Left stick press", "lctrl"),
        ("RCLICK", "Right stick press", "c"), ("LEFT", "D-pad left", "1"), ("UP", "D-pad up", "2"),
        ("RIGHT", "D-pad right", "3"), ("DOWN", "D-pad down", "4"), ("PLUS", "+ (start)", "tab"), ("MINUS", "- (select)", "m"),
    };

    // input_map.c key_names, in that order
    static readonly string[] KeyNames =
    {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
        "y", "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "space", "tab", "lshift", "lctrl", "up", "down", "left",
        "right", "mouse_l", "mouse_r", "mouse_m", "mouse_x1", "mouse_x2", "enter", "backspace", "rshift", "rctrl", "lalt",
        "capslock", "insert", "delete", "home", "end", "pageup", "pagedown", "f2", "f3", "f4", "f5", "f6", "f7", "f9", "f12",
        "num0", "num1", "num2", "num3", "num4", "num5", "num6", "num7", "num8", "num9", "num_add", "num_sub", "num_mul",
        "num_div", "num_decimal", "minus", "equals", "lbracket", "rbracket", "semicolon", "apostrophe", "comma", "period",
        "slash", "backslash", "grave",
    };

    static readonly (string, string)[] PadLayouts =
        { ("auto", "Automatic (from the controller)"), ("nintendo", "Nintendo (A on the right)"), ("xbox", "Xbox (A at the bottom)"),
          ("xbox_prompts", "Nintendo positions, Xbox button names") };

    readonly ComboBox padLayout = Combo();
    readonly TrackBar deadzone = new() { Minimum = 0, Maximum = 50, TickFrequency = 5, Width = 280, BackColor = Theme.Card };
    readonly TrackBar mouseSens = new() { Minimum = 1, Maximum = 50, TickFrequency = 5, Width = 280, BackColor = Theme.Card };
    readonly TrackBar rumble = new() { Minimum = 0, Maximum = 300, TickFrequency = 25, SmallChange = 10, LargeChange = 25, Width = 280, BackColor = Theme.Card };
    readonly Label deadzoneValue = Theme.Label(""), mouseSensValue = Theme.Label(""), rumbleValue = Theme.Label("");
    readonly CheckBox motion = new() { AutoSize = true, ForeColor = Theme.Text, Text = "Motion controls (gyro)", BackColor = Theme.Card };
    readonly CheckBox rumbleTriggers = new() { AutoSize = true, ForeColor = Theme.Text, Text = "Rumble in the triggers too", BackColor = Theme.Card };
    readonly KeyButton[] keyButtons = new KeyButton[Actions.Length];
    readonly Button saveControls = Theme.Button("Save controls", true, "");
    readonly Button defaultKeys = Theme.Button("Default keys", glyph: "");
    readonly Label controlsNote = Theme.Label("", 9.5f, muted: true);

    static Control Slider(TrackBar t, Label value)
    {
        var p = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, Margin = new Padding(0) };
        value.Margin = new Padding(6, 6, 0, 0);
        p.Controls.Add(t);
        p.Controls.Add(value);
        return p;
    }

    Control ControlsPage()
    {
        var page = new Panel { BackColor = Theme.Back };
        var grid = Grid(2);
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        Fill(padLayout, PadLayouts);
        padLayout.Width = 360;
        SettingRow(grid, "Controller layout", padLayout);
        SettingRow(grid, "Stick deadzone", Slider(deadzone, deadzoneValue));
        SettingRow(grid, "Mouse sensitivity", Slider(mouseSens, mouseSensValue));
        SettingRow(grid, "Rumble strength", Slider(rumble, rumbleValue));
        SettingRow(grid, "", rumbleTriggers);
        SettingRow(grid, "", motion);
        deadzone.ValueChanged += (_, _) => deadzoneValue.Text = $"{deadzone.Value} %";
        mouseSens.ValueChanged += (_, _) => mouseSensValue.Text = (mouseSens.Value / 10.0).ToString("0.0");
        rumble.ValueChanged += (_, _) => rumbleValue.Text = $"{rumble.Value} %";

        // the keys in four groups, two side by side: label and key button per row
        var keys = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, BackColor = Theme.Card };
        keys.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        keys.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        (string Title, string[] Actions)[] groups =
        {
            ("Move", new[] { "LSTICK_UP", "LSTICK_LEFT", "LSTICK_DOWN", "LSTICK_RIGHT" }),
            ("Camera", new[] { "RSTICK_UP", "RSTICK_LEFT", "RSTICK_DOWN", "RSTICK_RIGHT" }),
            ("Buttons", new[] { "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "LCLICK", "RCLICK" }),
            ("D-pad and menu", new[] { "UP", "LEFT", "DOWN", "RIGHT", "PLUS", "MINUS" }),
        };
        for (int i = 0; i < Actions.Length; i++)
        {
            int idx = i;
            keyButtons[i] = new KeyButton(Actions[i].Default);
            keyButtons[i].KeyChosen += (_, _) => controlsNote.Text = "Press Save controls to keep it.";
        }
        Control Group((string Title, string[] Actions) g)
        {
            var t = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, BackColor = Theme.Card, Dock = DockStyle.Top, Margin = new Padding(0, 0, 24, 12) };
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            var title = Theme.Label(g.Title, 10.5f, style: FontStyle.Bold);
            title.Margin = new Padding(0, 4, 0, 6);
            t.Controls.Add(title, 0, 0);
            t.SetColumnSpan(title, 2);
            int row = 1;
            foreach (string a in g.Actions)
            {
                int i = Array.FindIndex(Actions, x => x.Action == a);
                var l = Theme.Label(Actions[i].Name);
                l.Margin = new Padding(0, 11, 16, 0);
                l.MinimumSize = new Size(150, 0);
                t.Controls.Add(l, 0, row);
                t.Controls.Add(keyButtons[i], 1, row);
                row++;
            }
            return t;
        }
        keys.Controls.Add(Group(groups[0]), 0, 0);
        keys.Controls.Add(Group(groups[1]), 1, 0);
        keys.Controls.Add(Group(groups[2]), 0, 1);
        keys.Controls.Add(Group(groups[3]), 1, 1);
        var keysTitle = Theme.Label("Keyboard and mouse", 12f, style: FontStyle.Bold);
        keysTitle.Dock = DockStyle.Top;
        keysTitle.Padding = new Padding(0, 0, 0, 4);
        var keysHint = Theme.Label("Click a key, then press the key or mouse button you want. Esc cancels; right-click for Default or None.", 9.5f, muted: true);
        keysHint.Dock = DockStyle.Top;
        keysHint.Padding = new Padding(0, 0, 0, 10);

        var bottom = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, BackColor = Theme.Back };
        controlsNote.Margin = new Padding(6, 10, 0, 0);
        bottom.Controls.Add(saveControls);
        bottom.Controls.Add(defaultKeys);
        bottom.Controls.Add(controlsNote);
        saveControls.Click += (_, _) => SaveControls();
        defaultKeys.Click += (_, _) => { foreach (var b in keyButtons) b.Key = "default"; controlsNote.Text = "Press Save controls to keep it."; };

        page.Controls.Add(bottom);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(keys));
        page.Controls.Add(keysHint);
        page.Controls.Add(keysTitle);
        page.Controls.Add(Spacer(14));
        page.Controls.Add(CardOf(grid));
        page.Controls.Add(Header("Controls", "Gamepad, keyboard and mouse. In the game, F1 or Esc opens the same settings."));
        return page;
    }

    // The keyboard key of one bind_<action> value: "key:<name>" (the first), "default" for key:default or no value,
    // "none" when the value names no key
    static string KeyOf(string? bind)
    {
        if (string.IsNullOrWhiteSpace(bind)) return "default";
        foreach (var part in bind.Split(','))
        {
            string p = part.Trim();
            if (p.StartsWith("key:", StringComparison.OrdinalIgnoreCase)) return p[4..].Trim().ToLowerInvariant();
        }
        return "none";
    }

    void LoadControls()
    {
        bool built = targetBox.Text != "" && File.Exists(Builder.Exe(targetBox.Text));
        var ini = new Ini(built ? Builder.Ini(targetBox.Text) : "");
        Select(padLayout, built ? ini.Get("pad_layout") : null, "auto");
        deadzone.Value = Math.Clamp(int.TryParse(built ? ini.Get("stick_deadzone") : null, out int dz) ? dz : 8, 0, 50);
        double ms = double.TryParse(built ? ini.Get("mouse_sens") : null, System.Globalization.NumberStyles.Float,
                                    System.Globalization.CultureInfo.InvariantCulture, out double m) ? m : 1.0;
        mouseSens.Value = Math.Clamp((int)Math.Round(ms * 10), 1, 50);
        rumble.Value = Math.Clamp(int.TryParse(built ? ini.Get("rumble_strength") : null, out int rs) ? rs : 130, 0, 300);
        rumbleTriggers.Checked = (built ? ini.Get("rumble_triggers") : null) == "1";
        motion.Checked = (built ? ini.Get("motion") : null) != "0";
        deadzoneValue.Text = $"{deadzone.Value} %";
        mouseSensValue.Text = (mouseSens.Value / 10.0).ToString("0.0");
        rumbleValue.Text = $"{rumble.Value} %";
        for (int i = 0; i < Actions.Length; i++)
            keyButtons[i].Key = built ? KeyOf(ini.Get("bind_" + Actions[i].Action.ToLowerInvariant())) : "default";
        foreach (Control c in new Control[] { padLayout, deadzone, mouseSens, rumble, rumbleTriggers, motion, saveControls, defaultKeys })
            c.Enabled = built;
        foreach (var b in keyButtons) b.Enabled = built;
        controlsNote.Text = built ? "" : "Build the game first: the settings belong to its folder.";
    }

    void SaveControls()
    {
        try
        {
            var ini = new Ini(Builder.Ini(targetBox.Text));
            ini.Set("input", "pad_layout", Value(padLayout));
            ini.Set("input", "stick_deadzone", deadzone.Value.ToString());
            ini.Set("input", "mouse_sens", (mouseSens.Value / 10.0).ToString("0.0", System.Globalization.CultureInfo.InvariantCulture));
            ini.Set("input", "rumble_strength", rumble.Value.ToString());
            ini.Set("input", "rumble_triggers", rumbleTriggers.Checked ? "1" : "0");
            ini.Set("input", "motion", motion.Checked ? "1" : "0");
            for (int i = 0; i < Actions.Length; i++)
            {
                string k = keyButtons[i].Key;
                // the gamepad keeps its own buttons; the stick directions take keys only
                bool stick = Actions[i].Action.Contains("STICK_");
                string pad = stick ? "" : ", pad:default";
                string v = k == "default" ? "key:default" + pad : k == "none" ? (stick ? "none" : "pad:default") : "key:" + k + pad;
                ini.Set("input", "bind_" + Actions[i].Action.ToLowerInvariant(), v);
            }
            ini.Save();
            controlsNote.Text = "Saved. The game takes them at its next start (or at once from its own Controls page).";
        }
        catch (Exception e) { controlsNote.Text = "Could not save: " + e.Message; }
    }
}
