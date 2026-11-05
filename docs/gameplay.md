# Gameplay and presentation

The front end has four explicit stages: title, team selection, mode selection, and result. Waiting eight seconds on the title starts an AI demonstration in which all outfield players and both keepers use the same simulation and animation logic as a played match.

Home and away may be Napoli, London, Tokyo, or Rio. Their selected primary colors are applied to the indexed sprite palette. Single match ends after one 90-second fixture. Championship mode continues for three fixtures and tracks home wins before returning to the title.

Outfield animation derives from simulation state: idle, a four-phase walk, close control, and a timed kick follow-through. Keepers have distinct colors and enter a dive/reach pose when a free ball approaches their penalty area. The crowd has faces, bodies, legs, raised arms, banners, parallax motion, and a timed jump animation after goals.

Music starts only on title and result/winner screens and stops before kickoff. Gameplay triggers only football-related effects: kick/pass, whistle/save, and a layered goal roar. The committed night build sets master volume to zero in `sensational_soccer_init`; see `AGENTS.md` for the daytime capture policy.
