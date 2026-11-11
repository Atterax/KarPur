using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace KarPurLauncher;

// The launcher with a controller (issue #30): in the sidebar the left stick or the D-pad moves between the tabs and
// Play, A opens the tab (or presses Play), Start presses Play from anywhere; in a page the stick moves from control to
// control, left / right change a dropdown, a slider or a switch, A presses or toggles, B goes back to the sidebar (or
// closes an open dropdown). Only while the launcher's window has the focus; the first connected XInput pad.
static class Pad
{
    [StructLayout(LayoutKind.Sequential)]
    public struct State { public uint packet; public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] static extern int XInputGetState(int user, out State s);

    public static bool Read(out State s)
    {
        s = default;
        try
        {
            for (int i = 0; i < 4; i++)
                if (XInputGetState(i, out s) == 0) return true;
        }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
        return false;
    }

    public const ushort Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008, Start = 0x0010,
                        L = 0x0100, R = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000;

    // the D-pad and the left stick past half way, as D-pad bits
    public static ushort Directions(in State s)
    {
        ushort d = (ushort)(s.buttons & 0x000F);
        const short dead = 16000;
        if (s.ly > dead) d |= Up;
        if (s.ly < -dead) d |= Down;
        if (s.lx < -dead) d |= Left;
        if (s.lx > dead) d |= Right;
        return d;
    }

    public static bool Active;   // a pad moved the focus: buttons and switches draw a focus ring
}

// Observe real mouse clicks and scrolling before native child controls consume them.
// Merely repainting or hovering after pad navigation must not change the input mode.
sealed class PadMouseFilter(Form owner, Action useMouse) : IMessageFilter
{
    public bool PreFilterMessage(ref Message m)
    {
        if (m.Msg is 0x0201 or 0x0204 or 0x0207 or 0x020B or 0x020A or 0x020E &&
            Control.FromChildHandle(m.HWnd)?.FindForm() == owner) useMouse();
        return false;
    }
}

partial class MainForm
{
    readonly System.Windows.Forms.Timer padTimer = new() { Interval = 30 };
    int padIndex;            // the sidebar entry the pad is on: the tabs in order, then Play
    bool padInPage;          // the pad works inside the page
    ushort padHeld;          // buttons and directions held at the last poll
    bool padArmed;           // wait for neutral after focus loss or reconnect; a held Start must not launch a game
    long padRepeatAt;        // when a held direction repeats
    readonly List<string> padTabs = new();
    readonly BackHint padBack = new() { Visible = false, Size = new Size(180, 36) };
    readonly Dictionary<string, Control> padLastFocus = new();
    ContextMenuStrip? padMenu;
    PadMouseFilter? padMouse;

    void UseMouse()
    {
        if (!Pad.Active) return;
        Pad.Active = false;
        padHeld = 0;
        padArmed = false;
        foreach (var (nav, _) in pages.Values) nav.PadCursor = false;
        padBack.Visible = false;
        Invalidate(true);
    }

    void StartPad()
    {
        padTabs.Clear();
        padTabs.AddRange(pages.Keys);
        padTimer.Tick += (_, _) => PollPad();
        padTimer.Start();
        padMouse = new PadMouseFilter(this, UseMouse);
        Application.AddMessageFilter(padMouse);
        FormClosed += (_, _) =>
        {
            Application.RemoveMessageFilter(padMouse);
            padTimer.Dispose(); teamWatch?.Dispose(); Pad.Active = false;
        };
    }

