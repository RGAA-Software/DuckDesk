using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ParsecDisplay
{
    public static class Program
    {
        // Keep the product worker independent from an upstream ParsecDisplay
        // controller that may already be installed in the same user session.
        public const string AppId = "PixelsPxDisplayControllerV1";
        public const string AppName = "px_display";
        public const string AppVersion = "1.0.2";
        public const string GitHubRepo = "nomi-san/parsec-vdd";

        [STAThread]
        static int Main(string[] args)
        {
            // CLI mode runs short-lived against the user's terminal; skip the
            // log header entirely to avoid polluting debug.log with help text
            // queries. GUI mode triggers Log's static ctor on first use below.
            if (args.Length == 0 ||
                (args[0] != "-cli" && args[0] != "-custom" && args[0] != "-worker"))
                Log.Info("Main start: args=[{0}]", string.Join(" ", args));

            if (args.Length >= 2 && args[0] == "-custom")
            {
                var modes = Display.ParseModes(args[1]);
                Vdd.Utils.SetCustomDisplayModes(modes);

                if (args.Length >= 3)
                {
                    if (Enum.TryParse<Vdd.Utils.ParentGPU>(args[2], true, out var kind))
                    {
                        Vdd.Utils.SetParentGPU(kind);
                    }
                }

                return 0;
            }

            if (args.Length > 0 && args[0] == "-worker")
            {
                if (!SingleInstance())
                    return 0;

                Vdd.Controller.Start();
                try
                {
                    // Product worker mode is deliberately headless. px_service
                    // supervises this process and CLI commands use the named
                    // single-instance event to confirm heartbeat is alive.
                    using (var shutdown = new ManualResetEventSlim(false))
                        shutdown.Wait();
                }
                finally
                {
                    Vdd.Controller.Stop();
                }
                return 0;
            }

            if (args.Length > 0 && args[0] == "-cli")
            {
                args = args.Skip(1).ToArray();
                return CLI.Execute(args);
            }

            if (SingleInstance())
            {
                App.LoadTranslations();
                Helper.StayAwake(false);

                Application.Run(new Tray());
                Log.Info("Main exit");
            }
            else
            {
                Log.Info("Another instance already running, signaling and exiting");
            }

            return 0;
        }

        static bool SingleInstance()
        {
            bool isOwned = false;
            var signal = new EventWaitHandle(false,
                EventResetMode.AutoReset, AppId, out isOwned);

            if (isOwned)
            {
                Task.Run(() =>
                {
                    while (signal.WaitOne())
                    {
                        Tray.Instance?.Invoke(Tray.Instance.ShowApp);
                    }
                });
            }
            else
            {
                signal.Set();
                signal.Dispose();
            }

            return isOwned;
        }
    }
}
