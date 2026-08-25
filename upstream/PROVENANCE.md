# Where this came from

Everything in this directory is a **verbatim copy** of the original
"Simple String" / TouchString sketch for the Synthux Simple Touch board, taken
from [`Synthux-Academy/simple-touch-instruments`](https://github.com/Synthux-Academy/simple-touch-instruments)
at `daisyduino/TouchString/`.

That repository is **MIT licensed** — `LICENSE.md` here is its licence file,
copyright 2024 Stichting PTM Academy. The original implementation is by
Vlad Litvinenko (bleeptools).

It is committed here unmodified, first, and left in the tree so that every
adaptation in `src/` reads as a diff against a known, licensed root.

## A note on the other TouchString repository

[`Synthux-Academy/TouchString`](https://github.com/Synthux-Academy/TouchString)
is a later re-port of this same sketch from Daisyduino to bare libDaisy, by
Rosa Schuurmans. It is the version the Synthux firmware page ships.

**It carries no licence file of any kind.** Its sibling instrument repos
(`Audrey-II`, `Spotykach`, `simple-touch-instruments`, `simple-fix-instruments`)
are all explicitly MIT, so the omission looks like an oversight rather than a
decision — but an omission is not a grant, so this port does not take code from
it. It was read as a reference, and two of its structural ideas are
acknowledged in `docs/PORTING.md`, but the code here descends from the MIT
sketch above.

If Synthux add a licence to that repository, this note should be revisited.
