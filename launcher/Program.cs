using System.Reflection;

namespace KarPurLauncher;

static class Program
{
    // The launcher's version (KarPurLauncher.csproj <Version>), shown top left and compared with GitHub releases.
    public static readonly Version Current = Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 1, 0);
    public static string Version => $"{Current.Major}.{Current.Minor}.{Current.Build}";

    [STAThread]
    static void Main(string[] args)
    {
        Updater.CleanUpOld();
        ApplicationConfiguration.Initialize();
        string tab = args.FirstOrDefault(a => a.StartsWith("--tab="))?[6..] ?? "Setup";   // --tab=Settings: open on that page
        Application.Run(new MainForm(updated: args.Contains("--updated"), tab));
    }
}
