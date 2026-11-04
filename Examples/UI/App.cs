using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Vdevice.Example
{
    internal static class Native
    {
        private const string Library = "VdeviceBridge.dll";
        [StructLayout(LayoutKind.Sequential)] internal struct Packet
        {
            internal ulong DeviceId, RequestId;
            internal uint Kind, Endpoint, Length, Error;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] internal byte[] Setup;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4096)] internal byte[] Data;
        }
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern uint VdUiValidate([In] byte[] config, uint length, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern uint VdUiCreate([In] byte[] config, uint length, out ulong id, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern uint VdUiDelete(ulong id, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern uint VdUiSend(ulong id, uint endpoint, [In] byte[] data, uint length, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern uint VdUiInfo(ulong id, StringBuilder text, uint capacity, StringBuilder details, uint detailsCapacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern int VdUiReceive(out Packet packet, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] internal static extern uint VdUiRespond(ulong id, ulong request, [In] byte[] data, uint length, uint stall, StringBuilder error, uint capacity);
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void VdUiClose();
    }
    public sealed class DescriptorRow : INotifyPropertyChanged
    {
        public string Kind { get; set; }
        public string Index { get; set; }
        public string Language { get; set; }
        public string Interface { get; set; }
        private string bytes;
        public string Bytes { get { return bytes; } set { bytes = value; if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("Bytes")); } }
        public event PropertyChangedEventHandler PropertyChanged;
        public DescriptorRow() { Kind = "Standard"; Index = "0"; Language = "0000"; Interface = "0"; Bytes = ""; }
    }
    public sealed class RuleRow : INotifyPropertyChanged
    {
        public string Setup { get; set; }
        public string Mask { get; set; }
        public string Action { get; set; }
        private string bytes;
        public string Bytes { get { return bytes; } set { bytes = value; if (PropertyChanged != null) PropertyChanged(this, new PropertyChangedEventArgs("Bytes")); } }
        public event PropertyChangedEventHandler PropertyChanged;
        public RuleRow() { Setup = "C0 00 00 00 00 00 00 00"; Mask = "FF FF FF FF FF FF FF FF"; Action = "Reply"; Bytes = ""; }
    }
    public sealed class WorkshopDocument
    {
        public int Schema { get; set; }
        public string Name { get; set; }
        public uint Speed { get; set; }
        public uint InputDepth { get; set; }
        public bool DelegateControls { get; set; }
        public DescriptorRow[] Descriptors { get; set; }
        public RuleRow[] Rules { get; set; }
        public WorkshopDocument() { Schema = 1; Name = "My USB device"; Speed = 2; InputDepth = 32; Descriptors = new DescriptorRow[0]; Rules = new RuleRow[0]; }
    }
    // These are editor/file helpers, not device profiles or public SDK APIs.
    internal static class Workshop
    {
        internal static uint Number(string text, uint maximum, bool hexadecimal)
        {
            if (text == null) throw new FormatException("A required number is missing.");
            text = text.Trim(); if (hexadecimal && text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) text = text.Substring(2);
            uint result;
            if (!UInt32.TryParse(text, hexadecimal ? NumberStyles.AllowHexSpecifier : NumberStyles.Integer, CultureInfo.InvariantCulture, out result) || result > maximum)
                throw new FormatException("Invalid " + (hexadecimal ? "hexadecimal" : "decimal") + " number: " + text);
            return result;
        }
        internal static byte[] Hex(string text, int maximum)
        {
            if (text == null) throw new FormatException("A byte string is missing.");
            string[] words = text.Split(new[] { ' ', '-', ',', '\r', '\n', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (words.Length > maximum) throw new FormatException("Too many bytes; limit " + maximum + ".");
            byte[] bytes = new byte[words.Length];
            for (int i = 0; i < words.Length; ++i)
            {
                string word = words[i]; if (word.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) word = word.Substring(2);
                if (word.Length != 2 || !Byte.TryParse(word, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out bytes[i]))
                    throw new FormatException("Each byte must contain two hexadecimal digits: " + words[i]);
            }
            return bytes;
        }
        internal static string Hex(byte[] bytes, int length) { return length == 0 ? "" : BitConverter.ToString(bytes, 0, length).Replace('-', ' '); }
        internal static byte[] Serialize(WorkshopDocument document)
        {
            if (document == null || document.Schema != 1 || document.Speed < 1 || document.Speed > 3 || document.InputDepth < 1 || document.InputDepth > 256
                || document.Descriptors == null || document.Descriptors.Length > 64 || document.Rules == null || document.Rules.Length > 32)
                throw new FormatException("Invalid document schema, speed, queue depth, or descriptor/rule count.");
            using (MemoryStream memory = new MemoryStream())
            using (BinaryWriter writer = new BinaryWriter(memory, Encoding.UTF8))
            {
                writer.Write(0x31574456u); writer.Write(document.Speed); writer.Write(document.InputDepth); writer.Write(document.DelegateControls ? 1u : 0u);
                writer.Write((uint)document.Descriptors.Length); writer.Write((uint)document.Rules.Length);
                foreach (DescriptorRow descriptor in document.Descriptors)
                {
                    if (descriptor == null || (descriptor.Kind != "Standard" && descriptor.Kind != "Report")) throw new FormatException("Descriptor kind must be Standard or Report.");
                    byte[] data = Hex(descriptor.Bytes, 65536);
                    writer.Write(descriptor.Kind == "Standard" ? 1u : 2u); writer.Write(Number(descriptor.Index, 255, false));
                    writer.Write(Number(descriptor.Language, 65535, true)); writer.Write(Number(descriptor.Interface, 255, false)); writer.Write((uint)data.Length); writer.Write(data);
                    if (memory.Length > 65536) throw new FormatException("The complete USB configuration exceeds the 64 KiB wire limit.");
                }
                foreach (RuleRow rule in document.Rules)
                {
                    if (rule == null) throw new FormatException("A control rule is missing.");
                    byte[] setup = Hex(rule.Setup, 8), mask = Hex(rule.Mask, 8), data = Hex(rule.Bytes, 4096);
                    if (setup.Length != 8 || mask.Length != 8) throw new FormatException("Every setup and mask must contain exactly eight bytes.");
                    uint action = rule.Action == "Reply" ? 1u : rule.Action == "Accept" ? 2u : rule.Action == "Stall" ? 3u : 0u;
                    if (action == 0) throw new FormatException("Rule action must be Reply, Accept, or Stall.");
                    writer.Write(setup); writer.Write(mask); writer.Write(action); writer.Write((uint)data.Length); writer.Write(data);
                    if (memory.Length > 65536) throw new FormatException("The complete USB configuration exceeds the 64 KiB wire limit.");
                }
                writer.Flush(); return memory.ToArray();
            }
        }
    }
    internal sealed class DeviceItem
    {
        internal ulong Id;
        internal string Name;
        public override string ToString() { return Name + " · #" + Id.ToString(CultureInfo.InvariantCulture); }
    }
    internal sealed class OutputItem
    {
        internal DateTime Received;
        internal long ReceivedTicks;
        internal Native.Packet Packet;
        internal double Age { get { return (Stopwatch.GetTimestamp() - ReceivedTicks) / (double)Stopwatch.Frequency; } }
        public string Time { get { return Received.ToLocalTime().ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture); } }
        public string Device { get { return "#" + Packet.DeviceId.ToString(CultureInfo.InvariantCulture); } }
        public string Kind { get { return Packet.Kind == 1 ? "OUT data" : Packet.Kind == 2 ? "Control" : "Device state"; } }
        public string Endpoint { get { return Packet.Endpoint.ToString("X2", CultureInfo.InvariantCulture); } }
        public uint Length { get { return Packet.Length; } }
        public ulong Request { get { return Packet.RequestId; } }
        public string Setup { get { return Workshop.Hex(Packet.Setup, Packet.Setup.Length); } }
        public string Bytes { get { return Workshop.Hex(Packet.Data, (int)Packet.Length); } }
        public string FullText { get { return Time + " | " + Device + " | " + Kind + " | endpoint " + Endpoint + " | request " + Request + "\r\nSetup: " + Setup + "\r\nData (" + Length + " bytes):\r\n" + Bytes; } }
        public override string ToString() { return Device + " / request " + Request + " / " + Setup; }
    }
    internal sealed class OutputBatch
    {
        internal readonly List<OutputItem> Packets = new List<OutputItem>();
        internal string Error;
    }
    internal sealed class MainWindow : Window
    {
        private readonly FrameworkElement root;
        private readonly ObservableCollection<DescriptorRow> descriptors = new ObservableCollection<DescriptorRow>();
        private readonly ObservableCollection<RuleRow> rules = new ObservableCollection<RuleRow>();
        private readonly ObservableCollection<DeviceItem> devices = new ObservableCollection<DeviceItem>();
        private readonly ObservableCollection<OutputItem> outputs = new ObservableCollection<OutputItem>();
        private readonly ObservableCollection<OutputItem> pending = new ObservableCollection<OutputItem>();
        private readonly SemaphoreSlim operation = new SemaphoreSlim(1, 1);
        private readonly DispatcherTimer timer = new DispatcherTimer();
        private readonly TextBox log;
        private readonly TextBlock status, details, deadline;
        private readonly ListBox deviceList, pendingList;
        private readonly DataGrid descriptorGrid, ruleGrid, outputGrid;
        private bool working, polling, closed;
        private DateTime nextStatus = DateTime.MinValue;
        private T Find<T>(string name) where T : class { return (T)root.FindName(name); }
        private string Text(string name) { return Find<TextBox>(name).Text; }
        internal MainWindow()
        {
            using (Stream input = typeof(MainWindow).Assembly.GetManifestResourceStream("MainWindow.xaml")) root = (FrameworkElement)XamlReader.Load(input);
            Content = root; Title = "Vdevice — Descriptor Workshop"; Width = 1380; Height = 930; MinWidth = 1120; MinHeight = 740;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            descriptorGrid = Find<DataGrid>("Descriptors"); descriptorGrid.ItemsSource = descriptors;
            ruleGrid = Find<DataGrid>("Rules"); ruleGrid.ItemsSource = rules;
            deviceList = Find<ListBox>("Devices"); deviceList.ItemsSource = devices;
            outputGrid = Find<DataGrid>("Outputs"); outputGrid.ItemsSource = outputs;
            pendingList = Find<ListBox>("Pending"); pendingList.ItemsSource = pending;
            log = Find<TextBox>("Log"); status = Find<TextBlock>("Status"); details = Find<TextBlock>("Details"); deadline = Find<TextBlock>("Deadline");
            Find<Button>("AddDescriptor").Click += delegate { DescriptorRow row = new DescriptorRow(); descriptors.Add(row); descriptorGrid.SelectedItem = row; };
            Find<Button>("RemoveDescriptor").Click += delegate { DescriptorRow row = descriptorGrid.SelectedItem as DescriptorRow; if (row != null) descriptors.Remove(row); };
            Find<Button>("AddRule").Click += delegate { RuleRow row = new RuleRow(); rules.Add(row); ruleGrid.SelectedItem = row; };
            Find<Button>("RemoveRule").Click += delegate { RuleRow row = ruleGrid.SelectedItem as RuleRow; if (row != null) rules.Remove(row); };
            Find<Button>("Validate").Click += async delegate { await CreateOrValidate(false); };
            Find<Button>("Create").Click += async delegate { await CreateOrValidate(true); };
            Find<Button>("Delete").Click += async delegate { await Delete(); };
            Find<Button>("Send").Click += async delegate { await Send(); };
            Find<Button>("Reply").Click += async delegate { await Respond(false); };
            Find<Button>("Stall").Click += async delegate { await Respond(true); };
            Find<Button>("Import").Click += delegate { Import(); };
            Find<Button>("Export").Click += delegate { Export(); };
            Find<Button>("Copy").Click += delegate { Copy(log.Text); };
            Find<Button>("CopyPacket").Click += delegate { OutputItem item = outputGrid.SelectedItem as OutputItem; if (item != null) Copy(item.FullText); };
            Find<Button>("Refresh").Click += async delegate { await Refresh(); };
            outputGrid.SelectionChanged += delegate { OutputItem item = outputGrid.SelectedItem as OutputItem; Find<TextBox>("PacketBytes").Text = item == null ? "" : item.FullText; };
            pendingList.SelectionChanged += delegate { UpdatePending(); };
            deviceList.SelectionChanged += async delegate { await Refresh(); };
            timer.Interval = TimeSpan.FromMilliseconds(150); timer.Tick += async delegate { await Poll(); };
            Closing += OnClosing; Loaded += async delegate { await Refresh(); };
            Log("The editor starts blank. Import your descriptor document or add each exact descriptor and control rule. Validation never opens the driver.");
        }
        private void Log(string message)
        {
            if (closed) return;
            if (log.Text.Length > 100000) log.Clear();
            log.AppendText(DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + "  " + message + Environment.NewLine); log.ScrollToEnd();
        }
        private void Copy(string text) { try { Clipboard.SetText(text); } catch (Exception error) { Log("Clipboard unavailable: " + error.Message); } }
        private WorkshopDocument Document()
        {
            descriptorGrid.CommitEdit(DataGridEditingUnit.Cell, true); descriptorGrid.CommitEdit(DataGridEditingUnit.Row, true);
            ruleGrid.CommitEdit(DataGridEditingUnit.Cell, true); ruleGrid.CommitEdit(DataGridEditingUnit.Row, true);
            DescriptorRow[] descriptorRows = new DescriptorRow[descriptors.Count]; descriptors.CopyTo(descriptorRows, 0);
            RuleRow[] ruleRows = new RuleRow[rules.Count]; rules.CopyTo(ruleRows, 0);
            return new WorkshopDocument { Name = Text("DeviceName"), Speed = (uint)(Find<ComboBox>("Speed").SelectedIndex + 1), InputDepth = Workshop.Number(Text("Depth"), 256, false), DelegateControls = Find<CheckBox>("Delegate").IsChecked == true, Descriptors = descriptorRows, Rules = ruleRows };
        }
        private void LoadDocument(WorkshopDocument document)
        {
            Workshop.Serialize(document);
            Find<TextBox>("DeviceName").Text = document.Name ?? "My USB device"; Find<ComboBox>("Speed").SelectedIndex = (int)document.Speed - 1;
            Find<TextBox>("Depth").Text = document.InputDepth.ToString(CultureInfo.InvariantCulture); Find<CheckBox>("Delegate").IsChecked = document.DelegateControls;
            descriptors.Clear(); foreach (DescriptorRow row in document.Descriptors) descriptors.Add(row);
            rules.Clear(); foreach (RuleRow row in document.Rules) rules.Add(row);
            if (descriptors.Count > 0) descriptorGrid.SelectedIndex = 0;
        }
        private void Import()
        {
            try
            {
                OpenFileDialog dialog = new OpenFileDialog { Filter = "Vdevice descriptor document (*.json)|*.json", CheckFileExists = true, InitialDirectory = AppDomain.CurrentDomain.BaseDirectory, FileName = "CustomDevice.json" };
                if (dialog.ShowDialog(this) != true) return;
                if (new FileInfo(dialog.FileName).Length > 1024 * 1024) throw new FormatException("Document is limited to 1 MiB.");
                JavaScriptSerializer serializer = new JavaScriptSerializer { MaxJsonLength = 1024 * 1024 };
                LoadDocument(serializer.Deserialize<WorkshopDocument>(File.ReadAllText(dialog.FileName, Encoding.UTF8)));
                Log("Imported exact descriptor/rule bytes. Validate before creating the device.");
            }
            catch (Exception error) { Log("IMPORT ERROR: " + error.Message); }
        }
        private void Export()
        {
            try
            {
                WorkshopDocument document = Document(); Workshop.Serialize(document);
                SaveFileDialog dialog = new SaveFileDialog { Filter = "Vdevice descriptor document (*.json)|*.json", FileName = "device.json" };
                if (dialog.ShowDialog(this) != true) return;
                File.WriteAllText(dialog.FileName, new JavaScriptSerializer().Serialize(document), new UTF8Encoding(false)); Log("Descriptor document saved.");
            }
            catch (Exception error) { Log("EXPORT ERROR: " + error.Message); }
        }
        private async Task<bool> Begin()
        {
            if (working) { Log("Wait for the current operation."); return false; }
            working = true; await operation.WaitAsync(); return true;
        }
        private void End() { working = false; operation.Release(); }
        private async Task CreateOrValidate(bool create)
        {
            try
            {
                WorkshopDocument document = Document(); byte[] config = Workshop.Serialize(document);
                if (!await Begin()) return;
                try
                {
                    StringBuilder output = new StringBuilder(8192); ulong id = 0;
                    uint code = await Task.Run(delegate { return create ? Native.VdUiCreate(config, (uint)config.Length, out id, output, 8192) : Native.VdUiValidate(config, (uint)config.Length, output, 8192); });
                    Log(code == 0 ? create ? output.ToString() : "Descriptor/rule validation passed offline. No virtual device was created." : (create ? "CREATE" : "VALIDATE") + " ERROR " + code + ": " + output);
                    if (code == 0 && create) { DeviceItem item = new DeviceItem { Id = id, Name = String.IsNullOrWhiteSpace(document.Name) ? "USB device" : document.Name }; devices.Add(item); deviceList.SelectedItem = item; timer.Start(); nextStatus = DateTime.MinValue; }
                }
                finally { End(); }
            }
            catch (Exception error) { Log("CONFIGURATION ERROR: " + error.Message); }
        }
        private async Task Delete()
        {
            DeviceItem item = deviceList.SelectedItem as DeviceItem; if (item == null) { Log("Select an owned device first."); return; }
            if (!await Begin()) return;
            try
            {
                StringBuilder message = new StringBuilder(8192); uint code = await Task.Run(delegate { return Native.VdUiDelete(item.Id, message, 8192); });
                Log(code == 0 ? "Deleted " + item + "." : "DELETE ERROR " + code + ": " + message);
                if (code == 0)
                {
                    devices.Remove(item); for (int i = pending.Count - 1; i >= 0; --i) if (pending[i].Packet.DeviceId == item.Id) pending.RemoveAt(i);
                    if (devices.Count > 0) deviceList.SelectedIndex = 0; else timer.Stop(); UpdatePending(); nextStatus = DateTime.MinValue;
                }
            }
            catch (Exception error) { Log("DELETE ERROR: " + error.Message); }
            finally { End(); }
        }
        private async Task Send()
        {
            try
            {
                DeviceItem item = deviceList.SelectedItem as DeviceItem; if (item == null) { Log("Select an owned device first."); return; }
                uint endpoint = Workshop.Number(Text("Endpoint"), 255, true); byte[] data = Workshop.Hex(Text("InputBytes"), 4096);
                if (!await Begin()) return;
                try
                {
                    StringBuilder output = new StringBuilder(8192); uint code = await Task.Run(delegate { return Native.VdUiSend(item.Id, endpoint, data, (uint)data.Length, output, 8192); });
                    Log(code == 0 ? "IN accepted: " + item + ", endpoint " + endpoint.ToString("X2", CultureInfo.InvariantCulture) + ", " + data.Length + " exact bytes." : "SEND ERROR " + code + ": " + output);
                }
                finally { End(); }
            }
            catch (Exception error) { Log("SEND ERROR: " + error.Message); }
        }
        private async Task Respond(bool stall)
        {
            try
            {
                OutputItem item = pendingList.SelectedItem as OutputItem; if (item == null) { Log("Select a pending control request."); return; }
                byte[] data = stall ? new byte[0] : Workshop.Hex(Text("ResponseBytes"), 4096);
                if (!await Begin()) return;
                try
                {
                    StringBuilder message = new StringBuilder(8192);
                    uint code = await Task.Run(delegate { return Native.VdUiRespond(item.Packet.DeviceId, item.Packet.RequestId, data, (uint)data.Length, stall ? 1u : 0u, message, 8192); });
                    if (code == 0 || code == 1168 || code == 995) pending.Remove(item);
                    Log(code == 0 ? "Control request " + item.Request + (stall ? ": stalled." : ": replied with " + data.Length + " exact bytes.") : code == 1168 || code == 995 ? "Control request already finished, cancelled, or expired; continuing." : "REPLY ERROR " + code + ": " + message);
                    UpdatePending();
                }
                finally { End(); }
            }
            catch (Exception error) { Log("REPLY ERROR: " + error.Message); }
        }
        private void UpdatePending()
        {
            for (int i = pending.Count - 1; i >= 0; --i) if (pending[i].Age >= 5) pending.RemoveAt(i);
            OutputItem item = pendingList.SelectedItem as OutputItem;
            deadline.Text = item == null ? pending.Count + " pending requests. The driver deadline is five seconds; use control rules for prompt/repeated replies." : "Selected setup: " + item.Setup + " · received " + item.Age.ToString("F1", CultureInfo.InvariantCulture) + " s ago. Reply promptly.";
        }
        private async Task Refresh()
        {
            if (closed || !await operation.WaitAsync(0)) return;
            try
            {
                DeviceItem selected = deviceList.SelectedItem as DeviceItem; StringBuilder info = new StringBuilder(8192), detail = new StringBuilder(8192);
                uint code = await Task.Run(delegate { return Native.VdUiInfo(selected == null ? 0 : selected.Id, info, 8192, detail, 8192); });
                status.Text = code == 0 ? info.ToString() : "Driver status " + code + ": " + info; details.Text = detail.ToString();
            }
            catch (Exception error) { status.Text = "SDK bridge unavailable: " + error.Message; }
            finally { operation.Release(); }
        }
        private async Task Poll()
        {
            if (closed || polling || working || devices.Count == 0) return;
            polling = true;
            try
            {
                if (await operation.WaitAsync(0))
                {
                    try
                    {
                        OutputBatch received = await Task.Run(delegate
                        {
                            OutputBatch batch = new OutputBatch();
                            for (int i = 0; i < 16; ++i)
                            {
                                Native.Packet packet; StringBuilder error = new StringBuilder(8192); int result = Native.VdUiReceive(out packet, error, 8192);
                                if (result == 0) break;
                                if (result < 0) { batch.Error = "Output " + (-result) + ": " + error; break; }
                                batch.Packets.Add(new OutputItem { Received = DateTime.UtcNow, ReceivedTicks = Stopwatch.GetTimestamp(), Packet = packet });
                            }
                            return batch;
                        });
                        foreach (OutputItem item in received.Packets)
                        {
                            if (outputs.Count >= 256) outputs.RemoveAt(0); outputs.Add(item);
                            if (item.Request != 0 && item.Packet.Error == 0) { if (pending.Count >= 256) pending.RemoveAt(0); pending.Add(item); if (pendingList.SelectedItem == null) pendingList.SelectedItem = item; }
                            Log(item.Kind + ": " + item.Device + " endpoint " + item.Endpoint + " / " + item.Length + " bytes" + (item.Request == 0 ? "." : " / request " + item.Request + "."));
                            if (item.Packet.Error != 0) Log("The GUI could not retain this request for a reply (" + item.Packet.Error + "). Use control rules for high request rates.");
                        }
                        if (received.Error != null) { Log("OUTPUT ERROR: " + received.Error); timer.Stop(); }
                    }
                    finally { operation.Release(); }
                }
                UpdatePending(); if (DateTime.UtcNow >= nextStatus) { nextStatus = DateTime.UtcNow.AddSeconds(2); await Refresh(); }
            }
            catch (Exception error) { Log("OUTPUT ERROR: " + error.Message); timer.Stop(); }
            finally { polling = false; }
        }
        private void OnClosing(object sender, CancelEventArgs e)
        {
            if (working || polling || !operation.Wait(0)) { e.Cancel = true; status.Text = "Wait for the current operation before closing."; return; }
            operation.Release(); timer.Stop(); closed = true;
            try { Native.VdUiClose(); } catch (Exception) { }
        }
    }
    internal static class App
    {
        [STAThread]
        private static int Main(string[] args)
        {
            if (args.Length != 0) return 87;
            try
            {
                var identity = System.Security.Principal.WindowsIdentity.GetCurrent();
                using (identity)
                {
                    if (!new System.Security.Principal.WindowsPrincipal(identity).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator))
                    {
                        Process.Start(new ProcessStartInfo(System.Reflection.Assembly.GetExecutingAssembly().Location) { UseShellExecute = true, Verb = "runas" });
                        return 0;
                    }
                }
                return new Application().Run(new MainWindow());
            }
            catch (Win32Exception error)
            {
                if (error.NativeErrorCode == 1223) return 1223;
                MessageBox.Show(error.Message, "Vdevice example", MessageBoxButton.OK, MessageBoxImage.Error);
                return 1;
            }
        }
    }
}
