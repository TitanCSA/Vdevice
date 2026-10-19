using System;
using System.Globalization;
using System.Text;
using System.Threading;
using Vdevice.Setup;

internal static class Program
{
    private static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        if (args.Length == 1 && args[0] == "--self-test")
        {
            try { Console.WriteLine(Package.SelfTest()); return 0; }
            catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        }
        if (args.Length != 1 || args[0] == "/?" || args[0] == "--help")
        {
            Console.WriteLine("Vdevice " + Package.Version + " — Virtual USB Device Driver Setup");
            Console.WriteLine("Usage: VdeviceSetupConsole.exe /install | /uninstall | /status | /diagnose");
            Console.WriteLine("/install    Install or update the embedded driver and its root USB controller.");
            Console.WriteLine("/uninstall  Remove only Vdevice controllers and their owned driver package.");
            Console.WriteLine("/status     Read PnP, service and driver API state.");
            Console.WriteLine("/diagnose   Read-only diagnosis with a detailed report.");
            Console.WriteLine("Exit 0: success; 3010: success with restart required; other values: error.");
            Console.WriteLine("Installation requires a properly signed catalog. Security settings are never changed.");
            return args.Length == 1 ? 0 : 87;
        }
        string operation = args[0].ToLowerInvariant();
        if (operation != "/install" && operation != "/uninstall" && operation != "/status" && operation != "/diagnose")
        { Console.Error.WriteLine("Unknown argument. Use /?."); return 87; }
        SetupResult result = Package.Run(operation, delegate(int percent, string message)
        { Console.WriteLine("[" + percent.ToString("D3", CultureInfo.InvariantCulture) + "%] " + message); }, CancellationToken.None);
        return result.ExitCode;
    }
}
