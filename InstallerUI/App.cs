using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media.Animation;
using System.Windows.Media;
using System.Web.Script.Serialization;

namespace Vdevice.Setup
{
    internal sealed class SetupWindow : Window
    {
        private readonly TextBox log;
        private readonly TextBlock status;
        private readonly ProgressBar progress;
        private readonly Button install, uninstall, diagnose, refresh, reboot;
        private readonly TextBlock installationState, installationDetail, driverState, driverDetail, apiState, apiDetail;
        private bool busy, restartNeeded;
        private SetupSnapshot snapshot;
        internal SetupWindow() : this(true) { }
        internal SetupWindow(bool automaticStatus)
        {
            using (Stream input = typeof(SetupWindow).Assembly.GetManifestResourceStream("MainWindow.xaml")) Content = XamlReader.Load(input);
            Title = "Vdevice — Virtual USB Device Driver Setup"; Width = 1120; Height = 780; MinWidth = 880; MinHeight = 660;
            FontFamily = new FontFamily("Segoe UI");
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            FrameworkElement root = (FrameworkElement)Content;
            log = (TextBox)root.FindName("Log"); status = (TextBlock)root.FindName("Status"); progress = (ProgressBar)root.FindName("Progress");
            install = (Button)root.FindName("Install"); uninstall = (Button)root.FindName("Uninstall");
            diagnose = (Button)root.FindName("Diagnose"); refresh = (Button)root.FindName("Refresh");
            reboot = (Button)root.FindName("RebootButton");
            installationState = (TextBlock)root.FindName("InstallationState"); installationDetail = (TextBlock)root.FindName("InstallationDetail");
            driverState = (TextBlock)root.FindName("DriverState"); driverDetail = (TextBlock)root.FindName("DriverDetail");
            apiState = (TextBlock)root.FindName("ApiState"); apiDetail = (TextBlock)root.FindName("ApiDetail");
            install.Click += async delegate { await Run("/install"); };
            uninstall.Click += async delegate { await Run("/uninstall"); };
            diagnose.Click += async delegate { new DiagnosticWindow { Owner = this }.ShowDialog(); await Run("/status"); };
            refresh.Click += async delegate { await Run("/status"); };
            ((Button)root.FindName("Copy")).Click += delegate { Copy(); };
            reboot.Click += delegate { Restart(); };
            Closing += OnClosing;
            if (automaticStatus) Loaded += async delegate { await Run("/status"); };
        }
        internal void ApplyResult(SetupResult result, string operation)
        {
            snapshot = result.Snapshot;
            restartNeeded = restartNeeded || result.RebootRequired;
            reboot.Visibility = restartNeeded ? Visibility.Visible : Visibility.Collapsed;
            if (snapshot == null)
            {
                installationState.Text = "Status unavailable"; installationDetail.Text = "The Windows state could not be verified. Read the report below.";
                driverState.Text = apiState.Text = "Not verified"; driverDetail.Text = apiDetail.Text = "Refresh status or run diagnostics.";
            }
            else
            {
                installationState.Text = snapshot.InstallationText;
                installationDetail.Text = snapshot.RootDevices + " root controller(s) · " + snapshot.StagedPackages + " driver package(s)";
                driverState.Text = snapshot.ServiceRunning ? "Running" : snapshot.ServiceInstalled ? "Stopped" : "Not installed";
                driverDetail.Text = snapshot.PayloadMatches ? "Active and configured SYS files match this package." : snapshot.Installed ? "Driver identity is not fully verified. See diagnostics." : "No active Vdevice service.";
                apiState.Text = snapshot.ControllerQuiesced ? "Paused for removal" : snapshot.ApiAvailable ? "Responding" : "Unavailable";
                apiDetail.Text = snapshot.TrialExpired ? "Demo expired. Restart Windows for another two-hour session."
                    : snapshot.ApiAvailable ? snapshot.TrialStarted ? "Demo remaining: " + TimeSpan.FromSeconds(snapshot.TrialRemainingSeconds).ToString(@"hh\:mm\:ss") : "Demo starts when you create a virtual device."
                    : snapshot.ApiError != 0 ? "Windows error " + snapshot.ApiError + ". No virtual device was created by this check."
                    : "Check incomplete · Windows error " + snapshot.QueryError + ". See the detailed report.";
                if (snapshot.LastState == "Failed" || snapshot.LastState == "Running")
                    log.AppendText("Previous setup: " + snapshot.LastOperation + " · " + snapshot.LastState + " · " + snapshot.LastStage + " · exit " + snapshot.LastExitCode + Environment.NewLine);
            }
            install.Content = snapshot != null && snapshot.Installed ? "Reinstall / repair" : "Install";
            status.Text = !result.Success ? "Operation failed · Windows error " + result.ExitCode + ". Read the failed stage below."
                : operation == "/status" ? "Installation status read directly from Windows." : "Operation completed and Windows state checked.";
            if (restartNeeded) status.Text += " Save your work, then restart Windows to finish.";
            if (snapshot != null && snapshot.LastState == "Running") status.Text += " A setup operation is running or was interrupted; check the recorded stage.";
        }
        private void Restart()
        {
            if (busy || !restartNeeded || MessageBox.Show(this, "Save your work before continuing. Restart Windows now?", "Vdevice", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            try
            {
                Process.Start(new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "shutdown.exe"), "/r /t 0 /d p:4:1") { UseShellExecute = false, CreateNoWindow = true });
            }
            catch (Exception error) { log.AppendText("Could not restart Windows: " + error.Message + Environment.NewLine); status.Text = "Restart failed. You can restart Windows from the Start menu."; }
        }
        private void Copy()
        {
            try { Clipboard.SetText(log.Text); status.Text = "Report copied to the clipboard."; }
            catch (Exception error) { status.Text = "Clipboard unavailable: " + error.Message; }
        }
        private void OnClosing(object sender, CancelEventArgs e)
        {
            if (busy) { e.Cancel = true; status.Text = "Wait for the current operation to finish before closing."; }
        }
        private async Task Run(string operation)
        {
            if (busy) return;
            busy = true; install.IsEnabled = uninstall.IsEnabled = diagnose.IsEnabled = refresh.IsEnabled = reboot.IsEnabled = false;
            log.Clear(); progress.BeginAnimation(ProgressBar.ValueProperty, null); progress.Value = 0;
            status.Text = "Working…";
            try
            {
                SetupResult result = await Task.Run(delegate
                {
                    return Package.Run(operation, delegate(int percent, string message)
                    {
                        Dispatcher.BeginInvoke(new Action(delegate
                        {
                            DoubleAnimation animation = new DoubleAnimation(percent, TimeSpan.FromMilliseconds(280));
                            animation.EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut };
                            progress.BeginAnimation(ProgressBar.ValueProperty, animation);
                            log.AppendText(message + Environment.NewLine); log.ScrollToEnd(); status.Text = message;
                        }));
                    }, CancellationToken.None);
                });
                ApplyResult(result, operation);
            }
            catch (Exception error) { log.AppendText("ERROR: " + error.Message + Environment.NewLine); status.Text = "Operation failed."; }
            finally { busy = false; install.IsEnabled = diagnose.IsEnabled = refresh.IsEnabled = reboot.IsEnabled = true; uninstall.IsEnabled = snapshot != null && (snapshot.Installed || restartNeeded); }
        }
    }
    internal sealed class DiagnosticWindow : Window
    {
        private readonly TextBox report = new TextBox();
        private readonly TextBlock state = new TextBlock();
        private readonly ProgressBar progress = new ProgressBar();
        private bool busy = true;
        internal DiagnosticWindow()
        {
            Title = "Vdevice — Read-only diagnostics"; Width = 940; Height = 650; MinWidth = 680; MinHeight = 440;
            WindowStartupLocation = WindowStartupLocation.CenterOwner; Background = new SolidColorBrush(Color.FromRgb(244, 247, 252));
            Grid grid = new Grid { Margin = new Thickness(24) }; Content = grid;
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            StackPanel heading = new StackPanel();
            heading.Children.Add(new TextBlock { Text = "Check the complete installation", FontSize = 23, Foreground = Brushes.MidnightBlue });
            heading.Children.Add(new TextBlock { Text = "Reads the package, Windows device state, service and driver API. No input injection.", Margin = new Thickness(0, 8, 0, 16), Foreground = Brushes.SlateGray, TextWrapping = TextWrapping.Wrap });
            grid.Children.Add(heading);
            progress.Height = 7; progress.Maximum = 100; progress.Foreground = new SolidColorBrush(Color.FromRgb(23, 101, 233)); progress.Margin = new Thickness(0, 0, 0, 16);
            Grid.SetRow(progress, 1); grid.Children.Add(progress);
            report.IsReadOnly = true; report.TextWrapping = TextWrapping.Wrap; report.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            report.FontFamily = new FontFamily("Consolas"); report.FontSize = 12; report.Padding = new Thickness(16);
            report.Background = new SolidColorBrush(Color.FromRgb(19, 41, 68)); report.Foreground = Brushes.AliceBlue; report.BorderThickness = new Thickness(0);
            Grid.SetRow(report, 2); grid.Children.Add(report);
            DockPanel footer = new DockPanel { Margin = new Thickness(0, 16, 0, 0) }; Grid.SetRow(footer, 3); grid.Children.Add(footer);
            Button copy = new Button { Content = "Copy report", Padding = new Thickness(18, 10, 18, 10) }; DockPanel.SetDock(copy, Dock.Right); footer.Children.Add(copy);
            state.Text = "Starting…"; state.VerticalAlignment = VerticalAlignment.Center; state.TextWrapping = TextWrapping.Wrap; state.Margin = new Thickness(0, 0, 12, 0); footer.Children.Add(state);
            copy.Click += delegate { try { Clipboard.SetText(report.Text); } catch (Exception error) { state.Text = "Clipboard unavailable: " + error.Message; } };
            Closing += delegate(object sender, CancelEventArgs e) { if (busy) { e.Cancel = true; state.Text = "Wait for the current check to finish."; } };
            Loaded += async delegate
            {
                try
                {
                    SetupResult result = await Task.Run(delegate
                    {
                        return Package.Run("/diagnose", delegate(int percent, string message)
                        {
                            Dispatcher.BeginInvoke(new Action(delegate
                            {
                                progress.BeginAnimation(ProgressBar.ValueProperty, new DoubleAnimation(percent, TimeSpan.FromMilliseconds(280)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
                                report.AppendText(message + Environment.NewLine); report.ScrollToEnd(); state.Text = message;
                            }));
                        }, CancellationToken.None);
                    });
                    state.Text = result.Success ? "Checks completed. The report explains what was and was not verified." : "An error needs attention. Copy the report for troubleshooting.";
                    if (result.RebootRequired) state.Text += " Restart Windows to finish the pending operation.";
                }
                catch (Exception error) { report.AppendText("ERROR: " + error.Message); state.Text = "The check failed."; }
                finally { busy = false; }
            };
        }
    }
    internal static class App
    {
        [STAThread]
        private static int Main(string[] args)
        {
            if ((args.Length == 2 && args[0] == "--snapshot") || (args.Length == 3 && args[0] == "--action"))
            {
                try
                {
                    string operation = args[0] == "--snapshot" ? "/status" : args[1];
                    SetupResult result = Package.Run(operation, null, CancellationToken.None);
                    string path = args[args.Length - 1];
                    string json = new JavaScriptSerializer().Serialize(new { exitCode = result.ExitCode, rebootRequired = result.RebootRequired, installation = result.Snapshot == null ? "Status unavailable" : result.Snapshot.InstallationText, snapshot = result.Snapshot, report = result.Report });
                    File.WriteAllText(path, json, new UTF8Encoding(false)); return result.ExitCode;
                }
                catch (Exception) { return 1; }
            }
            if (args.Length == 2 && args[0] == "--self-test")
            {
                try { File.WriteAllText(args[1], Package.SelfTest(), new UTF8Encoding(false)); return 0; }
                catch (Exception) { return 1; }
            }
            if (args.Length != 0) return 87;
            Application app = new Application(); return app.Run(new SetupWindow());
        }
    }
}
