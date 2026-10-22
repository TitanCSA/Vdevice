// Exercises the real packaged UI and process runner without starting setup,
// querying the host driver, displaying a window or restarting Windows.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;

internal static class InstallerStateTests
{
    private const BindingFlags Hidden = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static;
    private static int checks;
    private static void Check(bool condition, string message) { ++checks; if (!condition) throw new InvalidDataException(message); }
    private static Dictionary<string, object> State()
    {
        return new Dictionary<string, object> {
            {"Schema",1},{"RootDevices",0},{"StartedControllers",0},{"ProblemControllers",0},{"StagedPackages",0},
            {"ServiceInstalled",false},{"ServiceRunning",false},{"ApiAvailable",false},{"ApiError",2},{"ImageError",0},
            {"PayloadMatches",false},{"RestartRequired",false},{"TrialExpired",false},{"TrialStarted",false},{"TrialRemainingSeconds",0},
            {"ControllerQuiesced",false},{"QueryError",0},{"LastOperation",""},{"LastStage",""},{"LastState",""},{"LastExitCode",""}
        };
    }
    private static Dictionary<string, object> Running()
    {
        var state=State(); state["RootDevices"]=1;state["StartedControllers"]=1;state["StagedPackages"]=1;
        state["ServiceInstalled"]=state["ServiceRunning"]=state["ApiAvailable"]=state["PayloadMatches"]=true;state["ApiError"]=0;return state;
    }
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length!=1) throw new ArgumentException("Expected the built UI installer path.");
            Assembly assembly=Assembly.LoadFile(Path.GetFullPath(args[0]));
            Type snapshot=assembly.GetType("Vdevice.Setup.SetupSnapshot",true),resultType=assembly.GetType("Vdevice.Setup.SetupResult",true),windowType=assembly.GetType("Vdevice.Setup.SetupWindow",true);
            MethodInfo parse=snapshot.GetMethod("Parse",Hidden),apply=windowType.GetMethod("ApplyResult",Hidden);
            JavaScriptSerializer json=new JavaScriptSerializer();
            Func<Dictionary<string,object>,object> read=delegate(Dictionary<string,object> state){return parse.Invoke(null,new object[]{json.Serialize(state)});};
            Action<Dictionary<string,object>> invalid=delegate(Dictionary<string,object> state){bool rejected=false;try{read(state);}catch(TargetInvocationException){rejected=true;}Check(rejected,"Invalid native snapshot accepted");};
            var malformed=State();malformed.Remove("RootDevices");invalid(malformed);
            malformed=State();malformed["Schema"]=2;invalid(malformed);
            malformed=State();malformed["StartedControllers"]=1;invalid(malformed);
            malformed=State();malformed["ServiceRunning"]=true;invalid(malformed);
            malformed=State();malformed["RootDevices"]=4294967295u;malformed["StartedControllers"]=4294967295u;malformed["ProblemControllers"]=1;invalid(malformed);
            var cases=new List<Tuple<Dictionary<string,object>,string,bool,int>>();
            cases.Add(Tuple.Create(State(),"Not installed",false,0));
            cases.Add(Tuple.Create(Running(),"Installed and ready",false,0));
            var staged=State();staged["StagedPackages"]=1;cases.Add(Tuple.Create(staged,"Package staged · not active",false,2));
            var stopped=Running();stopped["ServiceRunning"]=stopped["ApiAvailable"]=stopped["PayloadMatches"]=false;stopped["QueryError"]=21;cases.Add(Tuple.Create(stopped,"Installed · needs attention",false,21));
            var pending=Running();pending["RestartRequired"]=true;pending["QueryError"]=3010;pending["LastOperation"]="Install";cases.Add(Tuple.Create(pending,"Installed · restart required",true,3010));
            var removal=State();removal["RestartRequired"]=true;removal["LastOperation"]="Uninstall";removal["QueryError"]=3010;cases.Add(Tuple.Create(removal,"Removal pending restart",true,3010));
            var failed=Running();failed["PayloadMatches"]=false;failed["ImageError"]=1306;failed["QueryError"]=1306;cases.Add(Tuple.Create(failed,"Installed · needs attention",false,1306));
            var denied=State();denied["QueryError"]=5;cases.Add(Tuple.Create(denied,"Status unavailable",false,5));
            var expired=Running();expired["TrialExpired"]=true;cases.Add(Tuple.Create(expired,"Installed · demo expired",false,0));
            var paused=Running();paused["ControllerQuiesced"]=paused["RestartRequired"]=true;paused["QueryError"]=21;cases.Add(Tuple.Create(paused,"Installed · restart required",true,21));
            foreach(var test in cases)
            {
                object state=read(test.Item1),result=Activator.CreateInstance(resultType,true);
                resultType.GetField("Snapshot",Hidden).SetValue(result,state);resultType.GetField("ExitCode",Hidden).SetValue(result,test.Item4);
                Check((string)snapshot.GetProperty("InstallationText",Hidden).GetValue(state,null)==test.Item2,"Installation classification mismatch");
                Check((bool)resultType.GetProperty("RebootRequired",Hidden).GetValue(result,null)==test.Item3,"Restart state lost, including failure + restart");
                Window window=(Window)Activator.CreateInstance(windowType,Hidden,null,new object[]{false},null);
                try
                {
                    apply.Invoke(window,new object[]{result,"/status"});var root=(FrameworkElement)window.Content;
                    Check(((TextBlock)root.FindName("InstallationState")).Text==test.Item2,"Wrong installed state displayed");
                    Check((((Button)root.FindName("RebootButton")).Visibility==Visibility.Visible)==test.Item3,"Wrong restart button visibility");
                    if(test.Item4!=0 && test.Item4!=3010)Check(((TextBlock)root.FindName("Status")).Text.StartsWith("Operation failed",StringComparison.Ordinal),"Failure was replaced by a successful restart message");
                    if(test.Item3)
                    {
                        object refresh=Activator.CreateInstance(resultType,true);resultType.GetField("Snapshot",Hidden).SetValue(refresh,read(Running()));
                        apply.Invoke(window,new object[]{refresh,"/status"});Check(((Button)root.FindName("RebootButton")).Visibility==Visibility.Visible,"Refresh hid a pending restart");
                    }
                }
                finally{window.Close();}
            }
            Type package=assembly.GetType("Vdevice.Setup.Package",true);
            bool cleanupRejected=false;try{package.GetMethod("Cleanup",Hidden).Invoke(null,new object[]{Path.GetTempPath()});}catch(TargetInvocationException error){cleanupRejected=error.InnerException is IOException;}
            Check(cleanupRejected,"Cleanup accepted an unrelated directory");
            var worker=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe"));
            string script="for($i=0;$i -lt 256;$i++){[Console]::Out.WriteLine('OUT '+$i);[Console]::Error.WriteLine('ERR '+$i)};exit 3010";
            worker.Arguments="-NoProfile -NonInteractive -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
            worker.UseShellExecute=false;worker.CreateNoWindow=true;worker.RedirectStandardOutput=worker.RedirectStandardError=true;worker.StandardOutputEncoding=worker.StandardErrorEncoding=Encoding.UTF8;
            int count=0;bool lastOut=false,lastError=false;
            Action<string> receive=delegate(string line){++count;lastOut|=line=="OUT 255";lastError|=line=="ERR 255";if(count==1)throw new IOException("Simulated UI callback failure");};
            int exit=(int)package.GetMethod("RunHelper",Hidden).Invoke(null,new object[]{worker,receive});
            Check(exit==3010,"Worker exit code changed");Check(count==512 && lastOut && lastError,"Worker output was truncated or a callback error stopped the process");
            Console.WriteLine("PASS: "+checks+" installer state, real UI, restart, cleanup and process-output checks; no host setup or restart.");return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