    void PollPad()
    {
        if (ActiveForm != this || !Pad.Read(out var s)) { padHeld = 0; padArmed = false; return; }
        ushort now = (ushort)(Pad.Directions(s) | (s.buttons & (Pad.A | Pad.B | Pad.Start | Pad.L | Pad.R | Pad.X)));
        if (!padArmed) { padHeld = now; padArmed = now == 0; return; }
        ushort pressed = (ushort)(now & ~padHeld);
        long t = Environment.TickCount64;
        // a held direction repeats: first after 400 ms, then every 130 ms
        ushort dirs = (ushort)(now & 0x000F);
        if (dirs != 0 && (dirs & padHeld) == dirs && t >= padRepeatAt) { pressed |= dirs; padRepeatAt = t + 130; }
        else if ((pressed & 0x000F) != 0) padRepeatAt = t + 400;
        padHeld = now;
        // A binding editor owns the pad until it has its answer. Navigation must not steal B, Start or the D-pad
        // from it, nor move the focus and cancel capture before PadButton's own poll can read the new button.
        var focus = PadFocus();
        if (focus is PadButton { Capturing: true }) return;
        if (focus is KeyButton { Capturing: true } key)
        {
            if ((pressed & Pad.B) != 0) key.CancelCapture();
            return;
        }
        if (pressed == 0) return;
        if (!Pad.Active) { Pad.Active = true; PadMarkSidebar(); Invalidate(true); }

        if (padMenu is { Visible: true }) { PadInMenu(pressed); return; }
        if (padMenu != null) padMenu = null;
        if ((pressed & (Pad.L | Pad.R)) != 0)
        {
            Show(padTabs[(padIndex + ((pressed & Pad.R) != 0 ? 1 : -1) + padTabs.Count) % padTabs.Count]);
            return;
        }

        if ((pressed & Pad.Start) != 0 && playButton.Enabled && cancel == null) { Play(); return; }
        if (padInPage) PadInPage(pressed);
        else PadInSidebar(pressed);
    }

    // ---- the sidebar
    void PadInSidebar(ushort pressed)
    {
        int count = padTabs.Count + 1;   // + Play
        if ((pressed & Pad.Up) != 0) padIndex = (padIndex + count - 1) % count;
        if ((pressed & Pad.Down) != 0) padIndex = (padIndex + 1) % count;
        if ((pressed & (Pad.Up | Pad.Down)) != 0)
        {
            if (padIndex < padTabs.Count) Show(padTabs[padIndex]);   // the page follows the cursor
            PadMarkSidebar();
        }
        if ((pressed & Pad.A) != 0)
        {
            if (padIndex == padTabs.Count) { if (playButton.Enabled) playButton.PerformClick(); return; }
            Show(padTabs[padIndex]);
            var page = pages[padTabs[padIndex]].Page;
            padInPage = true;
            PadMarkSidebar();
            var choices = PadTargets(page);
            var first = padLastFocus.TryGetValue(padTabs[padIndex], out var last) && choices.Contains(last) ? last : choices.FirstOrDefault();
            if (first == null) { padInPage = false; PadMarkSidebar(); }
            else PadSelect(first);
        }
    }

    // the cursor in the sidebar: the tab's highlight, or Play's focus ring
    void PadMarkSidebar()
    {
        foreach (var (key, (nav, _)) in pages) nav.PadCursor = Pad.Active && !padInPage && padIndex < padTabs.Count && padTabs[padIndex] == key;
        if (!padInPage && padIndex == padTabs.Count) playButton.Focus();
        else if (!padInPage) ActiveControl = null;
        playButton.Invalidate();
        padBack.Visible = Pad.Active && padInPage;
    }

    static IEnumerable<Control> Descendants(Control parent)
    {
        foreach (Control c in parent.Controls)
        {
            yield return c;
            foreach (var child in Descendants(c)) yield return child;
        }
    }

    Control? PadFocus() => Descendants(this).FirstOrDefault(c => c.Focused);

    // Order by what is drawn, not Controls' insertion/Z order (top-docked pages are added bottom to top).
    static List<Control> PadTargets(Control page) => Descendants(page)
        .Where(c => c.Visible && c.Enabled && c.CanSelect && c.TabStop &&
                    c is Button or ComboBox or TrackBar or CheckBox or Toggle or TextBox &&
                    c is not TextBox { ReadOnly: true })
        .OrderBy(c => c.PointToScreen(Point.Empty).Y).ThenBy(c => c.PointToScreen(Point.Empty).X).ToList();

    void PadSelect(Control c)
    {
        c.Focus();
        padLastFocus[padTabs[padIndex]] = c;
        PadScrollTo(c);
        content.Invalidate(true);
    }

