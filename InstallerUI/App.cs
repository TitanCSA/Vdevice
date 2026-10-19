using System;
using System.ComponentModel;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media.Animation;
using System.Windows.Media;

namespace Vdevice.Setup
{
    internal sealed class SetupWindow : Window
    {
        private readonly TextBox log;
        private readonly TextBlock status;
        private readonly ProgressBar progress;
        private readonly Button install, uninstall, diagnose, refresh;
        private bool busy;
        internal SetupWindow()
        {
            using (Stream input = typeof(SetupWindow).Assembly.GetManifestResourceStream("MainWindow.xaml")) Content = XamlReader.Load(input);
            Title = "Vdevice — Virtual USB Device Driver"; Width = 1080; Height = 760; MinWidth = 840; MinHeight = 620;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            FrameworkElement root = (FrameworkElement)Content;
            log = (TextBox)root.FindName("Log"); status = (TextBlock)root.FindName("Status"); progress = (ProgressBar)root.FindName("Progress");
            install = (Button)root.FindName("Install"); uninstall = (Button)root.FindName("Uninstall");
            diagnose = (Button)root.FindName("Diagnose"); refresh = (Button)root.FindName("Refresh");
            install.Click += async delegate { await Run("/install"); };
            uninstall.Click += async delegate { await Run("/uninstall"); };
            diagnose.Click += delegate { new DiagnosticWindow { Owner = this }.ShowDialog(); };
            refresh.Click += async delegate { await Run("/status"); };
            ((Button)root.FindName("Copy")).Click += delegate { Copy(); };
            Closing += OnClosing;
            Loaded += async delegate { await Run("/status"); };
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
            busy = true; install.IsEnabled = uninstall.IsEnabled = diagnose.IsEnabled = refresh.IsEnabled = false;
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
                status.Text = result.RebootRequired ? "Completed — restart Windows to finish." : result.Success ? "Completed successfully." : "Needs attention — read the detailed report below.";
            }
            catch (Exception error) { log.AppendText("ERROR: " + error.Message + Environment.NewLine); status.Text = "Operation failed."; }
            finally { busy = false; install.IsEnabled = uninstall.IsEnabled = diagnose.IsEnabled = refresh.IsEnabled = true; }
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
