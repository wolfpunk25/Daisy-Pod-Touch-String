# Credits

**[Vlad Litvinenko](https://github.com/bleeptools)** — the original TouchString
/ "Simple String" for the Synthux Simple Touch. The arpeggiator, the Christoffel
pattern generator, the humanizer, the clock and the scales are all his, and the
instrument is his idea.

**[Rosa Schuurmans](https://github.com/vitrinekast)** — the later libDaisy
re-port of that sketch, read here as a reference. Two of its judgements are
followed: dropping the arpeggiator's unused gate machinery, and fixing the
missing return in the humanizer. No code was taken from it, because that
repository carries no licence — see `upstream/PROVENANCE.md`.

**[Synthux Academy](https://www.synthux.academy) / Stichting PTM Academy** — for
publishing their instruments under MIT, which is the only reason this port could
be made.

**[Electrosmith](https://www.electro-smith.com)** — libDaisy and DaisySP.

The reverb is not upstream's. `daisysp::ReverbSc` was removed from DaisySP in
their LGPL purge, so `src/string/space.*` is a feedback delay network written
for the Wrangler Sampler Pod port and carried over here.
