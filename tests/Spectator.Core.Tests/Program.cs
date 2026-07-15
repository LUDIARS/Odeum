using Spectator.Core.Capture;
using Spectator.Core.Sessions;

namespace Spectator.Core.Tests;

internal static class Program
{
    public static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
            ("Playtime excludes inactive intervals", PlaytimeExcludesInactiveIntervals),
            ("Repeated transitions are idempotent", RepeatedTransitionsAreIdempotent),
            ("Capture anchor uses injected identifier", CaptureAnchorUsesInjectedIdentifier),
        };

        foreach (var test in tests)
        {
            test.Run();
            Console.WriteLine($"PASS {test.Name}");
        }

        return 0;
    }

    private static void PlaytimeExcludesInactiveIntervals()
    {
        var time = new ManualTimeSource(new DateTimeOffset(2026, 7, 13, 0, 0, 0, TimeSpan.Zero));
        var session = new PlaySessionClock(time);

        session.Start(isActive: true);
        time.Advance(TimeSpan.FromSeconds(10));
        session.SetActive(isActive: false);
        time.Advance(TimeSpan.FromSeconds(5));
        session.SetActive(isActive: true);
        time.Advance(TimeSpan.FromSeconds(2));

        var snapshot = session.Snapshot();
        Check.Equal(TimeSpan.FromSeconds(17), snapshot.Elapsed, "Elapsed playtime differs");
        Check.Equal(TimeSpan.FromSeconds(12), snapshot.Active, "Active playtime differs");
        Check.True(snapshot.Active <= snapshot.Elapsed, "Active playtime exceeded elapsed playtime");
    }

    private static void RepeatedTransitionsAreIdempotent()
    {
        var time = new ManualTimeSource(DateTimeOffset.UnixEpoch);
        var session = new PlaySessionClock(time);

        session.Start(isActive: false);
        session.SetActive(isActive: false);
        time.Advance(TimeSpan.FromSeconds(3));
        session.SetActive(isActive: true);
        session.SetActive(isActive: true);
        time.Advance(TimeSpan.FromSeconds(4));

        var snapshot = session.Snapshot();
        Check.Equal(TimeSpan.FromSeconds(4), snapshot.Active, "Repeated state transition changed active time");
    }

    private static void CaptureAnchorUsesInjectedIdentifier()
    {
        var expected = Guid.Parse("69ca3205-f731-486b-a8f5-18a69f065c69");
        var factory = new CaptureAnchorFactory(new FixedIdentifierSource(expected));
        var playtime = new PlaytimeSnapshot(
            DateTimeOffset.UnixEpoch,
            DateTimeOffset.UnixEpoch.AddMinutes(1),
            TimeSpan.FromMinutes(1),
            TimeSpan.FromSeconds(50));

        var anchor = factory.Create(playtime);
        Check.Equal(expected, anchor.Id, "Capture anchor identifier differs");
        Check.Equal(playtime.CapturedAt, anchor.CapturedAt, "Capture timestamp differs");
    }
}
