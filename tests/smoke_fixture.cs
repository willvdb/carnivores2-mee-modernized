using System;
using System.IO;
using System.Threading;

internal static class SmokeFixture
{
    private static int Main(string[] args)
    {
        File.WriteAllText("fixture.pid", System.Diagnostics.Process.GetCurrentProcess().Id.ToString());
        string log = args.Length > 2 ? args[2] : "carnivor.log";
        switch (args[0])
        {
            case "clean":
                File.WriteAllText(log, "Runtime fixture ready\n");
                break;
            case "error":
                File.WriteAllText(log, "FATAL: fixture initialization failed\n");
                break;
            case "empty":
                File.WriteAllText(log, "");
                break;
            case "whitespace":
                File.WriteAllText(log, " \r\n\t");
                break;
            case "missing":
                break;
            default:
                return 2;
        }
        if (args[1] != "wait")
            return int.Parse(args[1]);
        Thread.Sleep(Timeout.Infinite);
        return 0;
    }
}
