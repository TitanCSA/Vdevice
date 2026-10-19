using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace Vdevice.Setup
{
    internal sealed class SetupResult
    {
        internal int ExitCode;
        internal string Report;
        internal bool Success { get { return ExitCode == 0 || ExitCode == 3010; } }
        internal bool RebootRequired { get { return ExitCode == 3010; } }
    }
    internal static class Package
    {
        private static readonly HashSet<string> Allowed = new HashSet<string>(StringComparer.Ordinal)
        { "Vdevice.inf", "Vdevice.sys", "Vdevice.cat", "VdeviceSetupNative.exe" };
        internal static string Version { get { return "1.0.0.0"; } }
        private static Stream Resource(string name)
        {
            Stream result = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
            if (result == null) throw new InvalidDataException("Missing embedded resource: " + name);
            return result;
        }
        private static string Digest(Stream stream)
        {
            using (SHA256 hash = SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "");
        }
        private static Dictionary<string, string> Manifest()
        {
            using (Stream input = Resource("PayloadHashes.json"))
            using (StreamReader reader = new StreamReader(input, Encoding.UTF8))
            {
                Dictionary<string, string> hashes = new JavaScriptSerializer().Deserialize<Dictionary<string, string>>(reader.ReadToEnd());
                if (hashes == null || hashes.Count < 3 || hashes.Count > Allowed.Count) throw new InvalidDataException("Invalid payload manifest.");
                foreach (KeyValuePair<string, string> pair in hashes)
                    if (!Allowed.Contains(pair.Key) || pair.Value == null || pair.Value.Length != 64) throw new InvalidDataException("Invalid payload manifest entry.");
                foreach (string required in new[] { "Vdevice.inf", "Vdevice.sys", "VdeviceSetupNative.exe" })
                    if (!hashes.ContainsKey(required)) throw new InvalidDataException("Missing required payload: " + required);
                return hashes;
            }
        }
        private static void VisitPayload(Action<ZipArchiveEntry> visitor)
        {
            Dictionary<string, string> hashes = Manifest();
            HashSet<string> seen = new HashSet<string>(StringComparer.Ordinal);
            using (Stream input = Resource("Payload.zip"))
            using (ZipArchive archive = new ZipArchive(input, ZipArchiveMode.Read))
            {
                if (archive.Entries.Count != hashes.Count) throw new InvalidDataException("Unexpected payload entry count.");
                long total = 0;
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    string expected;
                    if (!Allowed.Contains(entry.FullName) || !hashes.TryGetValue(entry.FullName, out expected) || !seen.Add(entry.FullName))
                        throw new InvalidDataException("Unexpected, duplicate, or unsafe payload path.");
                    if (entry.Length <= 0 || entry.Length > 64 * 1024 * 1024 || (total += entry.Length) > 128 * 1024 * 1024)
                        throw new InvalidDataException("Invalid payload size.");
                    using (Stream data = entry.Open())
                        if (!String.Equals(Digest(data), expected, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Payload integrity check failed: " + entry.FullName);
                    if (visitor != null) visitor(entry);
                }
            }
        }
        internal static string SelfTest()
        {
            VisitPayload(null);
            return "{\"passed\":true,\"installedAnything\":false,\"version\":\"" + Version + "\",\"payloadFiles\":" + Manifest().Count.ToString(CultureInfo.InvariantCulture) + "}";
        }
        internal static SetupResult Run(string operation, Action<int, string> progress, CancellationToken cancellation)
        {
            if (operation != "/install" && operation != "/uninstall" && operation != "/status" && operation != "/diagnose")
                throw new ArgumentException("Unsupported setup operation.", "operation");
            StringBuilder report = new StringBuilder();
            object gate = new object();
            Action<int, string> log = delegate(int percent, string message)
            {
                lock (gate) report.AppendLine(message);
                if (progress != null) progress(percent, message);
            };
            // A protected directory under the Windows temp directory prevents a
            // medium-integrity process from replacing an elevated helper. The
            // current user's writable %TEMP% is deliberately not used here.
            string stage = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Temp", "Vdevice-" + Guid.NewGuid().ToString("N"));
            try
            {
                cancellation.ThrowIfCancellationRequested();
                log(5, "Vdevice " + Version + " | " + DateTime.UtcNow.ToString("u", CultureInfo.InvariantCulture));
                log(8, "Operation: " + operation + "; Windows " + Environment.OSVersion.Version);
                log(12, "Verifying the embedded driver and setup helper...");
                DirectorySecurity security = new DirectorySecurity();
                SecurityIdentifier admins = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);
                SecurityIdentifier system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
                security.SetAccessRuleProtection(true, false); security.SetOwner(admins);
                InheritanceFlags inherit = InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit;
                security.AddAccessRule(new FileSystemAccessRule(admins, FileSystemRights.FullControl, inherit, PropagationFlags.None, AccessControlType.Allow));
                security.AddAccessRule(new FileSystemAccessRule(system, FileSystemRights.FullControl, inherit, PropagationFlags.None, AccessControlType.Allow));
                Directory.CreateDirectory(stage, security);
                VisitPayload(delegate(ZipArchiveEntry entry)
                {
                    cancellation.ThrowIfCancellationRequested();
                    string destination = Path.Combine(stage, entry.FullName);
                    using (Stream input = entry.Open())
                    using (FileStream output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None)) input.CopyTo(output);
                });
                log(30, "Payload SHA-256 checks passed.");
                if (operation == "/install" && !File.Exists(Path.Combine(stage, "Vdevice.cat")))
                {
                    log(100, "Installation stopped: the package has no driver catalog. Sign the generated driver package before using this installer on standard Windows. Windows security settings were not changed.");
                    return new SetupResult { ExitCode = 577, Report = report.ToString() };
                }
                cancellation.ThrowIfCancellationRequested();
                string arguments = operation == "/install" ? "/install \"" + Path.Combine(stage, "Vdevice.inf") + "\"" : operation == "/status" || operation == "/diagnose" ? operation + " \"" + Path.Combine(stage, "Vdevice.sys") + "\"" : operation;
                ProcessStartInfo start = new ProcessStartInfo(Path.Combine(stage, "VdeviceSetupNative.exe"), arguments);
                start.UseShellExecute = false; start.CreateNoWindow = true; start.WorkingDirectory = stage;
                start.RedirectStandardOutput = true; start.RedirectStandardError = true;
                start.StandardOutputEncoding = Encoding.UTF8; start.StandardErrorEncoding = Encoding.UTF8;
                log(40, operation == "/install" ? "Checking catalog trust and applying the Windows driver package..." : operation == "/uninstall" ? "Removing only Vdevice root controllers and their owned package..." : "Reading Windows device state, service state, and the driver API...");
                int exitCode;
                using (Process child = new Process())
                {
                    child.StartInfo = start;
                    DataReceivedEventHandler output = delegate(object sender, DataReceivedEventArgs e) { if (e.Data != null) log(65, e.Data); };
                    child.OutputDataReceived += output; child.ErrorDataReceived += output;
                    if (!child.Start()) throw new InvalidOperationException("The native setup helper did not start.");
                    child.BeginOutputReadLine(); child.BeginErrorReadLine();
                    // Do not kill a Windows installation halfway through. Closing the UI is
                    // disabled while an operation runs; all helper output is drained here.
                    Stopwatch waiting = Stopwatch.StartNew();
                    long nextUpdate = 10;
                    while (!child.WaitForExit(1000))
                    {
                        if (waiting.Elapsed.TotalSeconds >= nextUpdate)
                        {
                            log(65, "Windows is still completing " + operation + " (" + ((long)waiting.Elapsed.TotalSeconds).ToString(CultureInfo.InvariantCulture) + " s). The window is responsive; driver requests have cancellation deadlines, while Windows PnP changes must finish safely. Applications retaining controller handles can require a restart.");
                            nextUpdate += 10;
                        }
                    }
                    child.WaitForExit(); exitCode = child.ExitCode;
                }
                if (operation == "/diagnose")
                {
                    log(90, "Diagnostics are read-only. No virtual device was created and no input was injected.");
                    log(90, "Descriptor compatibility, application behavior, and report output require testing the actual profile; this check does not certify every device model.");
                }
                log(100, exitCode == 3010 ? "Completed. Windows requires a restart; this application does not restart the PC automatically." : exitCode == 0 ? "Completed successfully." : "Operation reported an error. Exit code: " + exitCode.ToString(CultureInfo.InvariantCulture) + ". See the details above.");
                return new SetupResult { ExitCode = exitCode, Report = report.ToString() };
            }
            catch (Exception error)
            {
                log(100, "ERROR: " + error.GetType().Name + ": " + error.Message);
                return new SetupResult { ExitCode = error is OperationCanceledException ? 1223 : 1, Report = report.ToString() };
            }
            finally
            {
                // The directory is created here from a random GUID. Never delete an
                // externally supplied path or follow directories from an archive.
                try { if (Directory.Exists(stage)) Directory.Delete(stage, true); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }
}
