# Curve control interaction checks

Build OrbedenCore.CSharp first, then run:

```powershell
dotnet run --project Build/CurveControlChecks/CurveControlChecks.csproj
```

Runs the production C# curve implementation against a deterministic GUI input host.
Covers preview/commit/cancel, key selection, insertion/deletion, ordered times, and RGBA gradient edits.
Native canvas rendering and actual Inspector screenshots require separate visual verification.
