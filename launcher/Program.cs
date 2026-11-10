using System.Reflection;

namespace KarPurLauncher;

static class Program
{
    // The launcher's version (KarPurLauncher.csproj <Version>), shown top left and compared with GitHub releases.
    public static readonly Version Current = Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 1, 0);
    public static string Version => $"{Current.Major}.{Current.Minor}.{Current.Build}";

    public static string? PreviewColour;
    public static bool DesignCheck;   // --shot: a picture of the window only, nothing else runs

    [STAThread]
    static void Main(string[] args)
    {
        Updater.CleanUpOld();
        ApplicationConfiguration.Initialize();
        PreviewColour = args.FirstOrDefault(a => a.StartsWith("--colour="))?[9..];
        string tab = args.FirstOrDefault(a => a.StartsWith("--tab="))?[6..] ?? "Setup";   // --tab=Settings: open on that page
        var form = new MainForm(updated: args.Contains("--updated"), tab);
        // --shot=<png>: a picture of the window a moment after it opened, then exit (a check of the design)
        string? shot = args.FirstOrDefault(a => a.StartsWith("--shot="))?[7..];
        DesignCheck = shot != null;
        if (shot != null)
            form.Shown += async (_, _) =>
            {
                await Task.Delay(1500);
                // the window's own painting (not the screen: another window may lie on top)
                using var bmp = new Bitmap(form.Width, form.Height);
                form.DrawToBitmap(bmp, new Rectangle(0, 0, form.Width, form.Height));
                bmp.Save(shot);
                form.Close();
            };
        Application.Run(form);
    }
}
