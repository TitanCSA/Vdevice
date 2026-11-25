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
        internal SetupSnapshot Snapshot;
        internal bool RestartReported;
        internal bool Success { get { return ExitCode == 0 || ExitCode == 3010; } }
        internal bool RebootRequired { get { return ExitCode == 3010 || RestartReported || (Snapshot != null && Snapshot.RestartRequired); } }
    }
    internal sealed class SetupSnapshot
    {
        public int Schema { get; set; }
        public uint RootDevices { get; set; }
        public uint StartedControllers { get; set; }
        public uint ProblemControllers { get; set; }
        public uint StagedPackages { get; set; }
        public bool ServiceInstalled { get; set; }
        public bool ServiceRunning { get; set; }
        public bool ApiAvailable { get; set; }
        public uint ApiError { get; set; }
        public uint ImageError { get; set; }
        public bool PayloadMatches { get; set; }
        public bool RestartRequired { get; set; }
        public bool TrialExpired { get; set; }
        public bool TrialStarted { get; set; }
        public ulong TrialRemainingSeconds { get; set; }
        public bool ControllerQuiesced { get; set; }
        public uint QueryError { get; set; }
        public string LastOperation { get; set; }
        public string LastStage { get; set; }
        public string LastState { get; set; }
        public string LastExitCode { get; set; }
        internal bool Installed { get { return RootDevices != 0 || ServiceInstalled || StagedPackages != 0 || ApiAvailable; } }
        internal bool Ready { get { return QueryError == 0 && ServiceRunning && ApiAvailable && StartedControllers != 0 && ProblemControllers == 0 && PayloadMatches && !ControllerQuiesced && !RestartRequired && !TrialExpired; } }
        internal string InstallationText
        {
            get
            {
                if (RestartRequired && LastOperation == "Uninstall") return "Removal pending restart";
                if (Ready) return "Installed and ready";
                if (RestartRequired) return Installed ? "Installed · restart required" : "Restart required";
                if (TrialExpired && ApiAvailable && PayloadMatches) return "Installed · demo expired";
                if (!Installed) return QueryError == 0 ? "Not installed" : "Status unavailable";
                if (RootDevices != 0 || ServiceInstalled || ApiAvailable) return "Installed · needs attention";
                return "Package staged · not active";
            }
        }
        internal static SetupSnapshot Parse(string json)
        {
            JavaScriptSerializer serializer = new JavaScriptSerializer();
            Dictionary<string, object> values = serializer.Deserialize<Dictionary<string, object>>(json);
            if (values == null) throw new InvalidDataException("Missing Windows installation state.");
            foreach (string key in new[] { "Schema", "RootDevices", "StartedControllers", "ProblemControllers", "StagedPackages", "ServiceInstalled", "ServiceRunning", "ApiAvailable", "ApiError", "ImageError", "PayloadMatches", "RestartRequired", "TrialExpired", "TrialStarted", "TrialRemainingSeconds", "ControllerQuiesced", "QueryError", "LastOperation", "LastStage", "LastState", "LastExitCode" })
                if (!values.ContainsKey(key)) throw new InvalidDataException("Incomplete Windows installation state: " + key);
            SetupSnapshot result = serializer.Deserialize<SetupSnapshot>(json);
            if (result.Schema != 1 || result.StartedControllers > result.RootDevices || result.ProblemControllers > result.RootDevices
                || (ulong)result.StartedControllers + result.ProblemControllers != result.RootDevices || (result.ServiceRunning && !result.ServiceInstalled))
                throw new InvalidDataException("Invalid Windows installation state.");
            return result;
        }
    }
    internal static class Package
    {
        private static readonly HashSet<string> Allowed = new HashSet<string>(StringComparer.Ordinal)
        { "Vdevice.inf", "Vdevice.sys", "Vdevice.cat", "TitanDemoClock.sys", "Signing.json", "VdeviceSetupNative.exe" };
        internal static string Version { get { return "1.0.6.1"; } }
        private static Stream Resource(string name)
        {
            Stream result = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
            if (result == null) throw new InvalidDataException("Missing embedded resource: " + name);
            return result;
        }
        internal static string DigestEntry(Stream input, long expected, Stream output = null)
        {
            if (expected <= 0 || expected > 64L * 1024 * 1024) throw new InvalidDataException("Invalid decoded payload size.");
            byte[] buffer = new byte[65536]; long total = 0; int count;
            using (SHA256 hash = SHA256.Create())
            {
                while ((count = input.Read(buffer, 0, buffer.Length)) != 0)
                {
                    if (count > expected - total) throw new InvalidDataException("Decoded payload exceeds its declared size.");
                    total += count;
                    hash.TransformBlock(buffer, 0, count, buffer, 0);
                    if (output != null) output.Write(buffer, 0, count);
                }
                if (total != expected) throw new InvalidDataException("Decoded payload is truncated.");
                hash.TransformFinalBlock(buffer, 0, 0);
                return BitConverter.ToString(hash.Hash).Replace("-", "");
            }
        }
        internal static MutexSecurity OperationSecurity()
        {
            var security = new MutexSecurity();
            security.SetAccessRuleProtection(true, false);
            security.SetOwner(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null));
            foreach (var kind in new[] { WellKnownSidType.BuiltinAdministratorsSid, WellKnownSidType.LocalSystemSid })
                security.AddAccessRule(new MutexAccessRule(new SecurityIdentifier(kind, null), MutexRights.FullControl, AccessControlType.Allow));
            return security;
        }
        private static Mutex OperationMutex()
        {
            bool created; Mutex mutex = new Mutex(false, @"Global\VdeviceSetup-Transaction", out created, OperationSecurity());
            try
            {
                var security = mutex.GetAccessControl();
                var admins = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);
                var system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
                var owner = security.GetOwner(typeof(SecurityIdentifier));
                if (!security.AreAccessRulesProtected || (!admins.Equals(owner) && !system.Equals(owner))) throw new UnauthorizedAccessException("The setup transaction lock has an unexpected owner or ACL.");
                bool hasAdmins = false, hasSystem = false;
                foreach (MutexAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
                {
                    if (rule.AccessControlType != AccessControlType.Allow || rule.IsInherited || rule.MutexRights != MutexRights.FullControl
                        || (!admins.Equals(rule.IdentityReference) && !system.Equals(rule.IdentityReference)))
                        throw new UnauthorizedAccessException("The setup transaction lock permits an unexpected principal.");
                    hasAdmins |= admins.Equals(rule.IdentityReference); hasSystem |= system.Equals(rule.IdentityReference);
                }
                if (!hasAdmins || !hasSystem) throw new UnauthorizedAccessException("The setup transaction lock ACL is incomplete.");
                return mutex;
            }
            catch { mutex.Dispose(); throw; }
        }
        internal static SetupResult Exclusive(Func<SetupResult> operation, Func<Mutex> factory)
        {
            using (Mutex mutex = factory())
            {
                bool held = false;
                try
                {
                    try { held = mutex.WaitOne(0); } catch (AbandonedMutexException) { held = true; }
                    if (!held) return new SetupResult { ExitCode = 1618, Report = "Another Vdevice setup operation is running. Wait for it to finish and retry." };
                    return operation();
                }
                finally { if (held) mutex.ReleaseMutex(); }
            }
        }
        internal static bool ExpectedRemovalRestart(SetupSnapshot state, int nativeExit, int verificationExit)
        {
            return nativeExit == 3010 && verificationExit == 21 && state != null && state.LastOperation == "Uninstall"
                && state.ControllerQuiesced && state.RestartRequired && state.ApiAvailable && state.PayloadMatches
                && state.ImageError == 0 && state.QueryError == 21;
        }
        internal static bool IsMicrosoftPolicy(string text)
        {
            var policy = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(text);
            if (policy == null) throw new InvalidDataException("Missing demo signing policy.");
            foreach (string key in new[] { "Product", "DriverVersion", "Mode", "TrialSeconds", "ClockRequired", "InstallationBlocked" })
                if (!policy.ContainsKey(key)) throw new InvalidDataException("Incomplete demo signing policy: " + key);
            if (!(policy["Product"] is string) || (string)policy["Product"] != "Vdevice"
                || !(policy["DriverVersion"] is string) || (string)policy["DriverVersion"] != Version
                || !(policy["TrialSeconds"] is int) || (int)policy["TrialSeconds"] != 3600
                || !(policy["ClockRequired"] is bool) || !(bool)policy["ClockRequired"]
                || !(policy["Mode"] is string) || !(policy["InstallationBlocked"] is bool))
                throw new InvalidDataException("The payload does not contain the matching Vdevice demo.");
            var mode = (string)policy["Mode"];
            var blocked = (bool)policy["InstallationBlocked"];
            if (mode == "Microsoft" && !blocked) return true;
            if (mode == "Development" && blocked) return false;
            throw new InvalidDataException("Invalid demo signing mode.");
        }
        private static Dictionary<string, string> Manifest()
        {
            using (Stream input = Resource("PayloadHashes.json"))
            using (StreamReader reader = new StreamReader(input, Encoding.UTF8))
            {
                Dictionary<string, string> hashes = new JavaScriptSerializer().Deserialize<Dictionary<string, string>>(reader.ReadToEnd());
                if (hashes == null || hashes.Count != Allowed.Count) throw new InvalidDataException("Invalid payload manifest.");
                foreach (KeyValuePair<string, string> pair in hashes)
                    if (!Allowed.Contains(pair.Key) || pair.Value == null || pair.Value.Length != 64 || !System.Text.RegularExpressions.Regex.IsMatch(pair.Value, "\\A[0-9A-Fa-f]{64}\\z")) throw new InvalidDataException("Invalid payload manifest entry.");
                foreach (string required in Allowed)
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
                        if (!String.Equals(DigestEntry(data, entry.Length), expected, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("Payload integrity check failed: " + entry.FullName);
                    if (visitor != null) visitor(entry);
                }
            }
        }
        internal static string SelfTest()
        {
            bool installable = false;
            VisitPayload(delegate(ZipArchiveEntry entry)
            {
                if (entry.FullName != "Signing.json") return;
                if (entry.Length > 4096) throw new InvalidDataException("Demo signing policy is too large.");
                using (var reader = new StreamReader(entry.Open(), Encoding.UTF8))
                    installable = IsMicrosoftPolicy(reader.ReadToEnd());
            });
            return "{\"passed\":true,\"installedAnything\":false,\"version\":\"" + Version + "\",\"installable\":"
                + (installable ? "true" : "false") + ",\"payloadFiles\":" + Manifest().Count.ToString(CultureInfo.InvariantCulture) + "}";
        }
        private static void CheckAncestors(string directory)
        {
            for (string ancestor = directory; !String.IsNullOrEmpty(ancestor); ancestor = Path.GetDirectoryName(ancestor))
                if (Directory.Exists(ancestor) && (File.GetAttributes(ancestor) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("The setup directory or an ancestor is redirected.");
        }
        internal static void Cleanup(string directory)
        {
            string parent = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Temp");
            string path = Path.GetFullPath(directory);
            Guid identifier;
            if (!String.Equals(Path.GetDirectoryName(path), parent, StringComparison.OrdinalIgnoreCase)
                || !Path.GetFileName(path).StartsWith("Vdevice-", StringComparison.Ordinal)
                || !Guid.TryParseExact(Path.GetFileName(path).Substring(8), "N", out identifier))
                throw new IOException("Cleanup outside the private Vdevice setup directory.");
            CheckAncestors(path);
            if (!Directory.Exists(path)) return;
            Stack<string> pending = new Stack<string>(); pending.Push(path);
            while (pending.Count != 0)
                foreach (string child in Directory.EnumerateFileSystemEntries(pending.Pop()))
                {
                    FileAttributes flags = File.GetAttributes(child);
                    if ((flags & FileAttributes.ReparsePoint) != 0) throw new IOException("Setup cleanup refuses redirected entries.");
                    if ((flags & FileAttributes.Directory) != 0) pending.Push(child);
                }
            Directory.Delete(path, true);
        }
        internal static int RunHelper(ProcessStartInfo start, Action<string> output)
        {
            object gate = new object();
            using (Process child = new Process { StartInfo = start })
            {
                DataReceivedEventHandler handler = delegate(object sender, DataReceivedEventArgs e)
                {
                    if (e.Data == null) return;
                    lock (gate) { try { output(e.Data); } catch (Exception error) { Debug.WriteLine("Setup output callback: " + error); } }
                };
                child.OutputDataReceived += handler; child.ErrorDataReceived += handler;
                if (!child.Start()) throw new IOException("The native setup helper did not start.");
                IntPtr heldHandle = child.Handle;
                child.BeginOutputReadLine(); child.BeginErrorReadLine();
                Stopwatch waiting = Stopwatch.StartNew(); long nextUpdate = 10;
                while (!child.WaitForExit(1000))
                    if (waiting.Elapsed.TotalSeconds >= nextUpdate)
                    {
                        try { output("Windows is still completing this operation (" + ((long)waiting.Elapsed.TotalSeconds).ToString(CultureInfo.InvariantCulture) + " s). Save your work and keep this window open. Windows PnP changes must finish safely; applications retaining device handles can require a restart."); } catch (Exception error) { Debug.WriteLine(error); }
                        nextUpdate += 10;
                    }
                child.WaitForExit(); return child.ExitCode;
            }
        }
        internal static SetupResult Run(string operation, Action<int, string> progress, CancellationToken cancellation)
        {
            try { return Exclusive(delegate { return RunCore(operation, progress, cancellation); }, OperationMutex); }
            catch (Exception error)
            {
                return new SetupResult { ExitCode = error is UnauthorizedAccessException ? 5 : 1,
                    Report = "Setup transaction failed: " + error.GetType().Name + ": " + error.Message };
            }
        }
        private static SetupResult RunCore(string operation, Action<int, string> progress, CancellationToken cancellation)
        {
            if (operation != "/install" && operation != "/uninstall" && operation != "/status" && operation != "/diagnose")
                throw new ArgumentException("Unsupported setup operation.", "operation");
            StringBuilder report = new StringBuilder();
            object gate = new object();
            Action<int, string> log = delegate(int percent, string message)
            {
                lock (gate) report.AppendLine(message);
                if (progress != null) { try { progress(percent, message); } catch (Exception error) { Debug.WriteLine("Setup display callback: " + error); } }
            };
            SetupResult result = new SetupResult();
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
                CheckAncestors(Path.GetDirectoryName(stage));
                if (Directory.Exists(stage)) throw new IOException("The private setup directory already exists.");
                Directory.CreateDirectory(stage, security);
                VisitPayload(delegate(ZipArchiveEntry entry)
                {
                    cancellation.ThrowIfCancellationRequested();
                    string destination = Path.Combine(stage, entry.FullName);
                    using (Stream input = entry.Open())
                    using (FileStream output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None)) DigestEntry(input, entry.Length, output);
                });
                log(30, "Payload SHA-256 checks passed.");
                if (operation == "/install" && !IsMicrosoftPolicy(File.ReadAllText(Path.Combine(stage, "Signing.json"), Encoding.UTF8)))
                {
                    log(100, "Installation stopped: this development build requires the matching Microsoft-signed demo package.");
                    result.ExitCode = 577; result.Report = report.ToString(); return result;
                }
                cancellation.ThrowIfCancellationRequested();
                string arguments = operation == "/install" ? "/install \"" + Path.Combine(stage, "Vdevice.inf") + "\"" : operation == "/status" || operation == "/diagnose" ? operation + " \"" + Path.Combine(stage, "Vdevice.sys") + "\"" : operation;
                ProcessStartInfo start = new ProcessStartInfo(Path.Combine(stage, "VdeviceSetupNative.exe"), arguments);
                start.UseShellExecute = false; start.CreateNoWindow = true; start.WorkingDirectory = stage;
                start.RedirectStandardOutput = true; start.RedirectStandardError = true;
                start.StandardOutputEncoding = Encoding.UTF8; start.StandardErrorEncoding = Encoding.UTF8;
                log(40, operation == "/install" ? "Checking catalog trust and applying the Windows driver package..." : operation == "/uninstall" ? "Removing only Vdevice root controllers and their owned package..." : "Reading Windows device state, service state, and the driver API...");
                string snapshotError = null;
                Action<string> recordOutput = delegate(string line)
                {
                    if (line == "VDEVICE_RESTART_REQUIRED: true") result.RestartReported = true;
                    if (line.StartsWith("VDEVICE_STATUS: ", StringComparison.Ordinal))
                    {
                        try { result.Snapshot = SetupSnapshot.Parse(line.Substring(16)); }
                        catch (Exception error) { snapshotError = error.Message; }
                    }
                    else log(65, line);
                };
                result.ExitCode = RunHelper(start, recordOutput);
                result.RestartReported = result.RestartReported || result.ExitCode == 3010;
                if (operation == "/install" || operation == "/uninstall")
                {
                    log(85, "Verifying the current Windows installation after " + operation + "...");
                    result.Snapshot = null; snapshotError = null;
                    start.Arguments = "/status \"" + Path.Combine(stage, "Vdevice.sys") + "\"";
                    int verification = RunHelper(start, recordOutput);
                    if (result.Success && verification != 0 && verification != 3010
                        && !(operation == "/uninstall" && ExpectedRemovalRestart(result.Snapshot, result.ExitCode, verification)))
                        result.ExitCode = verification;
                    else if (result.ExitCode == 0 && verification == 3010) result.ExitCode = 3010;
                }
                if (result.Snapshot == null || snapshotError != null) { log(90, "Windows state could not be verified: " + (snapshotError ?? "no structured status received")); if (result.Success) result.ExitCode = 13; }
                if (operation == "/diagnose")
                {
                    log(90, "Diagnostics are read-only. No virtual device was created and no input was injected.");
                    log(90, "Descriptor compatibility, application behavior, and report output require testing the actual profile; this check does not certify every device model.");
                }
                if (result.Snapshot != null) log(95, "Installation: " + result.Snapshot.InstallationText + ".");
                log(100, !result.Success ? "Operation reported an error. Exit code: " + result.ExitCode.ToString(CultureInfo.InvariantCulture) + ". See the failed stage above."
                    : result.RebootRequired ? "Windows requires a restart. Use Restart in this installer when your work is saved." : "Checks completed successfully.");
                result.Report = report.ToString(); return result;
            }
            catch (Exception error)
            {
                log(100, "ERROR: " + error.GetType().Name + ": " + error.Message);
                result.ExitCode = error is OperationCanceledException ? 1223 : error is UnauthorizedAccessException ? 5 : 1;
                result.Report = report.ToString(); return result;
            }
            finally
            {
                // The directory is created here from a random GUID. Never delete an
                // externally supplied path or follow directories from an archive.
                try { Cleanup(stage); }
                catch (IOException error) { log(100, "Temporary-file cleanup deferred: " + error.Message); }
                catch (UnauthorizedAccessException error) { log(100, "Temporary-file cleanup deferred: " + error.Message); }
                result.Report = report.ToString();
            }
        }
    }
}