    void PadMove(Control? from, int dx, int dy)
    {
        var choices = PadTargets(pages[padTabs[padIndex]].Page);
        if (from == null || !choices.Contains(from)) { if (choices.Count > 0) PadSelect(choices[0]); return; }
        Rectangle here = from.RectangleToScreen(from.ClientRectangle);
        double hx = here.Left + here.Width / 2.0, hy = here.Top + here.Height / 2.0;
        var next = choices.Where(c => c != from).Select(c =>
        {
            Rectangle r = c.RectangleToScreen(c.ClientRectangle);
            double x = r.Left + r.Width / 2.0 - hx, y = r.Top + r.Height / 2.0 - hy;
            double along = dx != 0 ? x * dx : y * dy, across = dx != 0 ? Math.Abs(y) : Math.Abs(x);
            // Prefer the same visual row/column, then the nearest row in the requested direction.
            bool lane = dx != 0 ? r.Top < here.Bottom && r.Bottom > here.Top : r.Left < here.Right && r.Right > here.Left;
            return (Control: c, Valid: along > 4 && (dx == 0 || lane), Score: dx == 0 ? along * 10000 + across : along + across * 2);
        }).Where(x => x.Valid).OrderBy(x => x.Score).FirstOrDefault();
        if (next.Control != null) PadSelect(next.Control);
        else if (dx < 0) { padInPage = false; PadMarkSidebar(); }
    }

    void PadInMenu(ushort pressed)
    {
        var items = padMenu!.Items.Cast<ToolStripItem>().Where(i => i.Enabled && i.Available && i is not ToolStripSeparator).ToArray();
        if (items.Length == 0) { padMenu.Close(); return; }
        int i = Array.FindIndex(items, x => x.Selected);
        if ((pressed & Pad.B) != 0) { padMenu.Close(); return; }
        if ((pressed & (Pad.Up | Pad.Down)) != 0) items[(i + ((pressed & Pad.Down) != 0 ? 1 : -1) + items.Length) % items.Length].Select();
        if ((pressed & Pad.A) != 0 && i >= 0) { items[i].PerformClick(); padMenu.Close(); }
    }

    // ---- inside a page
    void PadInPage(ushort pressed)
    {
        var c = PadFocus();
        if (c is ComboBox open && open.DroppedDown)
        {
            // an open list: up / down choose, A takes, B closes it
            if ((pressed & Pad.Up) != 0 && open.SelectedIndex > 0) open.SelectedIndex--;
            if ((pressed & Pad.Down) != 0 && open.SelectedIndex < open.Items.Count - 1) open.SelectedIndex++;
            if ((pressed & (Pad.A | Pad.B)) != 0) open.DroppedDown = false;
            return;
        }
        if ((pressed & Pad.B) != 0)
        {
            padInPage = false;
            PadMarkSidebar();
            return;
        }
        if ((pressed & (Pad.Up | Pad.Down)) != 0)
        {
            PadMove(c, 0, (pressed & Pad.Down) != 0 ? 1 : -1);
            return;
        }
        int step = (pressed & Pad.Right) != 0 ? 1 : (pressed & Pad.Left) != 0 ? -1 : 0;
        if (step != 0)
        {
            switch (c)
            {
                case ComboBox cb when cb.Items.Count > 0:
                    cb.SelectedIndex = Math.Clamp(cb.SelectedIndex + step, 0, cb.Items.Count - 1);
                    break;
                case TrackBar tb:
                    tb.Value = Math.Clamp(tb.Value + step * Math.Max(1, tb.SmallChange * 5), tb.Minimum, tb.Maximum);
                    break;
                case Toggle tg when tg.Enabled && tg.On != (step > 0):
                    tg.PerformToggle();
                    break;
                default:   // left / right as up / down between controls
                    PadMove(c, step, 0);
                    break;
            }
            return;
        }
        if ((pressed & Pad.X) != 0 && c?.ContextMenuStrip is ContextMenuStrip menu)
        {
            padMenu = menu;
            menu.Show(c, new Point(0, c.Height));
            menu.Items.Cast<ToolStripItem>().FirstOrDefault(i => i.Enabled && i is not ToolStripSeparator)?.Select();
            return;
        }
        if ((pressed & Pad.A) != 0)
        {
            switch (c)
            {
                case Button b when b.Enabled: b.PerformClick(); break;
                case CheckBox cx when cx.Enabled: cx.Checked = !cx.Checked; break;
                case ComboBox cb when cb.Enabled: cb.DroppedDown = true; break;
                case Toggle tg when tg.Enabled: tg.PerformToggle(); break;
            }
        }
    }

    void PadScrollTo(Control? c)
    {
        if (c == null) return;
        for (var p = c.Parent; p != null; p = p.Parent)
            if (p is ScrollableControl sc && sc.AutoScroll) { sc.ScrollControlIntoView(c); break; }
        c.Invalidate();
    }
}
